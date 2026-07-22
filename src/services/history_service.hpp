#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace freight::services {

class HistoryService : public QObject {
    Q_OBJECT

public:
    explicit HistoryService(const QString &db_path, QObject *parent = nullptr);
    ~HistoryService() override;

    bool Init();

    qint64 AddHistory(const QVariantMap &record);
    QVariantList QueryHistory(int page = 1, int page_size = 20);
    bool DeleteHistory(qint64 id);
    int CleanupOldData(int keep_days = 90);

private:
    QString db_path_;
};

} // namespace freight::services
