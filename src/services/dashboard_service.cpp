#include "services/dashboard_service.hpp"
#include "core/app_config.hpp"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QUuid>
#include <QSettings>
#include <QDir>
#include <QDebug>

namespace freight::services {

DashboardService::DashboardService(const QString &history_db_path, QObject *parent)
    : QObject(parent), db_path_(history_db_path) {

    conn_name_ = "DashboardSvc_" + QUuid::createUuid().toString(QUuid::WithoutBraces).mid(0, 8);
}

DashboardService::~DashboardService() {
    if (QSqlDatabase::contains(conn_name_)) {
        QSqlDatabase::removeDatabase(conn_name_);
    }
}

bool DashboardService::Init() {
    auto db = QSqlDatabase::addDatabase("QSQLITE", conn_name_);
    db.setDatabaseName(db_path_);
    if (!db.open()) return false;

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS dashboard_targets ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, "
           "period TEXT UNIQUE, "
           "target REAL, "
           "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");

    QSettings s(QDir(core::AppConfig::Instance().GetDataDir()).filePath("config.ini"),
                QSettings::IniFormat);
    monthly_target_ = s.value("dashboard/monthly_target", 100000.0).toDouble();
    return true;
}

bool DashboardService::SetMonthlyTarget(double target) {
    monthly_target_ = target;
    QSettings s(QDir(core::AppConfig::Instance().GetDataDir()).filePath("config.ini"),
                QSettings::IniFormat);
    s.setValue("dashboard/monthly_target", target);
    s.sync();
    return true;
}
double DashboardService::GetMonthlyTarget() const { return monthly_target_; }

static QVariant SafeV(const QSqlQuery &q, const QString &col) {
    int idx = q.record().indexOf(col);
    return idx < 0 ? QVariant() : q.value(idx);
}

DashboardSummary DashboardService::GetSummary(const QDate &from, const QDate &to) {
    DashboardSummary out;
    if (!QSqlDatabase::contains(conn_name_)) return out;
    auto db = QSqlDatabase::database(conn_name_);
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    QString sql = "SELECT COUNT(*) AS tasks, "
                  "COALESCE(SUM(total_rows),0) AS orders, "
                  "COALESCE(SUM(total_fee),0) AS revenue "
                  "FROM calc_history WHERE status=1 "
                  "AND date(created_at) BETWEEN date(:f) AND date(:t)";
    q.prepare(sql);
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    if (q.exec() && q.next()) {
        out.total_tasks = SafeV(q, "tasks").toLongLong();
        out.total_orders = SafeV(q, "orders").toLongLong();
        out.total_revenue = SafeV(q, "revenue").toDouble();
    }

    if (out.total_orders > 0) {
        out.avg_freight_per_order = out.total_revenue / out.total_orders;
    }

    int days = from.daysTo(to) + 1;
    out.total_processing_days = days;

    QDate prev_to = from.addDays(-1);
    QDate prev_from = from.addDays(-days);
    QSqlQuery pq(db);
    pq.prepare("SELECT COALESCE(SUM(total_fee),0) AS pr "
               "FROM calc_history WHERE status=1 "
               "AND date(created_at) BETWEEN date(:f) AND date(:t)");
    pq.bindValue(":f", prev_from.toString(Qt::ISODate));
    pq.bindValue(":t", prev_to.toString(Qt::ISODate));
    if (pq.exec() && pq.next()) {
        double prev = SafeV(pq, "pr").toDouble();
        if (prev > 0) out.growth_rate = (out.total_revenue - prev) / prev * 100.0;
    }

    out.target = monthly_target_;
    if (monthly_target_ > 0) out.target_progress = out.total_revenue / monthly_target_ * 100.0;
    return out;
}

QList<ProvinceStat> DashboardService::GetTopProvinces(const QDate &from, const QDate &to, int limit) {
    QList<ProvinceStat> out;
    if (!QSqlDatabase::contains(conn_name_)) return out;
    auto db = QSqlDatabase::database(conn_name_);
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare("SELECT input_file, total_rows, total_fee "
              "FROM calc_history WHERE status=1 "
              "AND date(created_at) BETWEEN date(:f) AND date(:t)");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));

    QMap<QString, ProvinceStat> map;
    double total_rev = 0;

    auto addProv = [&](const QString &name, double rev, qint64 ords) {
        if (name.isEmpty()) return;
        auto &p = map[name];
        p.province = name;
        p.revenue += rev;
        p.orders += ords;
        total_rev += rev;
    };

    while (q.next()) {
        QString infile = SafeV(q, "input_file").toString().toLower();
        double rev = SafeV(q, "total_fee").toDouble();
        qint64 ords = SafeV(q, "total_rows").toLongLong();

        QStringList hit;
        static QStringList provs = {"广东","浙江","江苏","上海","北京","山东","福建","四川","河南","湖北",
            "湖南","安徽","河北","陕西","江西","辽宁","重庆","广西","云南","山西","贵州","黑龙江",
            "吉林","新疆","甘肃","内蒙古","海南","宁夏","青海","西藏","天津"};
        for (const auto &p : provs) {
            if (infile.contains(p)) { hit << p; break; }
        }
        if (hit.isEmpty()) {
            addProv("其他", rev, ords);
        } else {
            for (const auto &p : hit) addProv(p, rev / hit.size(), ords / hit.size());
        }
    }

    for (auto it = map.begin(); it != map.end(); ++it) {
        it.value().pct = total_rev > 0 ? it.value().revenue / total_rev * 100.0 : 0.0;
        if (it.value().orders > 0)
            it.value().avg_freight = it.value().revenue / it.value().orders;
        out << it.value();
    }
    std::sort(out.begin(), out.end(), [](const ProvinceStat &a, const ProvinceStat &b){ return a.revenue > b.revenue; });
    if (out.size() > limit) {
        ProvinceStat rest;
        rest.province = "其他";
        for (int i = limit - 1; i < out.size(); ++i) {
            rest.revenue += out[i].revenue;
            rest.orders += out[i].orders;
        }
        rest.pct = total_rev > 0 ? rest.revenue / total_rev * 100 : 0;
        rest.avg_freight = rest.orders > 0 ? rest.revenue / rest.orders : 0;
        out = out.mid(0, limit - 1);
        out << rest;
    }
    return out;
}

QList<CustomerStat> DashboardService::GetTopCustomers(const QDate &from, const QDate &to, int limit) {
    QList<CustomerStat> out;
    if (!QSqlDatabase::contains(conn_name_)) return out;
    auto db = QSqlDatabase::database(conn_name_);
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare("SELECT input_file, total_rows, total_fee "
              "FROM calc_history WHERE status=1 "
              "AND date(created_at) BETWEEN date(:f) AND date(:t)");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));

    QMap<QString, CustomerStat> map;
    double total_rev = 0;
    int idx = 0;
    while (q.next()) {
        QString infile = SafeV(q, "input_file").toString();
        QFileInfo fi(infile);
        QString name = fi.baseName().split("_").first();
        if (name.isEmpty()) name = QString("客户%1").arg(++idx);
        double rev = SafeV(q, "total_fee").toDouble();
        qint64 ords = SafeV(q, "total_rows").toLongLong();
        auto &c = map[name];
        c.customer_id = name;
        c.revenue += rev;
        c.orders += ords;
        total_rev += rev;
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        it.value().pct = total_rev > 0 ? it.value().revenue / total_rev * 100 : 0;
        it.value().avg_freight = it.value().orders > 0 ? it.value().revenue / it.value().orders : 0;
        out << it.value();
    }
    std::sort(out.begin(), out.end(), [](const CustomerStat &a, const CustomerStat &b){ return a.revenue > b.revenue; });
    if (out.size() > limit) out = out.mid(0, limit);
    return out;
}

QList<RouteProfitStat> DashboardService::GetTopProfitRoutes(const QDate &from, const QDate &to, int limit) {
    QList<RouteProfitStat> out;
    auto provinces = GetTopProvinces(from, to, limit);
    for (const auto &p : provinces) {
        RouteProfitStat r;
        r.route = "至" + p.province;
        r.revenue = p.revenue;
        r.orders = p.orders;
        r.cost_estimate = p.revenue * 0.65;
        r.profit = r.revenue - r.cost_estimate;
        r.profit_rate = r.revenue > 0 ? r.profit / r.revenue * 100 : 0;
        out << r;
    }
    std::sort(out.begin(), out.end(), [](const RouteProfitStat &a, const RouteProfitStat &b){ return a.profit > b.profit; });
    return out;
}

QList<DailyTrendPoint> DashboardService::GetDailyTrend(const QDate &from, const QDate &to) {
    QList<DailyTrendPoint> out;
    if (!QSqlDatabase::contains(conn_name_)) return out;
    auto db = QSqlDatabase::database(conn_name_);
    if (!db.isOpen()) return out;

    QMap<QDate, DailyTrendPoint> dayMap;
    for (QDate d = from; d <= to; d = d.addDays(1)) {
        dayMap[d].date = d;
    }

    QSqlQuery q(db);
    q.prepare("SELECT date(created_at) AS d, "
              "COALESCE(SUM(total_rows),0) AS o, "
              "COALESCE(SUM(total_fee),0) AS r "
              "FROM calc_history WHERE status=1 "
              "AND date(created_at) BETWEEN date(:f) AND date(:t) "
              "GROUP BY date(created_at) ORDER BY d");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));
    while (q.next()) {
        QDate d = QDate::fromString(SafeV(q, "d").toString(), Qt::ISODate);
        if (!d.isValid()) continue;
        auto &pt = dayMap[d];
        pt.date = d;
        pt.orders = SafeV(q, "o").toLongLong();
        pt.revenue = SafeV(q, "r").toDouble();
        pt.avg_freight = pt.orders > 0 ? pt.revenue / pt.orders : 0;
    }
    for (auto it = dayMap.begin(); it != dayMap.end(); ++it) out << it.value();
    return out;
}

QMap<QString, double> DashboardService::GetCourierMix(const QDate &from, const QDate &to) {
    QMap<QString, double> out;
    if (!QSqlDatabase::contains(conn_name_)) return out;
    auto db = QSqlDatabase::database(conn_name_);
    if (!db.isOpen()) return out;

    QSqlQuery q(db);
    q.prepare("SELECT input_file, total_fee FROM calc_history WHERE status=1 "
              "AND date(created_at) BETWEEN date(:f) AND date(:t)");
    q.bindValue(":f", from.toString(Qt::ISODate));
    q.bindValue(":t", to.toString(Qt::ISODate));

    static QList<QPair<QString, QStringList>> rules = {
        {"中通", {"zto","中通"}}, {"圆通", {"yto","圆通"}}, {"韵达", {"yunda","韵达"}},
        {"申通", {"sto","申通"}}, {"极兔", {"jitu","jt","极兔"}},
        {"邮政", {"ems","邮政"}}, {"顺丰", {"sf","顺丰"}}, {"德邦", {"deppon","德邦"}}
    };

    double total = 0;
    while (q.next()) {
        QString f = SafeV(q, "input_file").toString().toLower();
        double v = SafeV(q, "total_fee").toDouble();
        total += v;
        bool hit = false;
        for (const auto &r : rules) {
            for (const auto &k : r.second) {
                if (f.contains(k)) {
                    out[r.first] += v;
                    hit = true;
                    break;
                }
            }
            if (hit) break;
        }
        if (!hit) out["其他"] += v;
    }
    if (total > 0) {
        for (auto it = out.begin(); it != out.end(); ++it) {
            it.value() = it.value() / total * 100.0;
        }
    }
    return out;
}

QMap<QString, double> DashboardService::GetFeeBreakdown(const QDate &from, const QDate &to) {
    QMap<QString, double> out;
    auto s = GetSummary(from, to);
    double total = qMax(1.0, s.total_revenue);
    out["基础运费"] = total * 0.75;
    out["燃油附加费"] = total * 0.10;
    out["偏远地区加价"] = total * 0.08;
    out["其他附加费"] = total * 0.07;
    return out;
}

} // namespace freight::services
