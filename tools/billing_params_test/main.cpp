#include "core/freight_types.hpp"
#include "core/app_config.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "db/duckdb_manager.hpp"
#include "services/calc_service.hpp"
#include "services/rule_service.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariantMap>
#include <cmath>

using namespace freight;

static bool NEAR(double a, double b, double eps = 0.01) {
    return std::fabs(a - b) <= eps;
}

#define ASSERT_NEAR(a, b, label) \
    do { \
        if (!NEAR((a), (b))) { \
            qCritical().noquote() << QStringLiteral("  ❌ FAIL %1: expected %2 got %3").arg(label).arg(b,0,'f',3).arg(a,0,'f',3); \
            return 1; \
        } else { \
            qInfo().noquote() << QStringLiteral("  ✅ PASS %1: %2").arg(label).arg(a,0,'f',3); \
        } \
    } while(0)

// 直接往 SQLite 里 INSERT zone/tier（repo没有暴露AddZoneGroup等API）
static bool InsZoneGroup(freight::db::SqliteRuleRepository &repo,
                         const QString &tid, const QString &gc, const QString &gn, int so) {
    QSqlQuery q(repo.Database());
    q.prepare("INSERT INTO zone_groups (template_id, group_code, group_name, sort_order) VALUES (?,?,?,?)");
    q.addBindValue(tid); q.addBindValue(gc); q.addBindValue(gn); q.addBindValue(so);
    return q.exec();
}
static bool InsZoneProvince(freight::db::SqliteRuleRepository &repo,
                            const QString &tid, const QString &gc, const QString &p) {
    QSqlQuery q(repo.Database());
    q.prepare("INSERT INTO zone_group_provinces (template_id, group_code, province) VALUES (?,?,?)");
    q.addBindValue(tid); q.addBindValue(gc); q.addBindValue(p);
    return q.exec();
}
static bool InsTier(freight::db::SqliteRuleRepository &repo,
                    const QString &tid, const QString &gc,
                    double first_w, double first_p, double add_unit, double add_p) {
    QSqlQuery q(repo.Database());
    q.prepare("INSERT INTO tiered_pricing (template_id,group_code,tier_code,tier_name,min_weight,max_weight,"
              "first_weight,first_price,additional_unit,additional_price,sort_order) "
              "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(tid); q.addBindValue(gc); q.addBindValue("T1"); q.addBindValue("常规");
    q.addBindValue(0.0); q.addBindValue(9999999.0);
    q.addBindValue(first_w); q.addBindValue(first_p); q.addBindValue(add_unit); q.addBindValue(add_p);
    q.addBindValue(1);
    return q.exec();
}
// 把 SQLite -> DuckDB 表同步：使用官方 DuckDBManager::ReloadRules 保证一致
static bool SyncRulesSqliteToDuckDB(freight::db::SqliteRuleRepository &repo,
                                    const QString &rulesDbPath) {
    auto &dbm = freight::db::DuckDBManager::Instance();
    // 清掉旧缓存 duckdb，保证每次 reload 走最新 schema（含tpl_*三列）
    QString ddbPath = freight::core::AppConfig::Instance().GetCacheDir() + "/calc.duckdb";
    dbm.GetDB(); // force release optional
    QFile::remove(ddbPath);
    QFile::remove(ddbPath + ".wal");
    if (!dbm.Init(ddbPath)) { qCritical() << "re-init duckdb failed"; return false; }
    Q_UNUSED(repo);
    return dbm.ReloadRules(rulesDbPath);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // 临时目录放 data（AppConfig 从系统里读 data dir，我们覆写环境变量 QStandardPaths）。
    // 简单做法：直接改代码调用 AppConfig::Init() 之前先把自定义临时路径挂到环境变量。
    QString tmpDir = QDir::tempPath() + "/xiaoqiao_ut_" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(tmpDir);
    qputenv("HOME", tmpDir.toUtf8());        // Mac: ~/Library -> /tmp/.../Library
    qputenv("XDG_DATA_HOME", tmpDir.toUtf8()); // Linux/XDG fallback

    // 先初始化 AppConfig 确定 data 路径
    freight::core::AppConfig::Instance().Init();

    // 删除旧的 rules.db，确保每次测试从零来
    QString rulesDb = freight::core::AppConfig::Instance().GetRulesDbPath();
    QFile::remove(rulesDb);
    QDir().mkpath(QFileInfo(rulesDb).absolutePath());

    freight::db::SqliteRuleRepository repo(rulesDb);
    if (!repo.Init()) { qCritical() << "repo.Init failed at" << rulesDb; return 1; }

    // 清空 SQLite 所有默认模板/分区/阶梯/客户数据，避免与测试模板 ID 重叠导致 zone1 干扰匹配
    {
        QSqlQuery q(repo.Database());
        auto run = [&](const char *sql){
            if (!q.exec(sql)) {
                qCritical() << "cleanup sql failed:" << sql << q.lastError().text();
                return false;
            }
            return true;
        };
        if (!run("DELETE FROM surcharge_date_ranges;")) return 1;
        if (!run("DELETE FROM surcharge_customers;")) return 1;
        if (!run("DELETE FROM surcharge_provinces;")) return 1;
        if (!run("DELETE FROM surcharge_strategies;")) return 1;
        if (!run("DELETE FROM remote_areas;")) return 1;
        if (!run("DELETE FROM fuel_surcharge;")) return 1;
        if (!run("DELETE FROM customers;")) return 1;
        if (!run("DELETE FROM tiered_pricing;")) return 1;
        if (!run("DELETE FROM zone_group_provinces;")) return 1;
        if (!run("DELETE FROM zone_groups;")) return 1;
        if (!run("DELETE FROM freight_templates;")) return 1;
        // 立刻插入 3 个客户，避免 ValidateIntegrity 认为损坏重灌默认数据（会把zone1塞到所有模板）
        auto ins = [&](const QString &cid, const QString &cname, const QString &tid) {
            q.prepare("INSERT INTO customers (customer_id,customer_name,discount_rate,default_template,contact_person,contact_phone,address)"
                      " VALUES (?,?,?,?,?,?,?)");
            q.addBindValue(cid); q.addBindValue(cname); q.addBindValue(1.0);
            q.addBindValue(tid); q.addBindValue(""); q.addBindValue(""); q.addBindValue("");
            return q.exec();
        };
        if (!ins("default_cust_1","防重灌客户1","")) return 1;
        if (!ins("default_cust_2","防重灌客户2","")) return 1;
        if (!ins("default_cust_3","防重灌客户3","")) return 1;
        // 同时保证 templates 不为空：Integrity还检查templates（不过是"是否为空×是否损坏？"看一下）
        // 如果仍被判定损坏，再在最后手动把 sqlite schema_version 设为已初始化
    }

    // DuckDB 初始化（先删旧 duckdb 保证 schema 最新）
    QString ddbPath = freight::core::AppConfig::Instance().GetCacheDir() + "/calc.duckdb";
    QFile::remove(ddbPath);
    QFile::remove(ddbPath + ".wal");
    QDir().mkpath(QFileInfo(ddbPath).absolutePath());
    auto &dbm = freight::db::DuckDBManager::Instance();
    dbm.Init(ddbPath);

    // 预置4个测试模板 + 1个追加 E 模板（纯 INSERT，避开 repo.AddTemplate 自动塞默认 zone1/tier）
    QSqlQuery s(repo.Database());
    auto insertTpl = [&](const QString &id, const QString &name, const QString &rounding,
                         double add_unit, int vol_div, double first_w, double first_p, int is_def) {
        s.prepare("INSERT INTO freight_templates "
                  "(template_id,template_name,carrier_name,first_weight,additional_unit,"
                  " vol_weight_ratio,default_no_weight_fee,is_default,description,"
                  " tpl_rounding_mode,tpl_additional_unit,tpl_vol_divisor)"
                  " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
        s.addBindValue(id); s.addBindValue(name); s.addBindValue("TEST");
        s.addBindValue(first_w); s.addBindValue(add_unit); s.addBindValue((double)vol_div);
        s.addBindValue(0.0); s.addBindValue(is_def); s.addBindValue("auto test");
        s.addBindValue(rounding); s.addBindValue(add_unit); s.addBindValue(vol_div);
        if (!s.exec()) { qCritical() << "insertTpl fail" << id << s.lastError().text(); return false; }
        return true;
    };
    struct TplCfg {
        QString id, name, rounding;
        double  add_unit; int vol_div;
        double  first_w, first_p, add_p;
    };
    QList<TplCfg> tpls = {
        {"tpl_a", "A-国标0.1进一+1kg续重", "ceil_0_1kg",       1.0, 6000,  1.0, 8.0, 3.0},
        {"tpl_b", "B-1kg进一+0.5kg续重",   "ceil_1kg",         0.5, 5000,  1.0, 10.0, 1.6},
        {"tpl_c", "C-0.5kg进一+0.1kg续重", "ceil_0_5kg",       0.1, 8000,  0.5, 6.0, 0.5},
        {"tpl_d", "D-不进位+1kg续重",      "floor_no_round",   1.0, 12000, 1.0, 5.0, 2.0},
        {"tpl_e", "E-0.1四舍五入",          "round_0_1kg",      1.0, 6000,  1.0, 7.0, 2.5},
    };
    int idx = 0;
    for (const auto &t : tpls) {
        if (!insertTpl(t.id, t.name, t.rounding, t.add_unit, t.vol_div, t.first_w, t.first_p,
                       (idx == 0) ? 1 : 0)) return 1;
        ++idx;
        InsZoneGroup(repo, t.id, "Z1", "一区", 1);
        for (const QString &p : QStringList{"江苏","浙江","上海"})
            InsZoneProvince(repo, t.id, "Z1", p);
        InsTier(repo, t.id, "Z1", t.first_w, t.first_p, t.add_unit, t.add_p);
    }

    // 写入 DuckDB（用官方 ReloadRules，会建完整 schema + 导入所有 SQLite 数据）
    if (!SyncRulesSqliteToDuckDB(repo, rulesDb)) {
        qCritical() << "Sync rules to duckdb failed"; return 1;
    }

    freight::services::CalcService cs;

    // 调试：看 tpl_a 在 DuckDB 里有没有数据（用 dbm.CreateConnection 同一个DB实例）
    {
        auto con = dbm.CreateConnection();
        auto chk = [&](const QString &sql, const QString &tag) {
            try {
                auto r = con.Query(sql.toStdString());
                qInfo() << "  [dbg]" << tag << "rows=" << r->RowCount();
                if (r->RowCount() > 0 && r->ColumnCount() > 0) {
                    for (idx_t i = 0; i < std::min<idx_t>(2, r->RowCount()); ++i) {
                        QStringList row;
                        for (idx_t c = 0; c < r->ColumnCount(); ++c)
                            row << QString::fromStdString(r->GetValue(c, i).ToString());
                        qInfo() << "    " << row.join(" | ");
                    }
                }
            } catch (const std::exception &e) {
                qWarning() << "  [dbg] FAIL" << tag << ":" << e.what();
            }
        };
        chk("SELECT template_id, tpl_rounding_mode, tpl_additional_unit FROM freight_templates WHERE template_id='tpl_a'", "freight_templates[tpl_a]");
        chk("SELECT template_id, group_code, province FROM zone_group_provinces WHERE template_id='tpl_a' AND province LIKE '%江苏%'", "zgp[tpl_a,江苏]");
        chk("SELECT template_id, group_code, first_price, additional_price FROM tiered_pricing WHERE template_id='tpl_a'", "tier[tpl_a]");
        // 模拟 CalcSingle：直接跑 matched_zone + matched_tier
        chk("SELECT zgp.group_code, zg.group_name FROM zone_group_provinces zgp "
            "LEFT JOIN zone_groups zg ON zg.template_id=zgp.template_id AND zg.group_code=zgp.group_code "
            "WHERE zgp.template_id='tpl_a' AND REGEXP_REPLACE(zgp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '江苏' LIMIT 1",
            "matched_zone[tpl_a,江苏]");
        // 查 base_fee：匹配到哪组 first_price
        chk("SELECT tp.* FROM tiered_pricing tp "
            "WHERE tp.template_id='tpl_a' AND tp.group_code = ("
            "  SELECT zgp.group_code FROM zone_group_provinces zgp WHERE zgp.template_id='tpl_a' "
            "   AND REGEXP_REPLACE(zgp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '江苏' LIMIT 1"
            ") AND 1.3 > tp.min_weight AND 1.3 <= tp.max_weight LIMIT 1",
            "matched_tier[tpl_a,江苏,1.3]");
    }

    // ========== 测试1：5种进位模式 ==========
    qInfo() << "\n=========【测试1】5 种续重进位模式（江苏 1.23kg/1.234kg 实重）=========";
    {
        auto rA = cs.CalcSingle("江苏", 1.23, 0, "tpl_a");
        ASSERT_NEAR(rA.charge_weight, 1.3, "A 0.1进一 charge_weight=1.3");
        ASSERT_NEAR(rA.base_fee, 11.0, "A base_fee = 8 + CEIL(0.3/1)*3 = 11");

        auto rB = cs.CalcSingle("江苏", 1.23, 0, "tpl_b");
        ASSERT_NEAR(rB.charge_weight, 2.0, "B 1kg进一 charge_weight=2.0");
        ASSERT_NEAR(rB.base_fee, 13.2, "B base_fee = 10 + CEIL(1/0.5)*1.6 = 13.2");

        auto rC = cs.CalcSingle("江苏", 1.23, 0, "tpl_c");
        ASSERT_NEAR(rC.charge_weight, 1.5, "C 0.5kg进一 charge_weight=1.5");
        ASSERT_NEAR(rC.base_fee, 11.0, "C base_fee = 6 + CEIL(1.0/0.1)*0.5 = 11");

        auto rD = cs.CalcSingle("江苏", 1.23, 0, "tpl_d");
        ASSERT_NEAR(rD.charge_weight, 1.23, "D 不进位 charge_weight=1.23");
        ASSERT_NEAR(rD.base_fee, 7.0, "D base_fee = 5 + CEIL(0.23/1)*2 = 7");

        auto rE = cs.CalcSingle("江苏", 1.234, 0, "tpl_e");
        ASSERT_NEAR(rE.charge_weight, 1.2, "E 0.1四舍五入 charge_weight=1.2");
        ASSERT_NEAR(rE.base_fee, 9.5, "E base_fee = 7 + CEIL(0.2/1)*2.5 = 9.5");
    }

    // ========== 测试2：超首重边界 ==========
    qInfo() << "\n=========【测试2】续重单位影响——超首重边界（模板A 1.0kg vs 1.001kg）=========";
    {
        auto r1 = cs.CalcSingle("江苏", 1.0, 0, "tpl_a");
        ASSERT_NEAR(r1.charge_weight, 1.0, "A 1.0kg");
        ASSERT_NEAR(r1.base_fee, 8.0, "A 1.0kg=首价8");

        auto r2 = cs.CalcSingle("江苏", 1.001, 0, "tpl_a");
        ASSERT_NEAR(r2.charge_weight, 1.1, "A 1.001→1.1(0.1进一)");
        ASSERT_NEAR(r2.base_fee, 11.0, "A 1.001→超首0.1kg=1个续重 → 8+3=11");
    }

    // ========== 测试3：体积重除数影响 ==========
    qInfo() << "\n=========【测试3】体积重除数影响——长40宽30高20cm=========";
    {
        auto rA = cs.CalcSingle("浙江", 1.0, 0, "tpl_a", "", "", 40, 30, 20);
        ASSERT_NEAR(rA.charge_weight, 4.0, "A 40×30×20/6000=4.0kg");
        ASSERT_NEAR(rA.base_fee, 17.0, "A 4.0kg = 8+3×3=17");

        auto rB = cs.CalcSingle("浙江", 1.0, 0, "tpl_b", "", "", 40, 30, 20);
        ASSERT_NEAR(rB.charge_weight, 5.0, "B 4.8vol→1kg进一=5.0");
        ASSERT_NEAR(rB.base_fee, 22.8, "B 5.0kg = 10+8×1.6=22.8");

        auto rD = cs.CalcSingle("浙江", 1.0, 0, "tpl_d", "", "", 40, 30, 20);
        ASSERT_NEAR(rD.charge_weight, 2.0, "D 24000/12000=2.0→不进位=2.0");
        ASSERT_NEAR(rD.base_fee, 7.0, "D 2.0kg = 5+1×2=7");

        // 实重更大不取体积重
        auto rA2 = cs.CalcSingle("浙江", 5.0, 0, "tpl_a", "", "", 40, 30, 20);
        ASSERT_NEAR(rA2.charge_weight, 5.0, "A 实5>vol4→计费=5");
        ASSERT_NEAR(rA2.base_fee, 20.0, "A 5kg = 8+4×3=20");
    }

    // ========== 测试4：标准续重公式 vs 老线性 ==========
    qInfo() << "\n=========【测试4】标准续重公式 vs 老线性公式的差异（模板A 3.2kg）=========";
    {
        auto r = cs.CalcSingle("上海", 3.2, 0, "tpl_a");
        ASSERT_NEAR(r.charge_weight, 3.2, "A 3.2→0.1进一=3.2");
        ASSERT_NEAR(r.base_fee, 17.0, "A 3.2kg 新公式=8+CEIL(2.2)*3=17");
        if (NEAR(r.base_fee, 17.6, 0.001)) {
            qCritical() << "  ❌ FAIL: 仍走老线性公式(17.6)";
            return 1;
        }
        qInfo() << "  ✅ PASS: 已切换标准续重公式（≠老线性17.6）";
    }

    // ========== 测试5：首重以内无续重 ==========
    qInfo() << "\n=========【测试5】首重以内不产生续重（模板C首重0.5kg）=========";
    {
        auto r1 = cs.CalcSingle("上海", 0.3, 0, "tpl_c");
        ASSERT_NEAR(r1.charge_weight, 0.5, "C 0.3→0.5进一=0.5");
        ASSERT_NEAR(r1.base_fee, 6.0, "C 0.3kg=首价6");
        auto r2 = cs.CalcSingle("上海", 0.5, 0, "tpl_c");
        ASSERT_NEAR(r2.base_fee, 6.0, "C 0.5kg=首价6");
    }

    // ========== 测试6：批量 BuildCalcSQL 一致 ==========
    qInfo() << "\n=========【测试6】批量 BuildCalcSQL 与 CalcSingle 一致=========";
    {
        // DuckDB customers 里加 4 个客户，对应 4 个模板
        {
            QSqlQuery q(repo.Database());
            q.exec("DELETE FROM customers");
            auto ins = [&](const QString &cid, const QString &cname, const QString &tid) {
                q.prepare("INSERT INTO customers "
                          "(customer_id,customer_name,discount_rate,default_template,contact_person,contact_phone,address)"
                          " VALUES (?,?,?,?,?,?,?)");
                q.addBindValue(cid); q.addBindValue(cname); q.addBindValue(1.0);
                q.addBindValue(tid); q.addBindValue(""); q.addBindValue(""); q.addBindValue("");
                return q.exec();
            };
            ins("cA","客户A","tpl_a"); ins("cB","客户B","tpl_b");
            ins("cC","客户C","tpl_c"); ins("cD","客户D","tpl_d");
        }
        // 先 Sync 再拿 Connection，保证用的是最新数据&最新连接
        SyncRulesSqliteToDuckDB(repo, rulesDb);
        auto con = dbm.CreateConnection();
        con.Query("DROP TABLE IF EXISTS _ut_input");
        con.Query(R"SQL(
CREATE TABLE _ut_input (
    order_id VARCHAR, dest_province VARCHAR, dest_city VARCHAR,
    weight DOUBLE, vol_weight DOUBLE, customer_id VARCHAR
);
INSERT INTO _ut_input VALUES
    ('A1', '江苏', '', 1.23, 0, 'cA'),
    ('B1', '江苏', '', 1.23, 0, 'cB'),
    ('C1', '江苏', '', 1.23, 0, 'cC'),
    ('D1', '江苏', '', 1.23, 0, 'cD'),
    ('A32','上海','', 3.2,  0, 'cA');
)SQL");

        if (!cs.CalcBatch("_ut_input", "_ut_output")) { qCritical() << "CalcBatch fail"; return 1; }

        auto res = con.Query("SELECT \"订单号\", \"计费重量(KG)\", \"基础运费\" FROM _ut_output ORDER BY \"订单号\"");
        QMap<QString,std::pair<double,double>> rows;
        for (size_t i = 0; i < res->RowCount(); ++i) {
            QString oid = QString::fromStdString(res->GetValue(0,i).ToString());
            rows[oid] = { res->GetValue(1,i).GetValue<double>(), res->GetValue(2,i).GetValue<double>() };
        }
        ASSERT_NEAR(rows["A1"].first, 1.3,    "batch A1 cw=1.3");
        ASSERT_NEAR(rows["A1"].second, 11.0,  "batch A1 bf=11");
        ASSERT_NEAR(rows["B1"].first, 2.0,    "batch B1 cw=2.0");
        ASSERT_NEAR(rows["B1"].second, 13.2,  "batch B1 bf=13.2");
        ASSERT_NEAR(rows["C1"].first, 1.5,    "batch C1 cw=1.5");
        ASSERT_NEAR(rows["C1"].second, 11.0,  "batch C1 bf=11");
        ASSERT_NEAR(rows["D1"].first, 1.23,   "batch D1 cw=1.23");
        ASSERT_NEAR(rows["D1"].second, 7.0,   "batch D1 bf=7");
        ASSERT_NEAR(rows["A32"].first, 3.2,   "batch A32 cw=3.2");
        ASSERT_NEAR(rows["A32"].second, 17.0, "batch A32 bf=17");
    }

    qInfo() << "\n🎉 全部 6 类 共 30 个断言全部通过！✅\n";
    // cleanup
    QDir(tmpDir).removeRecursively();
    return 0;
}
