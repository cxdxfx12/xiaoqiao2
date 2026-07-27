#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include "core/freight_types.hpp"

namespace freight::db {

class SqliteRuleRepository {
public:
    explicit SqliteRuleRepository(const QString &db_path);
    ~SqliteRuleRepository();

    bool Init();
    bool IsFirstRun();

    bool CreateDefaultTemplate();
    bool CreateDefaultSurcharges();
    bool CreateDefaultCustomers();
    bool ValidateIntegrity();

    QVariantList ListTemplates();
    QVariantMap GetTemplate(const QString &template_id);
    bool AddTemplate(const QVariantMap &tpl);
    bool UpdateTemplate(const QVariantMap &tpl);
    bool DeleteTemplate(const QString &template_id);

    QVariantList ListSurchargeStrategies(const QString &scope = QString(),
                                         bool only_active = false);
    QVariantMap GetSurchargeStrategy(const QString &strategy_id);
    bool AddSurchargeStrategy(const QVariantMap &strategy);
    bool UpdateSurchargeStrategy(const QVariantMap &strategy);
    bool DeleteSurchargeStrategy(const QString &strategy_id);
    bool SetSurchargeActive(const QString &strategy_id, bool active);

    QStringList GetSurchargeProvinces(const QString &strategy_id);
    bool SetSurchargeProvinces(const QString &strategy_id, const QStringList &provinces);

    QVariantList ListCustomers();
    QVariantMap GetCustomer(const QString &customer_id);
    bool AddCustomer(const QVariantMap &cust);
    bool UpdateCustomer(const QVariantMap &cust);
    bool DeleteCustomer(const QString &customer_id);

    QVariantList ListFuelSurcharges(const QString &template_id);
    bool AddFuelSurcharge(const QVariantMap &fuel);
    bool UpdateFuelSurcharge(int id, const QVariantMap &fuel);
    bool DeleteFuelSurcharge(int id);
    bool SetFuelSurchargeActive(int id, bool active);

    QVariantList ListRemoteAreas(const QString &template_id);
    bool AddRemoteArea(const QVariantMap &area);
    bool UpdateRemoteArea(int id, const QVariantMap &area);
    bool DeleteRemoteArea(int id);
    bool SetRemoteAreaActive(int id, bool active);

    QVariantList ListAvgWeightTemplates();
    QVariantMap GetAvgWeightTemplate(const QString &avg_tpl_id);
    bool SaveAvgWeightTemplate(const QVariantMap &tpl);
    bool DeleteAvgWeightTemplate(const QString &avg_tpl_id);
    bool SetAvgWeightTemplateActive(const QString &avg_tpl_id, bool active);

    // 方案A（复用模板分区）：勾选的模板分区列表
    //   返回每项 QVariantMap: { template_id, group_code }
    QVariantList GetAvgWeightTplGroups(const QString &avg_tpl_id);
    //   groups: QVariantList，每项含 template_id + group_code
    //   out_err: 若函数返回 false，写入具体的 QSqlQuery 级 SQLite 错误文本
    bool SetAvgWeightTplGroups(const QString &avg_tpl_id, const QVariantList &groups,
                               QString *out_err = nullptr);

    // 方案A（复用模板分区）：勾选分区里的排除省
    //   返回每项 QVariantMap: { template_id, group_code, province }
    QVariantList GetAvgWeightExcludes(const QString &avg_tpl_id);
    //   out_err: 若函数返回 false，写入具体的 QSqlQuery 级 SQLite 错误文本
    bool SetAvgWeightExcludes(const QString &avg_tpl_id, const QVariantList &excludes,
                              QString *out_err = nullptr);

    // 方案B（自定义拉均重专属省份）：按 zone_code → 省份列表 读写
    //   返回 QVariantList：每项 { zone_code, zone_name(可选), provinces:[省1,省2,...] }
    QVariantList GetAvgWeightZones(const QString &avg_tpl_id);
    //   zones: 每项含 zone_code + provinces (QStringList) + zone_name(可选)
    //   out_err: 若函数返回 false，写入具体的 QSqlQuery 级 SQLite 错误文本
    bool SetAvgWeightZones(const QString &avg_tpl_id, const QVariantList &zones,
                           QString *out_err = nullptr);

    // 辅助：一次返回所有已启用模板 + 各自 zone_groups 信息 + 组内省份数量
    //   每项 QVariantMap: { template_id, template_name, carrier_name, is_active(bool),
    //                        groups:[ {group_code, group_name, sort_order, province_count,
    //                                  provinces:[省1,省2,...]} ] }
    QVariantList ListCourierTemplatesWithZones();

    // 供对话框直接查询关联表
    QSqlDatabase &Database() { return db_; }

private:
    QString db_path_;
    QString conn_name_;
    QSqlDatabase db_;

    bool CreateTables();
    void InitDefaultData();
};

} // namespace freight::db
