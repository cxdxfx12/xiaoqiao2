#pragma once
#include <QObject>
#include <QDate>
#include <QString>
#include <QMap>
#include <QList>
#include <QVariant>

namespace freight::services {

struct DashboardSummary {
    double total_revenue = 0.0;
    qint64 total_orders = 0;
    qint64 total_tasks = 0;
    double avg_freight_per_order = 0.0;
    double total_processing_days = 0;
    double growth_rate = 0.0;
    double target = 100000.0;
    double target_progress = 0.0;
};

struct ProvinceStat {
    QString province;
    double revenue;
    qint64 orders;
    double avg_freight;
    double pct;
};

struct CustomerStat {
    QString customer_id;
    double revenue;
    qint64 orders;
    double avg_freight;
    double pct;
};

struct RouteProfitStat {
    QString route;
    double revenue;
    double cost_estimate;
    double profit;
    double profit_rate;
    qint64 orders;
};

struct DailyTrendPoint {
    QDate date;
    double revenue;
    qint64 orders;
    double avg_freight;
};

class DashboardService : public QObject {
    Q_OBJECT
public:
    explicit DashboardService(const QString &history_db_path, QObject *parent = nullptr);
    ~DashboardService() override;

    bool Init();

    DashboardSummary GetSummary(const QDate &from, const QDate &to);
    QList<ProvinceStat> GetTopProvinces(const QDate &from, const QDate &to, int limit = 8);
    QList<CustomerStat> GetTopCustomers(const QDate &from, const QDate &to, int limit = 5);
    QList<RouteProfitStat> GetTopProfitRoutes(const QDate &from, const QDate &to, int limit = 5);
    QList<DailyTrendPoint> GetDailyTrend(const QDate &from, const QDate &to);
    QMap<QString, double> GetCourierMix(const QDate &from, const QDate &to);
    QMap<QString, double> GetFeeBreakdown(const QDate &from, const QDate &to);

    bool SetMonthlyTarget(double target);
    double GetMonthlyTarget() const;

private:
    QString db_path_;
    QString conn_name_;
    double monthly_target_ = 100000.0;
};

} // namespace freight::services
