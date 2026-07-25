#include "db/duckdb_manager.hpp"
#include "core/app_config.hpp"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QMetaObject>
#include <QTimer>

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

        auto &cfg = core::AppConfig::Instance();
        try {
            auto con = CreateConnection();
            QString mem_limit = QString("%1MB").arg(cfg.GetMemoryLimitMB());
            QString threads = QString::number(cfg.GetThreadCount());
            con.Query(QString("SET memory_limit = '%1'").arg(mem_limit).toStdString());
            con.Query(QString("SET threads = %1").arg(threads).toStdString());
            qDebug() << "DuckDB performance: memory_limit =" << mem_limit << ", threads =" << threads;
        } catch (const std::exception &e) {
            qWarning() << "Set DuckDB performance config warning:" << e.what();
        }

        // 加载 Excel 扩展（先尝试 LOAD 跳过不必要的重复 INSTALL；LOAD 失败再 INSTALL）
        try {
            auto con = CreateConnection();
            try {
                con.Query("LOAD excel");
                qDebug() << "Excel extension loaded (pre-installed)";
            } catch (...) {
                con.Query("INSTALL excel");
                con.Query("LOAD excel");
                qDebug() << "Excel extension installed & loaded";
            }
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

        // 唯一化连接名，避免多次 ReloadRules 时 duplicate connection 警告
        static int import_conn_counter = 0;
        QString conn_name = QString("duckdb_import_%1").arg(++import_conn_counter);
        if (QSqlDatabase::contains(conn_name)) {
            QSqlDatabase::removeDatabase(conn_name);
        }

        // 用 Qt 打开 SQLite 数据库
        QSqlDatabase sqlite_db = QSqlDatabase::addDatabase("QSQLITE", conn_name);
        sqlite_db.setDatabaseName(rules_db_path);
        if (!sqlite_db.open()) {
            qCritical() << "Failed to open SQLite db for import:" << sqlite_db.lastError().text();
            QSqlDatabase::removeDatabase(conn_name);
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
                    default_no_weight_fee DOUBLE DEFAULT 0,
                    description VARCHAR,
                    is_default INTEGER DEFAULT 0,
                    created_at TIMESTAMP,
                    updated_at TIMESTAMP,
                    tpl_rounding_mode VARCHAR DEFAULT 'ceil_0_1kg',
                    tpl_additional_unit DOUBLE DEFAULT 1.0,
                    tpl_vol_divisor INTEGER DEFAULT 6000
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
            int count = 0;
            {   // QSqlQuery 作用域：结束后立即释放 prepared statement，避免 close 时 "still in use"
                QSqlQuery q(sqlite_db);
                q.exec(QString("SELECT * FROM %1").arg(t.name));

                while (q.next()) {
                    QStringList values;
                    for (int i = 0; i < q.record().count(); i++) {
                        QVariant v = q.value(i);
                        if (v.isNull()) {
                            values << "NULL";
                            continue;
                        }
                        // 覆盖所有数值类型，避免 UInt/Bool/Float 被当作字符串插入数值列导致错位
                        const auto mt = v.metaType();
                        const int id = mt.id();
                        bool is_numeric = false;
                        switch (id) {
                            case QMetaType::Bool:
                                values << (v.toBool() ? "1" : "0");
                                is_numeric = true;
                                break;
                            case QMetaType::Int:
                            case QMetaType::UInt:
                            case QMetaType::LongLong:
                            case QMetaType::ULongLong:
                            case QMetaType::Float:
                            case QMetaType::Double:
                                values << v.toString();
                                is_numeric = true;
                                break;
                            default:
                                is_numeric = false;
                                break;
                        }
                        if (is_numeric) continue;

                        QString s = v.toString();
                        s.replace("'", "''");
                        values << QString("'%1'").arg(s);
                    }

                    QString insert_sql = QString("INSERT INTO %1 VALUES (%2)")
                        .arg(t.name, values.join(", "));
                    con.Query(insert_sql.toStdString());
                    count++;
                }
            }

            qDebug() << "  Loaded rule table:" << t.name << "(" << count << "rows)";
        }

        // 关闭 SQLite 句柄；延迟到下一事件循环再 removeDatabase，避免 QSqlDatabase/驱动内部仍持有引用导致 "still in use" 告警
        sqlite_db.close();
        const QString cn = conn_name;
        QTimer::singleShot(0, [cn]() {
            if (QSqlDatabase::contains(cn)) {
                QSqlDatabase::removeDatabase(cn);
            }
        });

        qDebug() << "Rules loaded from SQLite";
        return true;
    } catch (const std::exception &e) {
        // 异常路径同样用延迟删除兜底
        for (int i = 1; i <= 16; i++) {
            QString cn = QString("duckdb_import_%1").arg(i);
            if (QSqlDatabase::contains(cn)) {
                QSqlDatabase::removeDatabase(cn);
            }
        }
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
        QString abs_path = fi.absoluteFilePath();

        // 1. 保证父目录存在
        QDir().mkpath(fi.absolutePath());
        if (!QDir(fi.absolutePath()).exists()) {
            qCritical() << "Export failed: cannot create parent dir:" << fi.absolutePath();
            return false;
        }

        // 2. 路径中的单引号需要双写（DuckDB SQL 字符串标准转义），否则会 Parser syntax error
        QString escaped_path = abs_path;
        escaped_path.replace("'", "''");

        QString suffix = fi.suffix().toLower();
        QString sql;
        if (suffix == "csv") {
            sql = QString("COPY %1 TO '%2' (FORMAT CSV, HEADER TRUE)")
                .arg(table_name, escaped_path);
        } else if (suffix == "parquet") {
            sql = QString("COPY %1 TO '%2' (FORMAT PARQUET)")
                .arg(table_name, escaped_path);
        } else if (suffix == "xlsx" || suffix == "xls") {
            // .xls 输出按 xlsx 格式（DuckDB xlsx 写在 Excel 里可正常打开）
            sql = QString("COPY %1 TO '%2' (FORMAT xlsx, HEADER TRUE)")
                .arg(table_name, escaped_path);
        } else {
            qCritical() << "Export failed: unsupported suffix:" << suffix;
            return false;
        }

        qDebug() << "ExportToFile:" << sql;
        con.Query(sql.toStdString());
        if (!QFileInfo::exists(abs_path) || QFileInfo(abs_path).size() == 0) {
            qCritical() << "Export failed: output file missing or empty:" << abs_path;
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        qCritical() << "Export failed: table=" << table_name << "path=" << file_path << "err=" << e.what();
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
    // 用 SHOW TABLES LIKE 替代 information_schema（DuckDB 偶发 information_schema 查不到的坑）
    try {
        auto con = CreateConnection();
        auto result = con.Query(
            QString("SHOW TABLES LIKE '%1'").arg(table_name.toLower()).toStdString());
        return result->RowCount() > 0;
    } catch (...) {
        // 兜底：尝试 DESCRIBE 表，若不抛错则存在
        try {
            auto con = CreateConnection();
            con.Query(QString("DESCRIBE SELECT * FROM %1 LIMIT 1").arg(table_name).toStdString());
            return true;
        } catch (...) {
            return false;
        }
    }
}

} // namespace freight::db
