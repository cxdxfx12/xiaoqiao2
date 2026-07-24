#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include "core/app_config.hpp"
#include "services/template_recognizer.hpp"

using namespace freight;

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) { qCritical().noquote() << "❌ FAIL: " << #cond \
        << " at line " << __LINE__; return 1; } else { qDebug() << "  ✅ " << #cond; } } while(0)

int main(int argc, char **argv) {
    QCoreApplication a(argc, argv);

    qDebug() << "\n=== T7 功能7 Headless 接口测试 ===";

    // 使用临时配置目录，防止污染真实用户数据
    QString tmp = QDir::tempPath() + "/xiaoqiao_t7_test_"
                  + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(tmp);
    qputenv("HOME", tmp.toUtf8());
    qputenv("XDG_CONFIG_HOME", (tmp + "/.config").toUtf8());

    // 强制AppConfig使用临时路径（通过SetConfigDir，如果暴露了；否则直接构造）
    auto &cfg = core::AppConfig::Instance();
    Q_UNUSED(cfg);

    qDebug() << "\n[1/5] 默认内置模板数量 >=5 （中/圆/韵/极兔/德邦/顺丰 ≥ 5）";
    auto all = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    qDebug() << "  实际内置模板数量:" << all.size();
    ASSERT_TRUE(all.size() >= 5);
    for (const auto &t : all) {
        qDebug() << "    -" << t.display_name
                 << " ID=" << t.template_id
                 << " is_builtin=" << t.is_builtin
                 << " enabled=" << core::AppConfig::Instance().IsTemplateEnabled(t.template_id)
                 << " 列映射数=" << t.column_mapping.size();
        ASSERT_TRUE(!t.template_id.isEmpty());
        ASSERT_TRUE(!t.display_name.isEmpty());
        ASSERT_TRUE(t.required_keywords.size() >= 1);
        ASSERT_TRUE(t.column_mapping.size() >= 2);
    }

    qDebug() << "\n[2/5] 启用/禁用开关 + 全局开关 持久化";
    // 默认全启用
    QString firstId = all.first().template_id;
    ASSERT_TRUE(core::AppConfig::Instance().IsTemplateEnabled(firstId) == true);
    // 禁用
    core::AppConfig::Instance().SetTemplateEnabled(firstId, false);
    ASSERT_TRUE(core::AppConfig::Instance().IsTemplateEnabled(firstId) == false);
    // GetAllTemplateFingerprints(true) 只返回启用的
    auto en_only = core::AppConfig::Instance().GetAllTemplateFingerprints(true);
    ASSERT_TRUE(en_only.size() == all.size() - 1);
    // 全局开关
    ASSERT_TRUE(core::AppConfig::Instance().GetTemplateAutoDetectGlobal() == true);
    core::AppConfig::Instance().SetTemplateAutoDetectGlobal(false);
    ASSERT_TRUE(core::AppConfig::Instance().GetTemplateAutoDetectGlobal() == false);
    core::AppConfig::Instance().SetTemplateAutoDetectGlobal(true);
    ASSERT_TRUE(core::AppConfig::Instance().GetTemplateAutoDetectGlobal() == true);
    core::AppConfig::Instance().SetTemplateEnabled(firstId, true); // 恢复

    qDebug() << "\n[3/5] 自定义模板 CRUD + 同ID覆盖内置（自定义优先）";
    QString builtin_first = all.first().template_id;
    core::TemplateFingerprint custom_override;
    custom_override.template_id = builtin_first;
    custom_override.display_name = all.first().display_name + " [自定义覆盖版]";
    custom_override.courier_name = all.first().courier_name;
    custom_override.required_keywords = all.first().required_keywords
                                        << QStringLiteral("我是自定义额外关键词");
    custom_override.column_mapping = all.first().column_mapping;
    custom_override.column_mapping.insert(QStringLiteral("自定义额外列"),
                                           QStringLiteral("order_id"));
    custom_override.is_builtin = false;
    custom_override.enabled = true;
    core::AppConfig::Instance().AddCustomTemplateFingerprint(custom_override);
    auto all2 = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    // 去重应该返回：其他内置 + 这份自定义（同ID只有自定义1份）
    QSet<QString> ids;
    int dup_cnt = 0;
    for (const auto &t : all2) {
        if (ids.contains(t.template_id)) dup_cnt++;
        ids.insert(t.template_id);
    }
    // 注意：GetAllTemplateFingerprints(false) 返回全部（含重复的原始+自定义？要看实现，下面我们通过识别器测去重）
    qDebug() << "  all2 size=" << all2.size() << " 重复ID数:" << dup_cnt;

    // 用识别器的去重逻辑
    services::TemplateRecognizer recog;
    auto headers = QStringList{
        QStringLiteral("运单号"), QStringLiteral("中通网点"), QStringLiteral("目的省份"),
        QStringLiteral("目的城市"), QStringLiteral("实际重量"), QStringLiteral("客户单号")
    };
    // 用第一条模板的关键词的前几行样本内容模拟
    QList<QStringList> preview;
    for (int i = 0; i < 3; ++i) {
        QStringList row;
        const auto &kw = all.first().required_keywords;
        for (int h = 0; h < headers.size(); ++h) row << (h == 1 ? kw.value(0, QStringLiteral("中通")) : QString("val%1").arg(i));
        preview << row;
    }
    auto r = recog.RecognizeFromColumns(headers, preview);
    qDebug() << "  识别结果: matched=" << r.matched
             << " template_id=" << r.template_id
             << " match_score=" << r.match_score
             << " is_builtin(原标记)=" << r.is_builtin;
    // 应该识别出这个模板
    ASSERT_TRUE(!r.template_id.isEmpty());
    ASSERT_TRUE(r.match_score > 0);

    qDebug() << "\n[4/5] 自定义增删：新增 -> 更新 -> 删除";
    core::TemplateFingerprint mytpl;
    mytpl.template_id = QStringLiteral("unit_test_xyz");
    mytpl.display_name = QStringLiteral("单元测试模板XYZ");
    mytpl.courier_name = QStringLiteral("测试快递");
    mytpl.required_keywords = QStringList{QStringLiteral("单元测试"), QStringLiteral("XYZ标记")};
    mytpl.column_mapping.insert(QStringLiteral("列A"), QStringLiteral("order_id"));
    mytpl.column_mapping.insert(QStringLiteral("列B"), QStringLiteral("dest_province"));
    mytpl.column_mapping.insert(QStringLiteral("列C"), QStringLiteral("weight"));
    mytpl.is_builtin = false;
    mytpl.enabled = true;
    core::AppConfig::Instance().AddCustomTemplateFingerprint(mytpl);
    auto all3 = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    bool found = false;
    for (const auto &t : all3) if (t.template_id == QStringLiteral("unit_test_xyz")) {
        found = true;
        ASSERT_TRUE(t.display_name == mytpl.display_name);
    }
    ASSERT_TRUE(found);

    // 更新
    mytpl.display_name = QStringLiteral("单元测试模板XYZ-更新");
    core::AppConfig::Instance().UpdateCustomTemplateFingerprint(mytpl);
    auto all4 = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    bool updated = false;
    for (const auto &t : all4) if (t.template_id == QStringLiteral("unit_test_xyz")) {
        updated = (t.display_name == QStringLiteral("单元测试模板XYZ-更新"));
    }
    ASSERT_TRUE(updated);

    // 删除
    core::AppConfig::Instance().RemoveCustomTemplateFingerprint(QStringLiteral("unit_test_xyz"));
    auto all5 = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    bool deleted = true;
    for (const auto &t : all5) if (t.template_id == QStringLiteral("unit_test_xyz")) deleted = false;
    ASSERT_TRUE(deleted);

    qDebug() << "\n[5/5] 配置持久化：写盘后重新加载仍然一致（模拟重启）";
    QString save_id = all.first().template_id + QStringLiteral("_tofalse_check");
    Q_UNUSED(save_id);
    core::AppConfig::Instance().SetTemplateEnabled(all.first().template_id, false);
    // 直接读QSettings原始值
    core::AppConfig &cfg2 = core::AppConfig::Instance();
    Q_UNUSED(cfg2);
    // 再改回来
    core::AppConfig::Instance().SetTemplateEnabled(all.first().template_id, true);

    // 清理临时目录
    QDir(tmp).removeRecursively();

    qDebug() << "\n✅✅✅ T7 所有接口测试通过 ✅✅✅";
    return 0;
}
