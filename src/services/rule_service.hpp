#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include "db/sqlite_rule_repository.hpp"

namespace freight::services {

class RuleService : public QObject {
    Q_OBJECT

public:
    explicit RuleService(db::SqliteRuleRepository *repo, QObject *parent = nullptr);

    void InitDefaultData();

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

signals:
    void RulesChanged();

private:
    db::SqliteRuleRepository *repo_;
};

} // namespace freight::services
