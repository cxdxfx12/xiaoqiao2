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
