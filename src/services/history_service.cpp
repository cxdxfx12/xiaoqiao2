#include "services/history_service.hpp"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDate>
#include <QDateTime>
#include <QDebug>

namespace freight::services {

HistoryService::HistoryService(const QString &db_path, QObject *parent)
    : QObject(parent), db_path_(db_path) {
    static int conn_counter = 0;
    conn_name_ = QString("history_conn_%1").arg(++conn_counter);
}

HistoryService::~HistoryService() {
    if (db_.isOpen()) {
        db_.close();
    }
    if (!conn_name_.isEmpty()) {
        QSqlDatabase::removeDatabase(conn_name_);
    }
}

bool HistoryService::Init() {
    if (db_.isOpen()) return true;

    if (QSqlDatabase::contains(conn_name_)) {
        QSqlDatabase::removeDatabase(conn_name_);
    }
    db_ = QSqlDatabase::addDatabase("QSQLITE", conn_name_);
    db_.setDatabaseName(db_path_);

    if (!db_.open()) {
        qCritical() << "Failed to open history database:" << db_.lastError().text();
        return false;
    }

    return EnsureTable();
}

bool HistoryService::EnsureTable() {
    QSqlQuery q(db_);
    QString sql = R"SQL(
        CREATE TABLE IF NOT EXISTS calc_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            task_name TEXT NOT NULL,
            input_file TEXT,
            output_file TEXT,
            total_rows INTEGER DEFAULT 0,
            total_fee REAL DEFAULT 0,
            duration_ms INTEGER DEFAULT 0,
            status INTEGER DEFAULT 1,
            error_msg TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )SQL";

    if (!q.exec(sql)) {
        qCritical() << "Create history table failed:" << q.lastError().text();
        return false;
    }

    return true;
}

qint64 HistoryService::AddHistory(const QVariantMap &record) {
    if (!db_.isOpen() && !Init()) {
        return 0;
    }

    QSqlQuery q(db_);
    q.prepare(R"SQL(
        INSERT INTO calc_history
        (task_name, input_file, output_file, total_rows, total_fee, duration_ms, status, error_msg)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )SQL");

    q.addBindValue(record.value("task_name", record.value("input_file", "未命名任务")).toString());
    q.addBindValue(record.value("input_file", "").toString());
    q.addBindValue(record.value("output_file", "").toString());
    q.addBindValue(record.value("total_rows", 0).toInt());
    q.addBindValue(record.value("total_fee", 0.0).toDouble());
    q.addBindValue(record.value("duration_ms", 0).toInt());
    q.addBindValue(record.value("status", 1).toInt());
    q.addBindValue(record.value("error_msg", "").toString());

    if (!q.exec()) {
        qCritical() << "Add history failed:" << q.lastError().text();
        return 0;
    }

    return q.lastInsertId().toLongLong();
}

QVariantList HistoryService::QueryHistory(int page, int page_size,
                                           const QString &keyword,
                                           const QDate &date_from,
                                           const QDate &date_to) {
    QVariantList result;

    if (!db_.isOpen() && !Init()) {
        return result;
    }

    QSqlQuery q(db_);
    QString sql = "SELECT * FROM calc_history WHERE 1=1";
    QList<QVariant> params;

    if (!keyword.isEmpty()) {
        sql += " AND task_name LIKE ?";
        params << "%" + keyword + "%";
    }

    if (date_from.isValid()) {
        sql += " AND DATE(created_at) >= ?";
        params << date_from.toString("yyyy-MM-dd");
    }

    if (date_to.isValid()) {
        sql += " AND DATE(created_at) <= ?";
        params << date_to.toString("yyyy-MM-dd");
    }

    sql += " ORDER BY created_at DESC LIMIT ? OFFSET ?";
    params << page_size << (page - 1) * page_size;

    q.prepare(sql);
    for (const auto &p : params) {
        q.addBindValue(p);
    }

    if (!q.exec()) {
        qCritical() << "Query history failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        QVariantMap m;
        m["id"] = q.value("id").toLongLong();
        m["task_name"] = q.value("task_name").toString();
        m["input_file"] = q.value("input_file").toString();
        m["output_file"] = q.value("output_file").toString();
        m["total_rows"] = q.value("total_rows").toInt();
        m["total_fee"] = q.value("total_fee").toDouble();
        m["duration_ms"] = q.value("duration_ms").toInt();
        m["status"] = q.value("status").toInt();
        m["error_msg"] = q.value("error_msg").toString();
        m["created_at"] = q.value("created_at").toString();
        result << m;
    }

    return result;
}

bool HistoryService::DeleteHistory(qint64 id) {
    if (!db_.isOpen() && !Init()) {
        return false;
    }

    QSqlQuery q(db_);
    q.prepare("DELETE FROM calc_history WHERE id = ?");
    q.addBindValue(id);

    if (!q.exec()) {
        qCritical() << "Delete history failed:" << q.lastError().text();
        return false;
    }

    return true;
}

bool HistoryService::DeleteHistoryList(const QList<qint64> &ids) {
    if (ids.isEmpty()) return true;
    if (!db_.isOpen() && !Init()) {
        return false;
    }

    db_.transaction();

    bool ok = true;
    for (qint64 id : ids) {
        QSqlQuery q(db_);
        q.prepare("DELETE FROM calc_history WHERE id = ?");
        q.addBindValue(id);
        if (!q.exec()) {
            ok = false;
            break;
        }
    }

    if (ok) {
        db_.commit();
    } else {
        db_.rollback();
    }

    return ok;
}

int HistoryService::CleanupOldData(int keep_days) {
    if (!db_.isOpen() && !Init()) {
        return 0;
    }

    QSqlQuery q(db_);
    q.prepare("DELETE FROM calc_history WHERE DATE(created_at) < DATE('now', ?)");
    q.addBindValue(QString("-%1 days").arg(keep_days));

    if (!q.exec()) {
        qCritical() << "Cleanup history failed:" << q.lastError().text();
        return 0;
    }

    return q.numRowsAffected();
}

QVariantMap HistoryService::GetHistory(qint64 id) {
    QVariantMap result;

    if (!db_.isOpen() && !Init()) {
        return result;
    }

    QSqlQuery q(db_);
    q.prepare("SELECT * FROM calc_history WHERE id = ?");
    q.addBindValue(id);

    if (!q.exec() || !q.next()) {
        return result;
    }

    result["id"] = q.value("id").toLongLong();
    result["task_name"] = q.value("task_name").toString();
    result["input_file"] = q.value("input_file").toString();
    result["output_file"] = q.value("output_file").toString();
    result["total_rows"] = q.value("total_rows").toInt();
    result["total_fee"] = q.value("total_fee").toDouble();
    result["duration_ms"] = q.value("duration_ms").toInt();
    result["status"] = q.value("status").toInt();
    result["error_msg"] = q.value("error_msg").toString();
    result["created_at"] = q.value("created_at").toString();

    return result;
}

int HistoryService::GetTotalCount() {
    if (!db_.isOpen() && !Init()) {
        return 0;
    }

    QSqlQuery q(db_);
    if (!q.exec("SELECT COUNT(*) FROM calc_history") || !q.next()) {
        return 0;
    }

    return q.value(0).toInt();
}

} // namespace freight::services
