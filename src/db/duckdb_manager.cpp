#include "db/duckdb_manager.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>

namespace freight::db {

DuckDBManager& DuckDBManager::Instance() {
    static DuckDBManager instance;
    return instance;
}

bool DuckDBManager::Init(const QString &db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        db_path_ = db_path;
        db_ = std::make_unique<duckdb::DuckDB>(db_path.toStdString());
        // 加载 Excel 扩展（DuckDB 1.5 中 read_xlsx 来自 excel 扩展）
        try {
            auto con = CreateConnection();
            con.Query("INSTALL excel");
            con.Query("LOAD excel");
            qDebug() << "Excel extension loaded";
        } catch (const std::exception &e) {
            qWarning() << "Excel extension load warning:" << e.what();
        }
        qDebug() << "DuckDB initialized:" << db_path;
        return true;
    } catch (const std::exception &e) {
        qCritical() << "DuckDB init failed:" << e.what();
        return false;
    }
}

bool DuckDBManager::LoadRulesFromSQLite(const QString &rules_db_path) {
    try {
        auto con = CreateConnection();

        QFileInfo fi(rules_db_path);
        if (!fi.exists()) {
            qCritical() << "Rules db not found:" << rules_db_path;
            return false;
        }

        // 用 Qt 打开 SQLite 数据库
        QSqlDatabase sqlite_db = QSqlDatabase::addDatabase("QSQLITE", "duckdb_import");
        sqlite_db.setDatabaseName(rules_db_path);
        if (!sqlite_db.open()) {
            qCritical() << "Failed to open SQLite db for import:" << sqlite_db.lastError().text();
            return false;
        }

        struct TableDef {
            QString name;
            QString create_sql;
        };

        QList<TableDef> tables = {
            {"customers", R"SQL(
                CREATE TABLE customers (
                    customer_id VARCHAR PRIMARY KEY,
                    customer_name VARCHAR NOT NULL,
                    discount_rate DOUBLE DEFAULT 1.0,
                    default_template VARCHAR,
                    contact_person VARCHAR,
                    contact_phone VARCHAR,
                    address VARCHAR,
                    created_at TIMESTAMP,
                    updated_at TIMESTAMP
                )
            )SQL"},
            {"freight_templates", R"SQL(
                CREATE TABLE freight_templates (
                    template_id VARCHAR PRIMARY KEY,
                    template_name VARCHAR NOT NULL,
                    carrier_name VARCHAR,
                    first_weight DOUBLE DEFAULT 1.0,
                    additional_unit DOUBLE DEFAULT 1.0,
                    vol_weight_ratio DOUBLE DEFAULT 6000.0,
                    description VARCHAR,
                    is_default INTEGER DEFAULT 0,
                    created_at TIMESTAMP,
                    updated_at TIMESTAMP
                )
            )SQL"},
            {"zone_groups", R"SQL(
                CREATE TABLE zone_groups (
                    id INTEGER,
                    template_id VARCHAR NOT NULL,
                    group_code VARCHAR NOT NULL,
                    group_name VARCHAR NOT NULL,
                    sort_order INTEGER DEFAULT 0
                )
            )SQL"},
            {"zone_group_provinces", R"SQL(
                CREATE TABLE zone_group_provinces (
                    id INTEGER,
                    template_id VARCHAR NOT NULL,
                    group_code VARCHAR NOT NULL,
                    province VARCHAR NOT NULL
                )
            )SQL"},
            {"tiered_pricing", R"SQL(
                CREATE TABLE tiered_pricing (
                    id INTEGER,
                    template_id VARCHAR NOT NULL,
                    group_code VARCHAR NOT NULL,
                    tier_code VARCHAR NOT NULL,
                    tier_name VARCHAR,
                    min_weight DOUBLE NOT NULL,
                    max_weight DOUBLE NOT NULL,
                    first_weight DOUBLE DEFAULT 1.0,
                    first_price DOUBLE NOT NULL,
                    additional_unit DOUBLE DEFAULT 1.0,
                    additional_price DOUBLE NOT NULL,
                    sort_order INTEGER DEFAULT 0
                )
            )SQL"},
            {"fuel_surcharge", R"SQL(
                CREATE TABLE fuel_surcharge (
                    id INTEGER,
                    template_id VARCHAR NOT NULL,
                    effective_date DATE NOT NULL,
                    rate DOUBLE NOT NULL,
                    is_active INTEGER DEFAULT 1
                )
            )SQL"},
            {"remote_areas", R"SQL(
                CREATE TABLE remote_areas (
                    id INTEGER,
                    template_id VARCHAR NOT NULL,
                    province VARCHAR,
                    city VARCHAR,
                    district VARCHAR,
                    surcharge DOUBLE DEFAULT 0,
                    is_active INTEGER DEFAULT 1
                )
            )SQL"},
            {"surcharge_strategies", R"SQL(
                CREATE TABLE surcharge_strategies (
                    id INTEGER,
                    strategy_id VARCHAR UNIQUE NOT NULL,
                    strategy_name VARCHAR NOT NULL,
                    strategy_scope VARCHAR NOT NULL,
                    template_id VARCHAR,
                    strategy_type VARCHAR NOT NULL,
                    amount DOUBLE NOT NULL DEFAULT 0,
                    min_weight DOUBLE,
                    max_weight DOUBLE,
                    priority INTEGER NOT NULL DEFAULT 0,
                    is_active INTEGER NOT NULL DEFAULT 1,
                    description VARCHAR,
                    created_at TIMESTAMP,
                    updated_at TIMESTAMP
                )
            )SQL"},
            {"surcharge_provinces", R"SQL(
                CREATE TABLE surcharge_provinces (
                    id INTEGER,
                    strategy_id VARCHAR NOT NULL,
                    province VARCHAR NOT NULL
                )
            )SQL"},
            {"surcharge_customers", R"SQL(
                CREATE TABLE surcharge_customers (
                    id INTEGER,
                    strategy_id VARCHAR NOT NULL,
                    customer_id VARCHAR NOT NULL
                )
            )SQL"},
            {"surcharge_date_ranges", R"SQL(
                CREATE TABLE surcharge_date_ranges (
                    id INTEGER,
                    strategy_id VARCHAR NOT NULL,
                    start_date DATE NOT NULL,
                    end_date DATE NOT NULL,
                    week_days VARCHAR
                )
            )SQL"},
        };

        // 删除旧表，创建新表
        for (const auto &t : tables) {
            con.Query(QString("DROP TABLE IF EXISTS %1").arg(t.name).toStdString());
            con.Query(t.create_sql.toStdString());
        }

        // 从 SQLite 读取数据并插入到 DuckDB
        for (const auto &t : tables) {
            QSqlQuery q(sqlite_db);
            q.exec(QString("SELECT * FROM %1").arg(t.name));
            
            int count = 0;
            while (q.next()) {
                QStringList values;
                for (int i = 0; i < q.record().count(); i++) {
                    QVariant v = q.value(i);
                    if (v.isNull()) {
                        values << "NULL";
                    } else if (v.type() == QVariant::Int || v.type() == QVariant::LongLong) {
                        values << v.toString();
                    } else if (v.type() == QVariant::Double) {
                        values << v.toString();
                    } else {
                        QString s = v.toString();
                        s.replace("'", "''");
                        values << QString("'%1'").arg(s);
                    }
                }
                
                QString insert_sql = QString("INSERT INTO %1 VALUES (%2)")
                    .arg(t.name, values.join(", "));
                con.Query(insert_sql.toStdString());
                count++;
            }
            
            qDebug() << "  Loaded rule table:" << t.name << "(" << count << "rows)";
        }

        sqlite_db.close();
        QSqlDatabase::removeDatabase("duckdb_import");

        qDebug() << "Rules loaded from SQLite";
        return true;
    } catch (const std::exception &e) {
        qCritical() << "Load rules failed:" << e.what();
        return false;
    }
}

bool DuckDBManager::ReloadRules(const QString &rules_db_path) {
    return LoadRulesFromSQLite(rules_db_path);
}

duckdb::Connection DuckDBManager::CreateConnection() {
    return duckdb::Connection(*db_);
}

bool DuckDBManager::ImportFromFile(const QString &table_name, const QString &file_path) {
    try {
        auto con = CreateConnection();
        QFileInfo fi(file_path);
        QString suffix = fi.suffix().toLower();

        QString sql;
        if (suffix == "csv") {
            sql = QString("CREATE OR REPLACE TABLE %1 AS SELECT * FROM read_csv('%2', AUTO_DETECT=TRUE, HEADER=TRUE)")
                .arg(table_name, file_path);
        } else if (suffix == "parquet") {
            sql = QString("CREATE OR REPLACE TABLE %1 AS SELECT * FROM read_parquet('%2')")
                .arg(table_name, file_path);
        } else if (suffix == "xlsx" || suffix == "xls") {
            // DuckDB 1.5 中函数名为 read_xlsx（来自 excel 扩展），不是 read_excel
            sql = QString("CREATE OR REPLACE TABLE %1 AS SELECT * FROM read_xlsx('%2', header=true)")
                .arg(table_name, file_path);
        } else {
            return false;
        }

        con.Query(sql.toStdString());
        return true;
    } catch (const std::exception &e) {
        qCritical() << "Import failed:" << e.what();
        return false;
    }
}

bool DuckDBManager::ExportToFile(const QString &table_name, const QString &file_path) {
    try {
        auto con = CreateConnection();
        QFileInfo fi(file_path);
        QString suffix = fi.suffix().toLower();

        QString sql;
        if (suffix == "csv") {
            sql = QString("COPY %1 TO '%2' (FORMAT CSV, HEADER TRUE)")
                .arg(table_name, file_path);
        } else if (suffix == "parquet") {
            sql = QString("COPY %1 TO '%2' (FORMAT PARQUET)")
                .arg(table_name, file_path);
        } else if (suffix == "xlsx") {
            // DuckDB 1.5 中 excel 扩展提供 FORMAT xlsx（不是 GDAL）
            sql = QString("COPY %1 TO '%2' (FORMAT xlsx)")
                .arg(table_name, file_path);
        } else {
            return false;
        }

        con.Query(sql.toStdString());
        return true;
    } catch (const std::exception &e) {
        qCritical() << "Export failed:" << e.what();
        return false;
    }
}

int64_t DuckDBManager::GetRowCount(const QString &table_name) {
    try {
        auto con = CreateConnection();
        auto result = con.Query(QString("SELECT COUNT(*) FROM %1").arg(table_name).toStdString());
        return result->GetValue(0, 0).GetValue<int64_t>();
    } catch (...) {
        return 0;
    }
}

bool DuckDBManager::TableExists(const QString &table_name) {
    try {
        auto con = CreateConnection();
        auto result = con.Query(
            QString("SELECT 1 FROM information_schema.tables WHERE table_name = '%1'")
                .arg(table_name.toLower()).toStdString());
        return result->RowCount() > 0;
    } catch (...) {
        return false;
    }
}

} // namespace freight::db
