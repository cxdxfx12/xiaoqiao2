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
#include <QTextStream>
#include <QTemporaryDir>
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

    // ========== 测试7：启用拉均重 vs 禁用拉均重 —— 端到端门控 + 进池 + 封顶价 ==========
    qInfo() << "\n=========【测试7】enable_avg_weight=TRUE 启用拉均重端到端验证=========";
    {
        // 7.1 在 customers 的 客户A/cA 上挂 avg_weight_tpl_id，再写一份合同到 SQLite
        {
            QSqlQuery q(repo.Database());
            // 给客户A挂合同号（测试"客户级优先"绑定）
            q.prepare("UPDATE customers SET avg_weight_tpl_id='POLAIYA_LAJZ_001' WHERE customer_id='cA'");
            if (!q.exec()) { qCritical() << "upd cust fail:" << q.lastError().text(); return 1; }
            // 写一份 拉均重模板 tpl_a（客户级绑定 + 模板级 fallback 覆盖）
            q.prepare("INSERT OR REPLACE INTO avg_weight_templates"
                      "(avg_tpl_id,template_id,name,contract_no,avg_pool_max_kg,base_avg_kg,"
                      " avg_fee_cap_kg,base_fee,step_kg,step_fee,min_tickets,"
                      " reuse_zone_groups,over_cap_mode,is_active,version,effective_from,effective_to)"
                      " VALUES"
                      "('POLAIYA_LAJZ_001','tpl_a','珀莱-拉均重-测试合同','LAJZ-PLY-2026', 2.5, 0.3,"
                      " 1.5, 3.0, 0.1, 0.15, 5,"   // 只要 5 票就够门槛，基础价 3 元，每 0.1kg 加 0.15，封顶 1.5kg
                      " 0, 0, 1, 1, '2025-01-01', '2099-12-31')");
            if (!q.exec()) { qCritical() << "ins lajztpl fail:" << q.lastError().text(); return 1; }
            // reuse_zone_groups=0：用 avg_weight_zones 直接配省白名单（江苏 + 浙江 + 上海）
            auto insZone = [&](const QString &prov) {
                QSqlQuery iq(repo.Database());
                iq.prepare("INSERT INTO avg_weight_zones (avg_tpl_id, zone_code, province) VALUES (?,?,?)");
                iq.addBindValue("POLAIYA_LAJZ_001"); iq.addBindValue("华东区"); iq.addBindValue(prov);
                if (!iq.exec()) { qCritical() << "ins zone fail:" << iq.lastError().text() << prov; return false; }
                return true;
            };
            if (!insZone("江苏")) return 1;
            if (!insZone("浙江")) return 1;
            if (!insZone("上海")) return 1;
            qInfo() << "   [7.1] 写入合同 POLAIYA_LAJZ_001：基础价 3.0 + 步 0.1kg/0.15 元，封顶 1.5kg，5 票即可进池，进池省=江浙沪";
        }
        // 7.2 重新 SyncRulesSqliteToDuckDB 后再 ReloadRules（模拟 CalcBatch 开始时会 ReloadRules）
        SyncRulesSqliteToDuckDB(repo, rulesDb);
        dbm.ReloadRules(rulesDb);

        // 7.3 构造 10 条 客户A + 1 条上海（全部命中 0.5kg 小件 → 按合同池均重≈0.5 定价≈ 3.0 + ceil((0.5-0.3)/0.1)*0.15 = 3+2*0.15=3.30）
        {
            auto con2 = dbm.CreateConnection();
            con2.Query("DROP TABLE IF EXISTS _lajz_in");
            con2.Query(R"SQL(
CREATE TABLE _lajz_in (
    order_id VARCHAR, dest_province VARCHAR, dest_city VARCHAR,
    weight DOUBLE, vol_weight DOUBLE, customer_id VARCHAR
);
INSERT INTO _lajz_in VALUES
  ('L1','江苏','苏州', 0.5, 0, 'cA'),
  ('L2','江苏','南京', 0.5, 0, 'cA'),
  ('L3','江苏','无锡', 0.5, 0, 'cA'),
  ('L4','江苏','常州', 0.5, 0, 'cA'),
  ('L5','江苏','南通', 0.5, 0, 'cA'),
  ('L6','浙江','杭州', 0.6, 0, 'cA'),
  ('L7','浙江','宁波', 0.4, 0, 'cA'),
  ('L8','上海','浦东', 0.5, 0, 'cA'),
  ('L9','上海','徐汇', 0.5, 0, 'cA'),
  ('L10','江苏','盐城', 0.5, 0, 'cA'),
  ('X1','江苏','南京', 3.0, 0, 'cA');   -- 江苏在方案B白名单中，但 charge_weight=3.0 > 合同池上限 2.5 → lajz_in_pool_raw=FALSE → 退回 tpl_a 阶梯首8+续重（3.0-首1kg）=8+CEIL(2.0/1)*3 = 8+6=14元
)SQL");
        }

        // 7.4 enable=FALSE（关）→ 必须全走阶梯价，L1 基础运费=11
        {
            services::CalcService cs2;
            if (!cs2.CalcBatch("_lajz_in", "_lajz_off", false)) { qCritical() << "CalcBatch off fail"; return 1; }
            auto con3 = dbm.CreateConnection();
            auto r_off = con3.Query(R"SQL(SELECT "订单号","目的省份","基础运费","是否采用拉均重价","是否命中拉均重池" FROM _lajz_off ORDER BY "订单号")SQL");
            for (size_t i = 0; i < r_off->RowCount(); ++i) {
                QString oid = QString::fromStdString(r_off->GetValue(0,i).ToString());
                QString prov = QString::fromStdString(r_off->GetValue(1,i).ToString());
                double  bf   = r_off->GetValue(2,i).GetValue<double>();
                QString used = QString::fromStdString(r_off->GetValue(3,i).ToString());
                QString inpool = QString::fromStdString(r_off->GetValue(4,i).ToString());
                if (oid == "L1") {
                    ASSERT_NEAR(bf, 8.0, "off_L1 bf阶梯首1kg=8");
                    if (used != "否") { qCritical() << "off_L1 采用拉均重价必须是否，got=" << used; return 1; }
                    if (inpool != "否") { qCritical() << "off_L1 是否命中拉均重池必须是否，got=" << inpool; return 1; }
                    qInfo() << "   ✅ 开关关：L1 是否采用=" << used << "，命中池=" << inpool << "，基础运费=" << bf << "（阶梯首1kg）";
                }
            }
        }

        // 7.5 enable=TRUE（开）→ L1~L10 进池，池均重≈0.5kg，基础价≈3.30；X1北京不进池=11；"是否采用拉均重价=是" 必须命中
        {
            services::CalcService cs3;
            if (!cs3.CalcBatch("_lajz_in", "_lajz_on", true)) { qCritical() << "CalcBatch on fail"; return 1; }
            auto con4 = dbm.CreateConnection();
            auto r_on = con4.Query(R"SQL(SELECT "订单号","目的省份","基础运费","是否采用拉均重价","是否命中拉均重池","拉均重池平均重量(KG)","拉均重单票基础价(元)" FROM _lajz_on ORDER BY "订单号")SQL");
            qInfo() << "   -- enable=TRUE 全部结果：--";
            for (size_t i = 0; i < r_on->RowCount(); ++i) {
                QString oid = QString::fromStdString(r_on->GetValue(0,i).ToString());
                QString prov = QString::fromStdString(r_on->GetValue(1,i).ToString());
                double bf = r_on->GetValue(2,i).GetValue<double>();
                QString used = QString::fromStdString(r_on->GetValue(3,i).ToString());
                QString inp = QString::fromStdString(r_on->GetValue(4,i).ToString());
                double avgkg = r_on->GetValue(5,i).GetValue<double>();
                double lajz_bf = r_on->GetValue(6,i).GetValue<double>();
                qInfo().noquote() << QString("      %1 %2 基础费=%3  是否用拉=%4  进池=%5  池均重=%6  拉单票=%7")
                                         .arg(oid,-5).arg(prov,-4).arg(bf,0,'f',2)
                                         .arg(used,-4).arg(inp,-4).arg(avgkg,0,'f',4).arg(lajz_bf,0,'f',2);
            }
            // L1 必须：采用拉=是，命中池=是，基础费≈3.3
            {
                auto rL1 = con4.Query(R"SQL(SELECT "是否采用拉均重价","是否命中拉均重池","基础运费","拉均重单票基础价(元)" FROM _lajz_on WHERE "订单号"='L1')SQL");
                if (rL1->RowCount() == 0) { qCritical() << "L1 not found!"; return 1; }
                QString u = QString::fromStdString(rL1->GetValue(0,0).ToString());
                QString i = QString::fromStdString(rL1->GetValue(1,0).ToString());
                double bf  = rL1->GetValue(2,0).GetValue<double>();
                double lbf = rL1->GetValue(3,0).GetValue<double>();
                if (u != "是") { qCritical() << "FAIL：L1 是否采用拉均重价 必须是，got=" << u; return 1; }
                if (i != "是") { qCritical() << "FAIL：L1 是否命中拉均重池 必须是，got=" << i; return 1; }
                // 期望：池均重≈0.5 → 3.0 + ceil((0.5-0.3)/0.1)*0.15 = 3+2*0.15 = 3.30
                ASSERT_NEAR(lbf, 3.30, "L1 lajz_single_ticket≈3.30");
                ASSERT_NEAR(bf,  3.30, "L1 final_base_fee≈3.30 (拉均重价已覆盖阶梯)");
                qInfo() << "   ✅ 开关开：L1 采用拉均重价，基础价被覆盖为" << bf << "元（原阶梯 11 元 ↓↓↓）";
            }
            // X1 江苏 3kg：charge_weight=3.0 > pool_max=2.5 → 进池=否，采用拉=否，基础费=阶梯 8 + CEIL(2.0/1)*3 = 14 元
            {
                auto rX1 = con4.Query(R"SQL(SELECT "是否采用拉均重价","是否命中拉均重池","基础运费","计费重量(KG)" FROM _lajz_on WHERE "订单号"='X1')SQL");
                QString u = QString::fromStdString(rX1->GetValue(0,0).ToString());
                QString i = QString::fromStdString(rX1->GetValue(1,0).ToString());
                double bf  = rX1->GetValue(2,0).GetValue<double>();
                double cw  = rX1->GetValue(3,0).GetValue<double>();
                if (u != "否") { qCritical() << "FAIL：X1 是否采用拉均重价 必须否（超池重），got=" << u; return 1; }
                if (i != "否") { qCritical() << "FAIL：X1 是否命中拉均重池 必须否（超池重），got=" << i; return 1; }
                ASSERT_NEAR(cw, 3.0, "X1 3.0→0.1进一=3.0");
                ASSERT_NEAR(bf, 14.0, "X1 江苏超池重 → 阶梯首8+续2kg*3=14");
                qInfo() << "   ✅ 开关开：X1 江苏 3kg（超池上限2.5）→ 不进池，维持阶梯 14 元（计费重" << cw << "kg）";
            }
        }
    }

    // ========== 测试7.1：SqliteRuleRepository 层面直连——SaveAvgWeightTemplate + SetAvgWeightTplGroups/Excludes/Zones，验证主表+三张子表写库不报错（放在T7之后/T8之前，防止T8 return 1 导致 T7.1 根本不运行）==========
    qInfo() << "\n=========【测试7.1】SqliteRuleRepository 层：保存拉均重合同/分区/排除省/自定义省（DB 直连写入）=========";
    {
        const QString avg_tpl_id = "UT_LAJZ_DIRECT_001";
        QVariantMap tpl;
        tpl["avg_tpl_id"] = avg_tpl_id;
        tpl["name"] = "UT 直接写库测试合同";
        tpl["contract_no"] = "UT-001";
        tpl["template_id"] = "tpl_a";
        tpl["version"] = 1;
        tpl["effective_from"] = "2025-01-01";
        tpl["effective_to"] = "2099-12-31";
        tpl["base_avg_kg"] = 0.3;
        tpl["avg_pool_max_kg"] = 2.5;
        tpl["avg_fee_cap_kg"] = 1.5;
        tpl["base_fee"] = 3.0;
        tpl["step_kg"] = 0.1;
        tpl["step_fee"] = 0.15;
        tpl["min_tickets"] = 5;
        tpl["over_cap_mode"] = 0;
        tpl["reuse_zone_groups"] = 1;
        tpl["is_active"] = 1;
        bool ok1 = repo.SaveAvgWeightTemplate(tpl);
        if (!ok1) { qCritical() << "FAIL 7.1.1 SaveAvgWeightTemplate 失败"; return 1; }
        qInfo() << "   ✅ 7.1.1 主行 SaveAvgWeightTemplate 成功：avg_tpl_id=" << avg_tpl_id;

        QVariantList groups;
        auto addG = [&](const QString &tid, const QString &gc) {
            QVariantMap g;
            g["template_id"] = tid; g["group_code"] = gc;
            groups << g;
        };
        addG("tpl_a", "Z1");
        addG("tpl_a", "Z2");
        bool ok2 = repo.SetAvgWeightTplGroups(avg_tpl_id, groups);
        if (!ok2) { qCritical() << "FAIL 7.1.2 SetAvgWeightTplGroups 失败"; return 1; }
        auto rgroups = repo.GetAvgWeightTplGroups(avg_tpl_id);
        if (rgroups.size() != 2) { qCritical() << "FAIL 7.1.2 Get 回来 size!=2，实际=" << rgroups.size(); return 1; }
        qInfo() << "   ✅ 7.1.2 SetAvgWeightTplGroups 成功，DB 实际行数=" << rgroups.size();

        QVariantList excl;
        auto addE = [&](const QString &tid, const QString &gc, const QString &p) {
            QVariantMap e;
            e["template_id"] = tid; e["group_code"] = gc; e["province"] = p;
            excl << e;
        };
        addE("tpl_a", "Z1", "上海");
        addE("tpl_a", "Z2", "福建");
        bool ok3 = repo.SetAvgWeightExcludes(avg_tpl_id, excl);
        if (!ok3) { qCritical() << "FAIL 7.1.3 SetAvgWeightExcludes 失败"; return 1; }
        auto rexcl = repo.GetAvgWeightExcludes(avg_tpl_id);
        if (rexcl.size() != 2) { qCritical() << "FAIL 7.1.3 Get 回来 size!=2，实际=" << rexcl.size(); return 1; }
        qInfo() << "   ✅ 7.1.3 SetAvgWeightExcludes 成功，DB 实际行数=" << rexcl.size();

        QVariantList zones;
        QVariantMap z1; z1["zone_code"] = "__global__";
        z1["provinces"] = QStringList{"江苏", "浙江", "上海", "安徽"};
        zones << z1;
        bool ok4 = repo.SetAvgWeightTplGroups(avg_tpl_id, QVariantList());
        bool ok5 = repo.SetAvgWeightExcludes(avg_tpl_id, QVariantList());
        bool ok6 = repo.SetAvgWeightZones(avg_tpl_id, zones);
        if (!ok4 || !ok5 || !ok6) { qCritical() << "FAIL 7.1.4 清空A并写方案B失败：ok4=" << ok4 << "ok5=" << ok5 << "ok6=" << ok6; return 1; }
        auto rzones = repo.GetAvgWeightZones(avg_tpl_id);
        if (rzones.size() != 1) { qCritical() << "FAIL 7.1.4 zones 条数!=1，实际=" << rzones.size(); return 1; }
        QStringList provs = rzones[0].toMap()["provinces"].toStringList();
        if (provs.size() != 4) { qCritical() << "FAIL 7.1.4 zone 省个数!=4，实际=" << provs; return 1; }
        qInfo() << "   ✅ 7.1.4 SetAvgWeightZones 成功，DB 实际 zones[0] 省个数=" << provs.size() << "，provs=" << provs.join(",");
        qInfo() << "   ✅ 测试7.1：SqliteRuleRepository 直连 4 个子操作 + Get 回读全部通过";
    }

    // ========== 测试8：用户真实场景方案A reuse_zone_groups=1 + 模板级绑定 fallback + 分区组白名单 + 排除省 ==========
    qInfo() << "\n=========【测试8】方案A（默认）+ 模板级绑定 + 分区勾选 + 排除省  ==========";
    {
        // 8.1 重置客户A的客户级外键（模拟用户"没有在客户资料里挂合同，只在合同里选模板=tpl_a 这种模板级绑定"）
        //     关键：必须先删除 T7 测试写入的 POLAIYA_LAJZ_001 合同（方案B，template_id='tpl_a', version=1）
        //           否则两个同 version=1、同 template_id='tpl_a' 的合同会冲突（DuckDB GROUP BY 随机取 POLAIYA_LAJZ_001），
        //           导致 T8 的 POL_TPL_001（方案A、排除上海、min=3）永远不生效
        {
            QSqlQuery q(repo.Database());
            // 清理：所有写在 T8 之前、template_id='tpl_a' 的拉均重合同（否则 GROUP BY 取 version=1 最大同 version=1 的，DuckDB 随机取，导致 POL_TPL_001 不生效）
            for (const char *oldIds : {"UT_LAJZ_DIRECT_001", "POLAIYA_LAJZ_001"}) {
                q.prepare(QString("DELETE FROM avg_weight_templates     WHERE avg_tpl_id='%1'").arg(oldIds));
                q.exec();
                q.prepare(QString("DELETE FROM avg_weight_zones           WHERE avg_tpl_id='%1'").arg(oldIds)); q.exec();
                q.prepare(QString("DELETE FROM avg_weight_zone_tpl_groups WHERE avg_tpl_id='%1'").arg(oldIds)); q.exec();
                q.prepare(QString("DELETE FROM avg_weight_zone_excludes   WHERE avg_tpl_id='%1'").arg(oldIds)); q.exec();
            }
            // 清客户A的客户级绑定（确保走模板级 fallback）
            q.prepare("UPDATE customers SET avg_weight_tpl_id=NULL WHERE customer_id='cA'");
            if (!q.exec()) { qCritical() << "reset cust fail:" << q.lastError().text(); return 1; }
            // 写一份新合同：POL_TPL_001，template_id='tpl_a'（模板级绑定），reuse_zone_groups=1，min_tickets=3
            q.prepare("INSERT OR REPLACE INTO avg_weight_templates"
                      "(avg_tpl_id,template_id,name,contract_no,avg_pool_max_kg,base_avg_kg,"
                      " avg_fee_cap_kg,base_fee,step_kg,step_fee,min_tickets,"
                      " reuse_zone_groups,over_cap_mode,is_active,version,effective_from,effective_to)"
                      " VALUES"
                      "('POL_TPL_001','tpl_a','珀莱-模板级绑定测试','LAJZ-TPL-001', 2.5, 0.3,"
                      " 1.5, 2.7, 0.1, 0.12, 3,"
                      " 1, 0, 1, 1, '2025-01-01', '2099-12-31')");
            if (!q.exec()) { qCritical() << "ins POL_TPL_001 fail:" << q.lastError().text(); return 1; }
            // 方案A：勾选模板分区 tpl_a 的 Z1（只有Z1，大写！），并再新建 Z2 但 tpl_a 原本没有 Z2，就只勾 Z1 模拟"全勾"
            QVariantList tplGroups;
            auto addGrp = [&](const QString &tid, const QString &gcode) {
                QVariantMap m; m["template_id"]=tid; m["group_code"]=gcode; tplGroups << m;
            };
            addGrp("tpl_a", "Z1");   // 勾选 Z1（江浙沪 zone1=江浙沪 3省）
            if (!repo.SetAvgWeightTplGroups("POL_TPL_001", tplGroups)) { qCritical() << "SetTplGrp fail"; return 1; }
            // 方案A排除省：Z1 下 排除 上海（上海不进池）
            QVariantList excl;
            auto addExcl = [&](const QString &tid, const QString &gcode, const QString &prov) {
                QVariantMap m; m["template_id"]=tid; m["group_code"]=gcode; m["province"]=prov; excl << m;
            };
            addExcl("tpl_a", "Z1", "上海");  // Z1 下排除 上海
            if (!repo.SetAvgWeightExcludes("POL_TPL_001", excl)) { qCritical() << "SetExcl fail"; return 1; }
            qInfo() << "   [8.1] 新合同 POL_TPL_001：模板级绑定 tpl_a，方案A勾选 Z1(江浙沪)，Z1排除省=上海，min_tickets=3，2.7+0.12/0.1kg 封顶1.5kg";
        }
        // 8.2 强制 reload
        SyncRulesSqliteToDuckDB(repo, rulesDb);
        dbm.ReloadRules(rulesDb);
        // 8.3 构造 5 条：江苏/浙江/上海/江苏/福建 → 走 CalcFromFile（与用户真实场景完全一致的路径：自动 ReloadRules + 同 Connection 计算）
        QString t8InputCSV, t8OutputCSV;
        {
            QTemporaryDir td; td.setAutoRemove(false);
            t8InputCSV = td.path() + QStringLiteral("/t8_input.csv");
            t8OutputCSV = td.path() + QStringLiteral("/t8_output.csv");
            QFile f(t8InputCSV);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { qCritical() << "write csv fail"; return 1; }
            QTextStream ts(&f);
            ts << "订单号,目的省份,目的城市,实际重量(KG),体积重量(KG),客户编号\n";
            auto addRow = [&](const QString &o, const QString &p, const QString &c, double w, double v, const QString &cid) {
                ts << o << ',' << p << ',' << c << ','
                   << QString::number(w,'f',2) << ',' << QString::number(v,'f',2) << ',' << cid << '\n';
            };
            addRow("T8L1","江苏","苏州",0.5,0,"cA");
            addRow("T8L2","浙江","杭州",0.5,0,"cA");
            addRow("T8L3","上海","浦东",0.5,0,"cA");
            addRow("T8L4","江苏","南京",0.4,0,"cA");
            addRow("T8X2","福建","福州",0.5,0,"cA");
            f.close();
            qInfo() << "   [8.3] 写入5条测试CSV:" << t8InputCSV << "（输出→" << t8OutputCSV << "）";
        }
        // 8.4 enable=TRUE 跑 CalcFromFile（跟用户界面完全一致的路径：文件导入 → ReloadRules → 归一化列 → CalcBatch → 导出）
        {
            services::CalcService cs;
            if (!cs.CalcFromFile(t8InputCSV, t8OutputCSV, true)) { qCritical() << "CalcFromFile t8 fail"; return 1; }
            auto con = dbm.CreateConnection();
            // 8.4.1 把输出CSV 再次导入 DuckDB 的 _t8out 表，方便查询断言
            con.Query("DROP TABLE IF EXISTS _t8out");
            if (!dbm.ImportFromFile("_t8out", t8OutputCSV)) {
                qCritical() << "re-import t8OutputCSV fail:" << t8OutputCSV;
                return 1;
            }
            // 8.4.2 行数检查 + 打印所有行
            {
                auto rc = con.Query("SELECT COUNT(*) AS c FROM _t8out");
                long long cnt = (rc && rc->RowCount()) ? rc->GetValue(0,0).GetValue<long long>() : -1;
                qInfo() << "   _t8out rows=" << cnt;
                if (cnt <= 0) { qCritical() << "CalcFromFile 输出 0 行"; return 1; }
            }
            {
                auto cols = con.Query(R"SQL(SELECT column_name FROM information_schema.columns WHERE table_schema=current_schema() AND table_name='_t8out' ORDER BY ordinal_position)SQL");
                if (cols) {
                    QStringList cnames;
                    for (size_t i = 0; i < cols->RowCount(); ++i) {
                        cnames << QString::fromStdString(cols->GetValue(0,i).ToString());
                    }
                    qInfo() << "   _t8out columns=" << cnames.join(" | ");
                }
            }
            // 输出列名在 CalcService 里是中文（带引号），CSV 导入 DuckDB 后列名一般会去掉引号或保留；这里用 CSV 导入的真实列名查
            auto rs = con.Query(R"SQL(SELECT * FROM _t8out ORDER BY 1)SQL");
            qInfo() << "   -- 测试8 enable=TRUE 结果：--";
            for (size_t i = 0; i < (rs ? rs->RowCount() : 0); ++i) {
                QStringList cells;
                for (size_t j = 0; j < (rs ? rs->ColumnCount() : 0); ++j) {
                    cells << QString::fromStdString(rs->GetValue(j,i).ToString());
                }
                qInfo().noquote() << "      " << cells.join("  |  ");
            }
            // 核心断言：T8L1 江苏 → 进池 + 采用拉均重价，基础费≈ 2.7 + ceil((0.5-0.3)/0.1)*0.12 = 2.7 + 2*0.12 = 2.94
            {
                auto r = con.Query(R"SQL(SELECT "是否命中拉均重池","是否采用拉均重价","基础运费" FROM _t8out WHERE "订单号"='T8L1')SQL");
                if (r->RowCount()==0) { qCritical() << "T8L1 miss"; return 1; }
                QString h = QString::fromStdString(r->GetValue(0,0).ToString());
                QString u = QString::fromStdString(r->GetValue(1,0).ToString());
                double bf = r->GetValue(2,0).GetValue<double>();
                if (h != "是") { qCritical() << "FAIL T8L1 命中必须是，got=" << h; return 1; }
                if (u != "是") { qCritical() << "FAIL T8L1 采用必须是，got=" << u; return 1; }
                ASSERT_NEAR(bf, 2.94, "T8L1 基础费≈2.7+2*0.12=2.94");
                qInfo() << "   ✅ T8L1（江苏 zone1 勾选+不排除）：进池+采用拉均重价，基础价" << bf << "元";
            }
            // T8L3 上海 → 命中="否"（被排除），基础费=8
            {
                auto r = con.Query(R"SQL(SELECT "是否命中拉均重池","是否采用拉均重价","基础运费" FROM _t8out WHERE "订单号"='T8L3')SQL");
                QString h = QString::fromStdString(r->GetValue(0,0).ToString());
                QString u = QString::fromStdString(r->GetValue(1,0).ToString());
                double bf = r->GetValue(2,0).GetValue<double>();
                if (h != "否") { qCritical() << "FAIL T8L3 命中必须否（上海被排除），got=" << h; return 1; }
                if (u != "否") { qCritical() << "FAIL T8L3 采用必须否，got=" << u; return 1; }
                ASSERT_NEAR(bf, 8.0, "T8L3 上海被排除 → 阶梯 8");
                qInfo() << "   ✅ T8L3（上海被排除省剔除）：不进池，阶梯 8 元";
            }
            // T8X2 福建 → 不在 tpl_a Z1 勾选分区 → group_code 空 → 命中=否（福建没有在 Z1 阶梯里，阶梯 base_fee=0 是正常数据集问题，不影响拉均重进池判定）
            {
                auto r = con.Query(R"SQL(SELECT "是否命中拉均重池","是否采用拉均重价" FROM _t8out WHERE "订单号"='T8X2')SQL");
                QString h = QString::fromStdString(r->GetValue(0,0).ToString());
                QString u = QString::fromStdString(r->GetValue(1,0).ToString());
                if (h != "否") { qCritical() << "FAIL T8X2 命中必须否（福建非勾选分区），got=" << h; return 1; }
                if (u != "否") { qCritical() << "FAIL T8X2 采用必须否，got=" << u; return 1; }
                qInfo() << "   ✅ T8X2（福建非勾选分区）：不进池";
            }
        }
    }

    qInfo() << "\n🎉 全部 8 类 断言全部通过！✅\n";
    // cleanup
    QDir(tmpDir).removeRecursively();
    return 0;
}
