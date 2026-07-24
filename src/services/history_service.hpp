#pragma once
#include <QObject>
#include <QDate>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSqlDatabase>

namespace freight::services {

class HistoryService : public QObject {
    Q_OBJECT

public:
    explicit HistoryService(const QString &db_path, QObject *parent = nullptr);
    ~HistoryService() override;

    bool Init();

    qint64 AddHistory(const QVariantMap &record);
    QVariantList QueryHistory(int page = 1, int page_size = 20,
                               const QString &keyword = QString(),
                               const QDate &date_from = QDate(),
                               const QDate &date_to = QDate());
    bool DeleteHistory(qint64 id);
    bool DeleteHistoryList(const QList<qint64> &ids);
    int CleanupOldData(int keep_days = 90);
    QVariantMap GetHistory(qint64 id);
    int GetTotalCount();

private:
    QString db_path_;
    QString conn_name_;
    QSqlDatabase db_;

    bool EnsureTable();
};

} // namespace freight::services
