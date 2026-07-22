#include "services/history_service.hpp"
#include <QSqlDatabase>
#include <QSqlQuery>

namespace freight::services {

HistoryService::HistoryService(const QString &db_path, QObject *parent)
    : QObject(parent), db_path_(db_path) {}

HistoryService::~HistoryService() = default;

bool HistoryService::Init() {
    // TODO: 实现历史记录数据库初始化
    return true;
}

qint64 HistoryService::AddHistory(const QVariantMap &record) {
    Q_UNUSED(record)
    return 0;
}

QVariantList HistoryService::QueryHistory(int page, int page_size) {
    Q_UNUSED(page)
    Q_UNUSED(page_size)
    return {};
}

bool HistoryService::DeleteHistory(qint64 id) {
    Q_UNUSED(id)
    return true;
}

int HistoryService::CleanupOldData(int keep_days) {
    Q_UNUSED(keep_days)
    return 0;
}

} // namespace freight::services
