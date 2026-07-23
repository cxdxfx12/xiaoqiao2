#include <QApplication>
#include <QIcon>
#include <QString>
#include <QDebug>
#include "core/app_config.hpp"
#include "core/license_manager.hpp"
#include "db/duckdb_manager.hpp"
#include "services/rule_service.hpp"
#include "ui/main_window.hpp"
#include "ui/icon_manager.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("小乔运费结算");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("杭州喵喵至家网络有限公司");
    app.setOrganizationDomain("hbdxm.com");

    qDebug() << "=== 小乔运费结算 v1.0.0 ===";
    qDebug() << "杭州喵喵至家网络有限公司";

    if (!freight::core::AppConfig::Instance().Init()) {
        qCritical() << "配置初始化失败";
        return 1;
    }

    if (!freight::db::DuckDBManager::Instance().Init(
        freight::core::AppConfig::Instance().GetDataDir() + "/calc.duckdb")) {
        qCritical() << "DuckDB初始化失败";
        return 1;
    }

    freight::db::SqliteRuleRepository rule_repo(
        freight::core::AppConfig::Instance().GetDataDir() + "/rules.db");
    if (!rule_repo.Init()) {
        qCritical() << "规则库初始化失败";
        return 1;
    }

    freight::services::RuleService rule_service(&rule_repo);
    rule_service.InitDefaultData();

    if (!freight::db::DuckDBManager::Instance().LoadRulesFromSQLite(
        freight::core::AppConfig::Instance().GetDataDir() + "/rules.db")) {
        qCritical() << "规则加载失败";
        return 1;
    }

    freight::ui::IconManager &icons = freight::ui::IconManager::Instance();
    app.setWindowIcon(icons.GetIcon("app_logo", freight::ui::IconCategory::LOGO,
                      freight::ui::IconSize::SIZE_64));

    // 初始化授权系统
    freight::core::LicenseManager::Instance().Init();
    auto lic_info = freight::core::LicenseManager::Instance().GetLicenseInfo();
    qDebug() << "授权状态:" << lic_info.TypeString()
             << "剩余天数:" << lic_info.DaysRemaining();

    freight::ui::MainWindow w;
    w.show();

    return app.exec();
}
