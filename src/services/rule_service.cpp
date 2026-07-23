#include "services/rule_service.hpp"

namespace freight::services {

RuleService::RuleService(db::SqliteRuleRepository *repo, QObject *parent)
    : QObject(parent), repo_(repo) {}

void RuleService::InitDefaultData() {
    // 默认数据由 SqliteRuleRepository::UpgradeSchema() 内部按需初始化，无需外部显式操作。
    // 保留此空方法以兼容接口。
}

QVariantList RuleService::ListTemplates() {
    return repo_->ListTemplates();
}

QVariantMap RuleService::GetTemplate(const QString &template_id) {
    return repo_->GetTemplate(template_id);
}

bool RuleService::AddTemplate(const QVariantMap &tpl) {
    bool ok = repo_->AddTemplate(tpl);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::UpdateTemplate(const QVariantMap &tpl) {
    bool ok = repo_->UpdateTemplate(tpl);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::DeleteTemplate(const QString &template_id) {
    bool ok = repo_->DeleteTemplate(template_id);
    if (ok) emit RulesChanged();
    return ok;
}

QVariantList RuleService::ListSurchargeStrategies(const QString &scope, bool only_active) {
    return repo_->ListSurchargeStrategies(scope, only_active);
}

QVariantMap RuleService::GetSurchargeStrategy(const QString &strategy_id) {
    return repo_->GetSurchargeStrategy(strategy_id);
}

bool RuleService::AddSurchargeStrategy(const QVariantMap &strategy) {
    bool ok = repo_->AddSurchargeStrategy(strategy);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::UpdateSurchargeStrategy(const QVariantMap &strategy) {
    bool ok = repo_->UpdateSurchargeStrategy(strategy);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::DeleteSurchargeStrategy(const QString &strategy_id) {
    bool ok = repo_->DeleteSurchargeStrategy(strategy_id);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::SetSurchargeActive(const QString &strategy_id, bool active) {
    bool ok = repo_->SetSurchargeActive(strategy_id, active);
    if (ok) emit RulesChanged();
    return ok;
}

QStringList RuleService::GetSurchargeProvinces(const QString &strategy_id) {
    return repo_->GetSurchargeProvinces(strategy_id);
}

bool RuleService::SetSurchargeProvinces(const QString &strategy_id, const QStringList &provinces) {
    bool ok = repo_->SetSurchargeProvinces(strategy_id, provinces);
    if (ok) emit RulesChanged();
    return ok;
}

QVariantList RuleService::ListCustomers() {
    return repo_->ListCustomers();
}

QVariantMap RuleService::GetCustomer(const QString &customer_id) {
    return repo_->GetCustomer(customer_id);
}

bool RuleService::AddCustomer(const QVariantMap &cust) {
    bool ok = repo_->AddCustomer(cust);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::UpdateCustomer(const QVariantMap &cust) {
    bool ok = repo_->UpdateCustomer(cust);
    if (ok) emit RulesChanged();
    return ok;
}

bool RuleService::DeleteCustomer(const QString &customer_id) {
    bool ok = repo_->DeleteCustomer(customer_id);
    if (ok) emit RulesChanged();
    return ok;
}

} // namespace freight::services
