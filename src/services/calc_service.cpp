#include "services/calc_service.hpp"
#include "core/freight_types.hpp"
#include "core/app_config.hpp"
#include "db/duckdb_manager.hpp"
#include "db/sqlite_rule_repository.hpp"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantList>
#include <QMap>
#include <QSet>
#include <QStringList>

namespace freight::services {

CalcService::CalcService(QObject *parent) : QObject(parent) {}

namespace {
double RoundChargeWeightByMode(double cw, const QString &mode) {
    if (cw <= 0) return cw;
    const QString m = mode.trimmed().isEmpty() ? QStringLiteral("ceil_0_1kg") : mode;
    if (m == "ceil_1kg")       return std::ceil(cw);
    if (m == "ceil_0_5kg")     return std::ceil(cw * 2.0) / 2.0;
    if (m == "ceil_0_1kg")     return std::ceil(cw * 10.0) / 10.0;
    if (m == "round_0_1kg")    return std::round(cw * 10.0) / 10.0;
    if (m == "floor_no_round") return cw;
    return std::ceil(cw * 10.0) / 10.0;  // 默认：国标推荐 0.1kg 进一
}
} // anon ns

core::CalcResult CalcService::CalcSingle(const QString &province,
                                          double weight,
                                          double vol_weight,
                                          const QString &template_id,
                                          const QString &city,
                                          const QString &customer_id,
                                          double vol_length,
                                          double vol_width,
                                          double vol_height,
                                          bool enable_avg_weight) {
    core::CalcResult result;
    result.dest_province = province;
    result.weight = weight;
    result.vol_weight = vol_weight;

    try {
        auto &cfg = core::AppConfig::Instance();
        db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
        repo.Init();

        // 关键修复：单笔计算前也强制把 SQLite 新规则同步到 DuckDB，
        // 保证用户在「规则设置」里刚保存的合同能立刻生效，且与批量算结果一致
        try {
            auto &dbm_lazy = db::DuckDBManager::Instance();
            dbm_lazy.ReloadRules(cfg.GetRulesDbPath());
        } catch (...) {}

        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        // 先读客户级覆写 3 列（优先于模板级）
        QString cust_rounding_mode;
        double  cust_additional_unit = 0.0;
        int     cust_vol_divisor = 0;
        if (!customer_id.isEmpty()) {
            QVariantMap cust = repo.GetCustomer(customer_id);
            if (!cust.isEmpty()) {
                cust_rounding_mode = cust.value("cust_rounding_mode").toString().trimmed();
                bool ok = false;
                double au = cust.value("cust_additional_unit").toDouble(&ok);
                if (ok && au > 0) cust_additional_unit = au;
                int vd = cust.value("cust_vol_divisor").toInt(&ok);
                if (ok && vd > 0)  cust_vol_divisor = vd;
            }
        }

        // 读模板计费三参数：续重进位、续重单位、体积重除数
        QString rounding_mode = "ceil_0_1kg";
        double tpl_additional_unit = 1.0;
        int vol_divisor = 6000;
        QVariantMap tpl = repo.GetTemplate(template_id);
        if (!tpl.isEmpty()) {
            // 客户级三列优先 > 模板级 > 默认
            rounding_mode = cust_rounding_mode;
            if (rounding_mode.isEmpty())
                rounding_mode = tpl.value("tpl_rounding_mode", QString{}).toString().trimmed();
            if (rounding_mode.isEmpty())
                rounding_mode = "ceil_0_1kg";
            if (cust_additional_unit > 0) tpl_additional_unit = cust_additional_unit;
            else {
                double au_tpl = tpl.value("tpl_additional_unit", 0.0).toDouble();
                if (au_tpl > 0) tpl_additional_unit = au_tpl;
                else {
                    double au_old = tpl.value("additional_unit", 0.0).toDouble();
                    if (au_old > 0) tpl_additional_unit = au_old;
                }
            }
            if (cust_vol_divisor > 0) vol_divisor = cust_vol_divisor;
            else {
                int vd_tpl = tpl.value("tpl_vol_divisor", 6000).toInt();
                if (vd_tpl > 0) vol_divisor = vd_tpl;
                else {
                    int vd_old = tpl.value("vol_weight_ratio", 6000).toInt();
                    if (vd_old > 0) vol_divisor = vd_old;
                }
            }
        }

        // 体积重：若传了长宽高，用 vol_divisor 现算；否则用已填 vol_weight
        double actual_vol = vol_weight;
        if (vol_length > 0 && vol_width > 0 && vol_height > 0) {
            actual_vol = (vol_length * vol_width * vol_height) / static_cast<double>(vol_divisor);
        }
        // 计费重 = MAX(实重, 体积重>0 && 体积重>实重 ? 体积重 : 实重)
        double raw_cw = weight;
        if (actual_vol > 0 && actual_vol > weight) raw_cw = actual_vol;
        // 按 rounding_mode 进位（国标默认 0.1kg 进一）
        double charge_weight = RoundChargeWeightByMode(raw_cw, rounding_mode);
        // 续重单位（模板级优先），C++层先存，SQL里再显式传一份，保证两端一致
        const double eff_add_unit = tpl_additional_unit > 0 ? tpl_additional_unit : 1.0;

        QRegularExpression province_suffix_re(R"((省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$)");
        QString norm_province = province;
        norm_province.remove(province_suffix_re);

        QString sql = QString(R"SQL(
WITH
template_info AS (
    SELECT
        COALESCE(default_no_weight_fee, 0) AS default_no_weight_fee,
        COALESCE(NULLIF(tpl_rounding_mode, ''), 'ceil_0_1kg') AS rounding_mode,
        COALESCE(NULLIF(tpl_additional_unit, 0), NULLIF(additional_unit, 0), 1.0) AS tpl_additional_unit,
        COALESCE(NULLIF(tpl_vol_divisor, 0), CAST(NULLIF(vol_weight_ratio, 0) AS INTEGER), 6000) AS tpl_vol_divisor
    FROM freight_templates
    WHERE template_id = '%1'
),
matched_zone AS (
    SELECT zgp.group_code, zg.group_name
    FROM zone_group_provinces zgp
    LEFT JOIN zone_groups zg
        ON zg.template_id = zgp.template_id
       AND zg.group_code = zgp.group_code
    WHERE zgp.template_id = '%1'
      AND REGEXP_REPLACE(zgp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '%2'
    LIMIT 1
),
matched_tier AS (
    SELECT tp.*
    FROM tiered_pricing tp
    WHERE tp.template_id = '%1'
      AND tp.group_code = (SELECT group_code FROM matched_zone)
      AND %3 > tp.min_weight
      AND %3 <= tp.max_weight
    LIMIT 1
),
tier_max AS (
    SELECT first_weight AS max_first_weight,
           first_price AS max_first_price,
           additional_unit AS max_additional_unit,
           additional_price AS max_additional_price,
           max_weight AS max_tier_weight
    FROM tiered_pricing
    WHERE template_id = '%1'
      AND group_code = (SELECT group_code FROM matched_zone)
    ORDER BY sort_order DESC
    LIMIT 1
),
base_fee_calc AS (
    SELECT
        CASE
            WHEN %3 <= 0 OR %3 IS NULL
                THEN (SELECT default_no_weight_fee FROM template_info)
            WHEN (SELECT group_code FROM matched_zone) IS NULL THEN 0
            WHEN %3 <= (SELECT first_weight FROM matched_tier)
                THEN (SELECT first_price FROM matched_tier)
            WHEN (SELECT tier_code FROM matched_tier) IS NOT NULL
                THEN (SELECT first_price FROM matched_tier)
                     + GREATEST(0, CEIL(
                         (%3 - (SELECT first_weight FROM matched_tier))
                         / GREATEST(COALESCE((SELECT tpl_additional_unit FROM template_info), %7), 0.0001)
                     )) * (SELECT additional_price FROM matched_tier)
            ELSE
                COALESCE((SELECT max_first_price FROM tier_max), 0) +
                GREATEST(0, CEIL(
                    (%3 - COALESCE((SELECT max_first_weight FROM tier_max), 0))
                    / GREATEST(COALESCE((SELECT tpl_additional_unit FROM template_info), %7), 0.0001)
                )) * COALESCE((SELECT max_additional_price FROM tier_max), 0)
        END AS base_fee
),
fuel_surcharge_calc AS (
    SELECT
        bfc.base_fee,
        COALESCE((
            SELECT fs.rate
            FROM fuel_surcharge fs
            WHERE fs.template_id = '%1'
              AND fs.is_active = 1
              AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                         WHERE template_id = '%1'
                                           AND is_active = 1
                                           AND effective_date <= CURRENT_DATE)
            LIMIT 1
        ), (
            SELECT fs.rate
            FROM fuel_surcharge fs
            WHERE fs.template_id = '*'
              AND fs.is_active = 1
              AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                         WHERE template_id = '*'
                                           AND is_active = 1
                                           AND effective_date <= CURRENT_DATE)
            LIMIT 1
        ), 0) * bfc.base_fee AS fuel_surcharge
    FROM base_fee_calc bfc
),
remote_area_calc AS (
    SELECT
        fsc.*,
        COALESCE(NULLIF((
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = '%1'
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND REGEXP_REPLACE(ra.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '%2'
                   AND (ra.city IS NULL OR ra.city = ''
                        OR REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                           = REGEXP_REPLACE('%4', '(市|区|县|旗|自治县|林区)$', ''))
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                       = REGEXP_REPLACE('%4', '(市|区|县|旗|自治县|林区)$', '')
                   AND (ra.district IS NULL OR ra.district = ''))
              )
        ), 0), (
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = '*'
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND REGEXP_REPLACE(ra.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '%2'
                   AND (ra.city IS NULL OR ra.city = ''
                        OR REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                           = REGEXP_REPLACE('%4', '(市|区|县|旗|自治县|林区)$', ''))
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                       = REGEXP_REPLACE('%4', '(市|区|县|旗|自治县|林区)$', '')
                   AND (ra.district IS NULL OR ra.district = ''))
              )
        ), 0) AS remote_surcharge
    FROM fuel_surcharge_calc fsc
),
strategy_surcharge_calc AS (
    SELECT
        rac.*,
        COALESCE((
            SELECT SUM(
                CASE s.strategy_type
                    WHEN 'fixed' THEN s.amount
                    WHEN 'percentage' THEN rac.base_fee * s.amount
                    WHEN 'per_weight' THEN %3 * s.amount
                    WHEN 'per_volume' THEN COALESCE(NULLIF(%6, 0), %3) * s.amount
                    ELSE 0
                END
            )
            FROM surcharge_strategies s
            LEFT JOIN surcharge_provinces sp ON sp.strategy_id = s.strategy_id
            LEFT JOIN surcharge_customers sc ON sc.strategy_id = s.strategy_id
            LEFT JOIN surcharge_date_ranges sd ON sd.strategy_id = s.strategy_id
            WHERE s.is_active = 1
              AND (s.strategy_scope IN ('global', 'template') OR s.template_id = '%1')
              AND (
                  s.strategy_scope IN ('global', 'template')
                  OR (s.strategy_scope = 'province'
                      AND REGEXP_REPLACE(sp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') = '%2')
                  OR (s.strategy_scope = 'customer' AND sc.customer_id = '%5')
              )
              AND (sd.strategy_id IS NULL
                   OR (CURRENT_DATE BETWEEN sd.start_date AND sd.end_date))
              AND (s.min_weight IS NULL OR %3 >= s.min_weight)
              AND (s.max_weight IS NULL OR s.max_weight = 0 OR %3 <= s.max_weight)
        ), 0) AS strategy_surcharge
    FROM remote_area_calc rac
)
SELECT
    ROUND(%3, 3) AS charge_weight,
    ROUND(base_fee, 2) AS base_fee,
    ROUND(fuel_surcharge, 2) AS fuel_surcharge,
    ROUND(remote_surcharge, 2) AS remote_surcharge,
    ROUND(strategy_surcharge, 2) AS strategy_surcharge,
    ROUND(base_fee + fuel_surcharge + remote_surcharge + strategy_surcharge, 2) AS total_fee,
    0::BOOLEAN AS lajz_in_pool,
    ''::VARCHAR AS lajz_contract_no,
    0.0::DOUBLE  AS lajz_pool_avg_kg,
    0.0::DOUBLE  AS lajz_pool_max_kg,
    0.0::DOUBLE  AS lajz_base_avg_kg,
    0.0::DOUBLE  AS lajz_fee_cap_kg,
    0.0::DOUBLE  AS lajz_base_fee,
    0.0::DOUBLE  AS lajz_step_kg,
    0.0::DOUBLE  AS lajz_step_fee,
    0::BOOLEAN AS lajz_used,
    0.0::DOUBLE  AS lajz_fee_per_ticket,
    0.0::DOUBLE  AS lajz_save_vs_tier,
    0::INT      AS lajz_over_cap_mode
FROM strategy_surcharge_calc
        )SQL")
        .arg(template_id, norm_province)
        .arg(charge_weight, 0, 'f', 6)
        .arg(city)
        .arg(customer_id)
        .arg(actual_vol, 0, 'f', 6)
        .arg(eff_add_unit, 0, 'f', 6);

        qDebug() << "Calculating:" << province << weight << "kg, charge_weight:" << charge_weight;

        auto res = con.Query(sql.toStdString());

        if (res->RowCount() > 0) {
            result.charge_weight = res->GetValue(0, 0).GetValue<double>();
            result.base_fee = res->GetValue(1, 0).GetValue<double>();
            result.fuel_surcharge = res->GetValue(2, 0).GetValue<double>();
            result.remote_surcharge = res->GetValue(3, 0).GetValue<double>();
            result.strategy_surcharge = res->GetValue(4, 0).GetValue<double>();
            result.total_fee = res->GetValue(5, 0).GetValue<double>();
            result.success = true;
            qDebug() << "  Result:"
                     << "base_fee=" << result.base_fee
                     << "fuel=" << result.fuel_surcharge
                     << "remote=" << result.remote_surcharge
                     << "strategy=" << result.strategy_surcharge
                     << "total=" << result.total_fee;
        } else {
            qCritical() << "No result rows!";
            result.success = false;
            result.error_msg = "无计算结果";
        }

        // ====== CalcSingle 拉均重补充计算（单算近似：池均重=本单charge_weight，省份门控对齐 Batch SQL 语义） ======
        //   注意：真实"池均重"必须 Batch 批量才有上下文，这里只验证"是否进池/是否有资格用拉均重价"的门控逻辑，
        //        和 BuildCalcSQL 里 avg_weight_active / avg_weight_pool_in 两端完全一致。
        //   FEAT-01：enable_avg_weight 总开关（默认 false）→ 未勾选时完全跳过拉均重逻辑
        if (result.success && enable_avg_weight) {
            // 客户级覆写：cust_contract_no / cust_avg_weight_tpl_id
            QString cust_contract_no;
            QString cust_avg_weight_tpl_id;
            if (!customer_id.isEmpty()) {
                QVariantMap cust = repo.GetCustomer(customer_id);
                if (!cust.isEmpty()) {
                    cust_contract_no = cust.value("cust_contract_no").toString().trimmed();
                    cust_avg_weight_tpl_id = cust.value("avg_weight_tpl_id").toString().trimmed();
                }
            }

            // A. 取 group_code（和 batch CTE matched_zone 同语义）
            QString group_code;
            {
                QSqlQuery zg_q(repo.Database());
                zg_q.prepare("SELECT zgp.group_code FROM zone_group_provinces zgp "
                             "WHERE zgp.template_id=? "
                             "AND REGEXP_REPLACE(zgp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')=? LIMIT 1");
                zg_q.addBindValue(template_id);
                zg_q.addBindValue(norm_province);
                if (zg_q.exec() && zg_q.next()) group_code = zg_q.value(0).toString();
            }

            // B. 匹配一份合同（和 avg_weight_active + avg_weight_joined 同语义）
            QString lajz_avg_tpl_id;
            double  lajz_pool_max_kg = 1.0;
            double  lajz_base_avg_kg = 0.3;
            double  lajz_fee_cap_kg  = 1.0;
            double  lajz_base_fee    = 2.7;
            double  lajz_step_kg     = 0.1;
            double  lajz_step_fee    = 0.2;
            int     lajz_min_tickets = 50;
            int     lajz_over_cap_mode = 0;
            int     lajz_reuse_zone_groups = 1;
            QString lajz_contract_no;
            {
                auto pick = [&](const QString &bind_avg, const QString &bind_tpl) -> bool {
                    QVariantList tpls = repo.ListAvgWeightTemplates();
                    int best_ver = -1;
                    QVariantMap best;
                    QDate today = QDate::currentDate();
                    for (const auto &t : tpls) {
                        QVariantMap m = t.toMap();
                        if (m["is_active"].toInt() != 1) continue;
                        QString bind_aw = m["avg_tpl_id"].toString().trimmed();
                        QString bind_tp = m["template_id"].toString().trimmed();
                        if (bind_avg.isEmpty()) {
                            if (bind_tp != bind_tpl && bind_tp != "") continue;
                        } else {
                            if (bind_aw != bind_avg) continue;
                        }
                        QDate ef = QDate::fromString(m["effective_from"].toString().left(10), Qt::ISODate);
                        QString et_s = m["effective_to"].toString().trimmed();
                        QDate et;
                        if (!et_s.isEmpty() && et_s != "") et = QDate::fromString(et_s.left(10), Qt::ISODate);
                        if (ef.isValid() && ef > today) continue;
                        if (et.isValid() && !et_s.isEmpty() && et < today) continue;
                        int ver = m["version"].toInt();
                        if (ver > best_ver) { best_ver = ver; best = m; }
                    }
                    if (best.isEmpty()) return false;
                    lajz_avg_tpl_id        = best["avg_tpl_id"].toString();
                    lajz_pool_max_kg       = best["avg_pool_max_kg"].toDouble();
                    lajz_base_avg_kg       = best["base_avg_kg"].toDouble();
                    lajz_fee_cap_kg        = best["avg_fee_cap_kg"].toDouble();
                    lajz_base_fee          = best["base_fee"].toDouble();
                    lajz_step_kg           = best["step_kg"].toDouble();
                    lajz_step_fee          = best["step_fee"].toDouble();
                    lajz_min_tickets       = best["min_tickets"].toInt();
                    lajz_over_cap_mode     = best["over_cap_mode"].toInt();
                    lajz_reuse_zone_groups = best["reuse_zone_groups"].toInt();
                    lajz_contract_no       = best["contract_no"].toString().trimmed();
                    return true;
                };
                bool ok = false;
                if (!cust_avg_weight_tpl_id.isEmpty())
                    ok = pick(cust_avg_weight_tpl_id, template_id);
                if (!ok)
                    ok = pick("", template_id);
                (void)ok;
            }

            if (!lajz_avg_tpl_id.isEmpty()) {
                // C. 进池判定（和 avg_weight_pool_in CTE 对齐）
                bool in_pool_raw = false;
                if (charge_weight <= lajz_pool_max_kg) {
                    if (lajz_reuse_zone_groups == 1 && !group_code.isEmpty()) {
                        // 方案A：勾选分区 + 排除省
                        bool has_any_check = false;
                        bool checked = false;
                        {
                            QVariantList gts = repo.GetAvgWeightTplGroups(lajz_avg_tpl_id);
                            for (const auto &g : gts) {
                                const auto m = g.toMap();
                                QString gt_t = m["template_id"].toString();
                                QString gt_g = m["group_code"].toString();
                                if (gt_t != "" && gt_t != "*" && gt_t != template_id) continue;
                                has_any_check = true;
                                if (gt_g == group_code) { checked = true; break; }
                            }
                        }
                        bool pass_check = (!has_any_check) || checked;
                        bool excluded = false;
                        {
                            QVariantList ex = repo.GetAvgWeightExcludes(lajz_avg_tpl_id);
                            for (const auto &e : ex) {
                                const auto m = e.toMap();
                                QString ex_t = m["template_id"].toString();
                                QString ex_g = m["group_code"].toString();
                                QString ex_p = m["province"].toString();
                                if (ex_t != "" && ex_t != "*" && ex_t != template_id) continue;
                                if (ex_g != group_code) continue;
                                // 归一化排除省
                                QString ne = ex_p;
                                static const QRegularExpression reProv(
                                    "(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$");
                                ne.replace(reProv, "");
                                if (ne.trimmed() == norm_province.trimmed()) { excluded = true; break; }
                            }
                        }
                        in_pool_raw = pass_check && !excluded;
                    } else if (lajz_reuse_zone_groups == 0) {
                        // 方案B：avg_weight_zones 归一化省命中
                        QVariantList zs = repo.GetAvgWeightZones(lajz_avg_tpl_id);
                        static const QRegularExpression reProv(
                            "(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$");
                        for (const auto &z : zs) {
                            const auto m = z.toMap();
                            const QStringList ps = m["provinces"].toStringList();
                            for (const auto &p : ps) {
                                QString np = p;
                                np.replace(reProv, "");
                                if (np.trimmed() == norm_province.trimmed()) { in_pool_raw = true; break; }
                            }
                            if (in_pool_raw) break;
                        }
                    }
                }
                result.lajz_in_pool     = in_pool_raw;
                result.lajz_avg_tpl_id  = lajz_avg_tpl_id;
                result.lajz_contract_no = cust_contract_no.isEmpty() ? lajz_contract_no : cust_contract_no;
                result.lajz_pool_avg_kg = in_pool_raw ? charge_weight : 0.0;
                result.lajz_pool_max_kg = lajz_pool_max_kg;
                result.lajz_base_avg_kg = lajz_base_avg_kg;
                result.lajz_fee_cap_kg  = lajz_fee_cap_kg;
                result.lajz_base_fee    = lajz_base_fee;
                result.lajz_step_kg     = lajz_step_kg;
                result.lajz_step_fee    = lajz_step_fee;
                result.lajz_over_cap_mode = lajz_over_cap_mode;
                // 单算：pool 门槛和 over_cap_mode 封顶 简单保守判断
                // （单票一个池 → pool_n=1，只要 min_tickets<=1 就合格；正常拉均重合同 min_tickets>=50 会不合格 → 退回阶梯）
                bool pool_eligible = (1 >= lajz_min_tickets);
                bool pass_cap = (lajz_over_cap_mode == 0) ||
                                (lajz_over_cap_mode == 1 && result.lajz_pool_avg_kg <= lajz_fee_cap_kg);
                bool used = in_pool_raw && pool_eligible && pass_cap;
                result.lajz_used = used;
                if (used) {
                    double stepkg = qMax(lajz_step_kg, 0.0001);
                    double steps = std::max(0.0, std::ceil((result.lajz_pool_avg_kg - lajz_base_avg_kg) / stepkg));
                    double cap   = std::max(0.0, std::ceil((lajz_fee_cap_kg - lajz_base_avg_kg) / stepkg));
                    double ticket = lajz_base_fee + std::min(steps, cap) * lajz_step_fee;
                    result.lajz_fee_per_ticket = std::round(ticket * 100.0) / 100.0;
                    result.lajz_save_vs_tier   = std::round((result.base_fee - result.lajz_fee_per_ticket) * 100.0) / 100.0;
                    result.base_fee = result.lajz_fee_per_ticket;
                    result.total_fee = std::round((result.base_fee + result.fuel_surcharge + result.remote_surcharge + result.strategy_surcharge) * 100.0) / 100.0;
                } else {
                    result.lajz_fee_per_ticket = 0.0;
                    result.lajz_save_vs_tier   = 0.0;
                }
            }
        }
    } catch (const std::exception &e) {
        result.success = false;
        result.error_msg = QString::fromStdString(e.what());
        qCritical() << "CalcSingle failed:" << result.error_msg;
    }

    return result;
}

bool CalcService::CalcBatch(const QString &input_table,
                            const QString &output_table,
                            bool enable_avg_weight) {
    try {
        emit ProgressChanged(2);
        auto &cfg = core::AppConfig::Instance();
        auto &dbm = db::DuckDBManager::Instance();
        // 防御性：CalcBatch 外部直接调用时（不走 CalcFromFile），也要确保规则表已 reload
        //   T8 集成测试 + 单条计算对话框都可能直接调用 CalcBatch 而未先 ReloadRules，
        //   这会导致 DuckDB 找不到 customers/freight_templates/avg_weight_templates 报 Catalog Error
        try {
            dbm.ReloadRules(cfg.GetRulesDbPath());
        } catch (const std::exception &re) {
            qWarning() << "[CalcBatch] ReloadRules skipped:" << re.what();
        }
        emit ProgressChanged(5);
        auto con = dbm.CreateConnection();

        QString sql = BuildCalcSQL(input_table, output_table, enable_avg_weight);
        emit ProgressChanged(15);
        // ========= 关键修复：检查 CREATE TABLE 的 Query 返回值 =========
        //   原代码没检查 Result 是否为 nullptr / 是否含 Error；SQL 语法错/列不匹配只会被吞掉返回 true
        auto qresult = con.Query(sql.toStdString());
        bool sql_exec_ok = (bool)qresult;
        long long qr_rows = qresult ? (long long)qresult->RowCount() : -1;
        QString qr_lasterr;
        // 防御性：用同一个 connection 再查一次 output 是否存在；失败则说明 SQL 其实报错了（DuckDB 有时 CREATE TABLE AS 失败不会抛异常，只在 Result 里体现）
        try {
            auto probe = con.Query(QString("SELECT COUNT(*) FROM %1").arg(output_table).toStdString());
            if (!probe || probe->RowCount()==0) sql_exec_ok = false;
            else {
                auto pc = probe->GetValue(0,0);
                // 只要能查询就认为 OK（行数为 0 也行）
            }
        } catch (const std::exception &pe) {
            sql_exec_ok = false;
            qr_lasterr = QString::fromStdString(pe.what());
        } catch (...) {
            sql_exec_ok = false;
        }
        if (!sql_exec_ok) {
            const QString err = QStringLiteral("DuckDB Calc SQL 执行失败（输出表未生成）");
            qCritical() << "[CalcBatch FAIL]" << err << " enable_avg_weight=" << enable_avg_weight
                        << " output_table=" << output_table
                        << " qresult.valid=" << (bool)qresult
                        << " qresult.RowCount=" << qr_rows
                        << " probe.exception=" << qr_lasterr;
            // 调试：写 SQL 到 /tmp 方便 duckdb CLI 复现
            QFile dump(QStringLiteral("/tmp/_calc_debug_%1.sql").arg(output_table));
            if (dump.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                dump.write(sql.toUtf8());
                dump.close();
                qCritical() << "    调试用 SQL 文件：" << QFileInfo(dump).absoluteFilePath();
            }
            // 打印关键片段：__lajz_global 前 800 字符，以及 avg_weight_pool_in（进池判断）前后 2000 字符
            int idx = sql.indexOf("avg_weight_pool_in AS (");
            qCritical() << "    === 调试片段 1/2（SQL 前 2000 字符）===\n" << sql.left(2000);
            if (idx >= 0) {
                qCritical() << "    === 调试片段 2/2（avg_weight_pool_in 进池判断 前后 2500 字符）===\n"
                            << sql.mid(qMax(0, idx-300), 3200);
            } else {
                qCritical() << "    （SQL 中找不到 avg_weight_pool_in AS，打印后 3000 字符）\n" << sql.right(3000);
            }
            emit CalcFinished(false, err);
            return false;
        }

        // 诊断：当输出为空或输入行丢失时打印 enable_avg_weight + SQL 片段，便于定位门控/进池判定问题
    try {
        auto chk = con.Query(QString("SELECT COUNT(*) FROM %1").arg(output_table).toStdString());
        if (chk && chk->RowCount() > 0) {
            long long cnt = chk->GetValue(0,0).GetValue<long long>();
            auto chki = con.Query(QString("SELECT COUNT(*) FROM %1").arg(input_table).toStdString());
            long long in_cnt = (chki && chki->RowCount()>0) ? chki->GetValue(0,0).GetValue<long long>() : -1;
            if (cnt == 0) {
                qWarning() << "[CalcBatch WARN] output=" << output_table << "ROWS=" << cnt
                           << "（input_rows=" << in_cnt << "，enable_avg_weight=" << enable_avg_weight << "）";
                qWarning() << "                SQL 前 2000 字符：\n" << sql.left(2000);
                qWarning() << "                SQL 后 4000 字符：\n" << sql.right(4000);
            } else if (in_cnt > 0 && cnt != in_cnt && cnt != 0) {
                qDebug() << "[CalcBatch] rows：input=" << in_cnt << " output=" << cnt
                         << "（enable_avg_weight=" << enable_avg_weight << "）";
            }
        } else {
            // 输出表甚至都不存在（CREATE TABLE 静默失败的情况）— 用第二个独立 con 再创建一次临时表试，尝试捕获原始 DuckDB SQL 错误
            qCritical() << "[CalcBatch FAIL] SELECT COUNT 查询空表 " << output_table
                        << " enable_avg_weight=" << enable_avg_weight;
            qCritical() << "    尝试在 /tmp/_t8_debug.sql 写入 SQL 后独立执行，检查 DuckDB 原错误:";
            QFile dump(QStringLiteral("/tmp/_calc_debug_%1.sql").arg(output_table));
            if (dump.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                dump.write(sql.toUtf8());
                dump.close();
                qCritical() << "    SQL 已写入：" << QFileInfo(dump).absoluteFilePath();
            }
            qCritical() << "    SQL(前2000字符):\n" << sql.left(2000);
            qCritical() << "    SQL(后4000字符):\n" << sql.right(4000);
            emit CalcFinished(false, QStringLiteral("输出表未生成，请查看日志/Debug SQL文件"));
            return false;
        }
    } catch(const std::exception &chk_e) {
        qCritical() << "[CalcBatch CHECK] exception: " << chk_e.what();
        emit CalcFinished(false, QString::fromStdString(chk_e.what()));
        return false;
    } catch(...) {}

        emit ProgressChanged(100);
        emit CalcFinished(true, QStringLiteral("计算完成"));
        return true;
    } catch (const std::exception &e) {
        qCritical() << "CalcBatch failed:" << e.what();
        emit CalcFinished(false, QString::fromStdString(e.what()));
        return false;
    } catch (...) {
        qCritical() << "CalcBatch failed: unknown exception";
        emit CalcFinished(false, QStringLiteral("未知错误，请查看控制台日志"));
        return false;
    }
}

bool CalcService::CalcFromFile(const QString &input_file,
                               const QString &output_file,
                               bool enable_avg_weight) {
    try {
        emit ProgressChanged(0);
        auto &cfg = core::AppConfig::Instance();
        auto &dbm = db::DuckDBManager::Instance();

        // DEBUG-01 关键修复：每次文件计算前强制重新加载 SQLite → DuckDB 规则表
        //   保证用户在「规则设置/客户设置」里刚保存的拉均重合同、参数立即生效
        try {
            dbm.ReloadRules(cfg.GetRulesDbPath());
        } catch (const std::exception &re) {
            qWarning() << "ReloadRules skipped:" << re.what();
        }

        QFileInfo fi(input_file);
        QString input_table = "_input_tmp";
        QString output_table = "_output_tmp";

        emit ProgressChanged(5);
        if (!dbm.ImportFromFile(input_table, input_file)) {
            emit CalcFinished(false, QStringLiteral("文件导入失败"));
            return false;
        }

        emit ProgressChanged(25);
        QString normalized_table = NormalizeColumns(input_table);
        if (normalized_table.isEmpty()) {
            emit CalcFinished(false, QStringLiteral("列归一化失败，请检查列名"));
            return false;
        }

        emit ProgressChanged(45);
        // 透传 enable_avg_weight 开关到 CalcBatch → BuildCalcSQL
        if (!CalcBatch(normalized_table, output_table, enable_avg_weight)) {
            return false;
        }

        emit ProgressChanged(85);
        if (!dbm.ExportToFile(output_table, output_file)) {
            QString native_path = QDir::toNativeSeparators(QFileInfo(output_file).absoluteFilePath());
            emit CalcFinished(false,
                QStringLiteral("结果导出失败\n路径：%1\n\n请检查：\n1. 输出目录是否可写\n2. 文件是否被 Excel/WPS 占用\n3. 磁盘是否已满")
                    .arg(native_path));
            return false;
        }

        emit ProgressChanged(100);
        emit CalcFinished(true, QStringLiteral("计算完成"));
        return true;
    } catch (const std::exception &e) {
        qCritical() << "CalcFromFile failed:" << e.what();
        emit CalcFinished(false, QString::fromStdString(e.what()));
        return false;
    }
}

QString CalcService::BuildCalcSQL(const QString &input_table,
                                  const QString &output_table,
                                  bool enable_avg_weight) {
    // ====== 拉均重 CTE 区块：enable 时用真实逻辑；disable 时全置空 ======
    //   这样下游 fuel_surcharge_calc / final_merged / SELECT 输出列 完全无需改动
    const QString LAJZ_CTES = enable_avg_weight ? QStringLiteral(R"SQL(
-- =====================================================================
-- Step5-2：拉均重合同匹配 avg_weight_templates + avg_weight_zones
--   ① 先筛当前生效合同（is_active=1 + 日期窗口 + 每个绑定(template_id,avg_tpl_id)取version最大）
--   ② 每行：优先客户级 cust_avg_weight_tpl_id 外键 → 模板级 template_id 同名绑定
-- =====================================================================
avg_weight_active AS (
    SELECT a.*
    FROM avg_weight_templates a
    INNER JOIN (
        SELECT COALESCE(NULLIF(template_id,''), '*')            AS bind_template_id,
               COALESCE(NULLIF(avg_tpl_id,''), '')              AS bind_avg_tpl_id,
               MAX(version)                                     AS max_ver
        FROM avg_weight_templates
        WHERE is_active = 1
          AND CAST(effective_from AS DATE) <= CURRENT_DATE
          AND (effective_to IS NULL OR CAST(effective_to AS VARCHAR)='' OR CAST(effective_to AS DATE) >= CURRENT_DATE)
        GROUP BY 1, 2
    ) k ON (
              (k.bind_avg_tpl_id <> '' AND a.avg_tpl_id = k.bind_avg_tpl_id)
           OR (k.bind_avg_tpl_id = '' AND COALESCE(NULLIF(a.template_id,''), '*') = k.bind_template_id)
          ) AND a.version = k.max_ver
    WHERE a.is_active = 1
),
avg_weight_joined AS (
    SELECT
        bfc.*,
        COALESCE(
            (SELECT aw.avg_tpl_id  FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.avg_tpl_id  FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1)
        )                                                      AS lajz_avg_tpl_id,
        COALESCE(
            (SELECT aw.contract_no  FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.contract_no  FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), ''
        )                                                      AS lajz_contract_no,
        COALESCE(
            (SELECT aw.avg_pool_max_kg FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.avg_pool_max_kg FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 1.0
        )                                                      AS lajz_pool_max_kg,
        COALESCE(
            (SELECT aw.base_avg_kg FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.base_avg_kg FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 0.3
        )                                                      AS lajz_base_avg_kg,
        COALESCE(
            (SELECT aw.avg_fee_cap_kg FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.avg_fee_cap_kg FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 1.0
        )                                                      AS lajz_fee_cap_kg,
        COALESCE(
            (SELECT aw.base_fee FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.base_fee FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 2.7
        )                                                      AS lajz_base_fee,
        COALESCE(
            (SELECT aw.step_kg FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.step_kg FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 0.1
        )                                                      AS lajz_step_kg,
        COALESCE(
            (SELECT aw.step_fee FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.step_fee FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 0.2
        )                                                      AS lajz_step_fee,
        COALESCE(
            (SELECT aw.min_tickets FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.min_tickets FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 50
        )                                                      AS lajz_min_tickets,
        COALESCE(
            (SELECT aw.over_cap_mode FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.over_cap_mode FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 0
        )::INT                                                  AS lajz_over_cap_mode,
        COALESCE(
            (SELECT aw.reuse_zone_groups FROM avg_weight_active aw
              WHERE bfc.cust_avg_weight_tpl_id IS NOT NULL
                AND aw.avg_tpl_id = bfc.cust_avg_weight_tpl_id LIMIT 1),
            (SELECT aw.reuse_zone_groups FROM avg_weight_active aw
              WHERE COALESCE(NULLIF(aw.template_id,''), '*') = bfc.template_id LIMIT 1), 1
        )::INT                                                  AS lajz_reuse_zone_groups
    FROM base_fee_calc bfc
),
-- Step5-3 L2：进池判定（命中合同 + 单件重 ≤ avg_pool_max_kg + 省在白名单）
avg_weight_pool_in AS (
    SELECT
        awj.*,
        CASE
            WHEN awj.lajz_avg_tpl_id IS NULL OR TRIM(awj.lajz_avg_tpl_id)='' THEN FALSE
            WHEN awj.charge_weight > awj.lajz_pool_max_kg                     THEN FALSE
            WHEN awj.lajz_reuse_zone_groups = 1 THEN
                (
                   awj.group_code IS NOT NULL
                   AND (
                         -- 子场景 A-1：本合同完全没勾选任何「模板级分区组」 → 视为「全部开放」（老合同/通配），省只要在阶梯表里有 group_code 就进
                         NOT EXISTS (SELECT 1 FROM avg_weight_zone_tpl_groups gt0
                                     WHERE gt0.avg_tpl_id = awj.lajz_avg_tpl_id)
                         OR
                         -- 子场景 A-2：有勾选分区组 → 严格命中分区组：gt.group_code = awj.group_code
                         --   注意：gt.template_id 字段仅是 UI 展示用（标记该组来自哪个模板），不参与过滤！
                         --         因为同一个合同可能给多个客户共用，勾选哪组就哪组生效，不应和订单归属模板再比一次
                         EXISTS (SELECT 1 FROM avg_weight_zone_tpl_groups gt
                                 WHERE gt.avg_tpl_id  = awj.lajz_avg_tpl_id
                                   AND gt.group_code  = awj.group_code)
                   )
                   AND
                   -- 公共：该省未在排除省名单（分区一致 + 省份规范化后相同）
                   --   注意：ex.template_id 字段同样仅 UI 展示用（标记该组来自哪个模板），不参与过滤
                   NOT EXISTS (SELECT 1 FROM avg_weight_zone_excludes ex
                               WHERE ex.avg_tpl_id  = awj.lajz_avg_tpl_id
                                 AND ex.group_code  = awj.group_code
                                 AND REGEXP_REPLACE(ex.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                                   = awj.norm_province)
                )
            WHEN awj.lajz_reuse_zone_groups = 0 AND EXISTS(
                     SELECT 1 FROM avg_weight_zones az
                     WHERE az.avg_tpl_id = awj.lajz_avg_tpl_id
                       AND REGEXP_REPLACE(az.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                         = awj.norm_province
                 ) THEN TRUE
            ELSE FALSE
        END AS lajz_in_pool_raw
    FROM avg_weight_joined awj
),
avg_weight_pool_b_zone AS (
    SELECT p.order_id,
           (SELECT az.zone_code FROM avg_weight_zones az
             WHERE az.avg_tpl_id = p.lajz_avg_tpl_id
               AND REGEXP_REPLACE(az.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                 = p.norm_province LIMIT 1) AS b_zone_code
    FROM avg_weight_pool_in p
    WHERE p.lajz_reuse_zone_groups = 0
),
avg_weight_pool_b_zone_cnt AS (
    SELECT avg_tpl_id, COUNT(DISTINCT zone_code) AS zone_cnt
    FROM avg_weight_zones
    GROUP BY 1
),
avg_weight_pool_agg AS (
    SELECT
        COALESCE(NULLIF(pi.lajz_avg_tpl_id,''), '__no_contract__')             AS pool_key,
        CASE
           WHEN COALESCE(pi.lajz_reuse_zone_groups, 1) = 1
               THEN COALESCE(NULLIF(pi.group_code,''), '__default__')
           ELSE COALESCE(
                 CASE WHEN COALESCE(zc.zone_cnt, 0) >= 2 THEN bz.b_zone_code END,
                 '__global__'
           )
        END                                                                     AS pool_subkey,
        COUNT(*) FILTER (WHERE pi.lajz_in_pool_raw = TRUE)                     AS pool_n,
        AVG(pi.charge_weight) FILTER (WHERE pi.lajz_in_pool_raw = TRUE)        AS pool_avg_kg,
        MAX(pi.lajz_min_tickets)                                               AS pool_min_required
    FROM avg_weight_pool_in pi
    LEFT JOIN avg_weight_pool_b_zone     bz ON bz.order_id = pi.order_id
    LEFT JOIN avg_weight_pool_b_zone_cnt zc ON zc.avg_tpl_id = pi.lajz_avg_tpl_id
    GROUP BY 1, 2
),
avg_weight_pool_final AS (
    SELECT p.*,
           CASE WHEN pool_n < pool_min_required THEN FALSE ELSE TRUE END AS pool_eligible
    FROM avg_weight_pool_agg p
),
lajz_final AS (
    SELECT
        p.*,
        COALESCE(fp.pool_avg_kg, 0.0)                              AS lajz_pool_avg_kg,
        COALESCE(fp.pool_n, 0)                                     AS lajz_pool_n,
        CASE WHEN fp.pool_eligible = TRUE AND p.lajz_in_pool_raw = TRUE
                AND (
                    (p.lajz_over_cap_mode = 1 AND COALESCE(fp.pool_avg_kg, 0) <= p.lajz_fee_cap_kg)
                    OR p.lajz_over_cap_mode = 0
                )
             THEN TRUE ELSE FALSE END                              AS lajz_used,
        CASE WHEN fp.pool_eligible = TRUE AND p.lajz_in_pool_raw = TRUE
             THEN ROUND(
                  p.lajz_base_fee +
                  LEAST(
                      GREATEST(0, CEIL((COALESCE(fp.pool_avg_kg,0) - p.lajz_base_avg_kg)
                                 / NULLIF(p.lajz_step_kg,0))) * p.lajz_step_fee,
                      GREATEST(0, CEIL((p.lajz_fee_cap_kg - p.lajz_base_avg_kg)
                                 / NULLIF(p.lajz_step_kg,0))) * p.lajz_step_fee
                  ), 2)
             ELSE NULL END                                         AS lajz_fee_per_ticket
    FROM avg_weight_pool_in p
    LEFT JOIN avg_weight_pool_b_zone     lbz ON lbz.order_id = p.order_id
    LEFT JOIN avg_weight_pool_b_zone_cnt lzc ON lzc.avg_tpl_id = p.lajz_avg_tpl_id
    LEFT JOIN avg_weight_pool_final fp
           ON fp.pool_key    = COALESCE(NULLIF(p.lajz_avg_tpl_id,''), '__no_contract__')
          AND fp.pool_subkey = CASE
                    WHEN COALESCE(p.lajz_reuse_zone_groups, 1) = 1
                        THEN COALESCE(NULLIF(p.group_code,''), '__default__')
                    ELSE COALESCE(
                          CASE WHEN COALESCE(lzc.zone_cnt,0) >= 2 THEN lbz.b_zone_code END,
                          '__global__'
                    ) END
),
)SQL") : QStringLiteral(R"SQL(
-- =====================================================================
-- Step5-2~5-4：拉均重总开关未启用（enable_avg_weight=FALSE）
--   全量列置空，占位直通，保证下游 CTE / 输出 SELECT 列结构完全一致
-- =====================================================================
avg_weight_active AS (SELECT * FROM avg_weight_templates WHERE 1=0),
avg_weight_joined AS (
    SELECT
        bfc.*,
        NULL::VARCHAR                                           AS lajz_avg_tpl_id,
        ''::VARCHAR                                             AS lajz_contract_no,
        1.0::DOUBLE                                             AS lajz_pool_max_kg,
        0.3::DOUBLE                                             AS lajz_base_avg_kg,
        1.0::DOUBLE                                             AS lajz_fee_cap_kg,
        2.7::DOUBLE                                             AS lajz_base_fee,
        0.1::DOUBLE                                             AS lajz_step_kg,
        0.2::DOUBLE                                             AS lajz_step_fee,
        50::BIGINT                                              AS lajz_min_tickets,
        0::INT                                                  AS lajz_over_cap_mode,
        1::INT                                                  AS lajz_reuse_zone_groups
    FROM base_fee_calc bfc
),
avg_weight_pool_in AS (
    SELECT awj.*, FALSE::BOOLEAN AS lajz_in_pool_raw FROM avg_weight_joined awj
),
avg_weight_pool_b_zone AS (SELECT order_id, NULL::VARCHAR AS b_zone_code FROM avg_weight_pool_in WHERE 1=0),
avg_weight_pool_b_zone_cnt AS (SELECT avg_tpl_id, 0::BIGINT AS zone_cnt FROM avg_weight_zones WHERE 1=0),
avg_weight_pool_agg AS (
    SELECT '__no_contract__'::VARCHAR AS pool_key, '__default__'::VARCHAR AS pool_subkey,
           0::BIGINT AS pool_n, 0::DOUBLE AS pool_avg_kg, 50::BIGINT AS pool_min_required
    WHERE 1=0
),
avg_weight_pool_final AS (SELECT p.*, FALSE::BOOLEAN AS pool_eligible FROM avg_weight_pool_agg p WHERE 1=0),
lajz_final AS (
    SELECT p.*,
           0.0::DOUBLE AS lajz_pool_avg_kg,
           0::BIGINT   AS lajz_pool_n,
           FALSE::BOOLEAN           AS lajz_used,
           NULL::DOUBLE             AS lajz_fee_per_ticket
    FROM avg_weight_pool_in p
),
)SQL");

    const QString enable_flag_str = enable_avg_weight ? "TRUE" : "FALSE";

    // ========== 输出列构造：enable_avg_weight=FALSE 时完全不输出拉均重列 ==========
    const QString LAJZ_OUTPUT_COLS = enable_avg_weight ? QStringLiteral(R"SQL(,
    CASE WHEN lajz_in_pool_raw THEN '是' ELSE '否' END              AS "是否命中拉均重池",
    CASE WHEN lajz_used        THEN '是' ELSE '否' END              AS "是否采用拉均重价",
    COALESCE(NULLIF(lajz_contract_no,''), '')                       AS "拉均重合同编号",
    ROUND(lajz_pool_avg_kg, 5)                                      AS "拉均重池平均重量(KG)",
    ROUND(lajz_pool_max_kg, 3)                                      AS "拉均重池进池上限(KG)",
    ROUND(lajz_base_avg_kg, 3)                                      AS "拉均重约定基准(KG)",
    ROUND(lajz_fee_cap_kg, 3)                                       AS "拉均重加价封顶(KG)",
    ROUND(COALESCE(lajz_fee_per_ticket, 0), 2)                      AS "拉均重单票基础价(元)",
    ROUND(COALESCE(lajz_save_vs_tier, 0), 2)                        AS "拉均重相比阶梯节省(元/票)",
    COALESCE(lajz_avg_tpl_id, '')                                   AS "诊断_命中的拉均重合同ID",
    CASE WHEN lajz_in_pool_raw IS TRUE THEN '是' ELSE '否' END      AS "诊断_是否命中拉均重池",
    COALESCE(lajz_pool_n, 0)                                        AS "诊断_有效进池票数",
    CASE WHEN lajz_used IS TRUE THEN '是' ELSE '否' END             AS "诊断_最终是否采用拉均重"
)SQL") : QStringLiteral("");

    qCritical() << "[DIAG] BuildCalcSQL called: enable_avg_weight =" << enable_avg_weight
                << "，enable_flag_str =" << enable_flag_str;

    // ========== 主 SQL：__lajz_global 的标志直接写死（不再靠 %4 arg，防止占位符顺序错位）==========
    const QString sql_head = QString(R"SQL(
CREATE OR REPLACE TABLE %1 AS
WITH
__lajz_global AS (SELECT )SQL") + enable_flag_str + QStringLiteral(R"SQL(::BOOLEAN AS enable_avg_weight),
input_data AS (
    SELECT
        COALESCE(order_id, '') AS order_id,
        COALESCE(dest_province, '') AS dest_province,
        COALESCE(dest_city, '') AS dest_city,
        COALESCE(weight, 0) AS weight,
        COALESCE(vol_weight, 0) AS vol_weight,
        COALESCE(customer_id, '') AS customer_id,
        CASE
            WHEN COALESCE(vol_weight, 0) > 0 AND vol_weight > weight THEN vol_weight
            ELSE weight
        END AS raw_charge_weight
    FROM %2
),
customer_template_lookup AS (
    SELECT
        i.*,
        COALESCE(c.default_template, 'zto_standard') AS template_id,
        COALESCE(NULLIF(TRIM(c.cust_rounding_mode), ''), NULL)   AS cust_rounding_mode,
        CASE WHEN COALESCE(c.cust_additional_unit, 0) > 0
             THEN c.cust_additional_unit ELSE NULL END           AS cust_additional_unit,
        CASE WHEN COALESCE(c.cust_vol_divisor, 0) > 0
             THEN c.cust_vol_divisor ELSE NULL END               AS cust_vol_divisor,
        COALESCE(NULLIF(TRIM(c.avg_weight_tpl_id), ''), NULL)    AS cust_avg_weight_tpl_id
    FROM input_data i
    LEFT JOIN customers c ON (TRIM(c.customer_id) = TRIM(i.customer_id) OR TRIM(c.customer_name) = TRIM(i.customer_id))
),
template_info AS (
    SELECT
        ctl.*,
        COALESCE(ft.default_no_weight_fee, 0) AS default_no_weight_fee,
        COALESCE(ctl.cust_rounding_mode,
                 NULLIF(ft.tpl_rounding_mode, ''),
                 'ceil_0_1kg')                                                 AS eff_rounding_mode,
        COALESCE(ctl.cust_additional_unit,
                 NULLIF(ft.tpl_additional_unit, 0),
                 NULLIF(ft.additional_unit, 0),
                 1.0)                                                        AS eff_additional_unit,
        COALESCE(ctl.cust_vol_divisor,
                 NULLIF(ft.tpl_vol_divisor, 0),
                 CAST(NULLIF(ft.vol_weight_ratio, 0) AS INTEGER),
                 6000)                                                       AS eff_vol_divisor
    FROM customer_template_lookup ctl
    LEFT JOIN freight_templates ft ON ft.template_id = ctl.template_id
),
charge_weight_rounded AS (
    SELECT
        ti.*,
        CASE COALESCE(ti.eff_rounding_mode, 'ceil_0_1kg')
            WHEN 'ceil_1kg'       THEN CEIL(ti.raw_charge_weight)
            WHEN 'ceil_0_5kg'     THEN CEIL(ti.raw_charge_weight * 2.0) / 2.0
            WHEN 'ceil_0_1kg'     THEN CEIL(ti.raw_charge_weight * 10.0) / 10.0
            WHEN 'round_0_1kg'    THEN ROUND(ti.raw_charge_weight * 10.0) / 10.0
            WHEN 'floor_no_round' THEN ti.raw_charge_weight
            ELSE CEIL(ti.raw_charge_weight * 10.0) / 10.0
        END AS charge_weight
    FROM template_info ti
),
matched_zone AS (
    SELECT
        cwr.*,
        REGEXP_REPLACE(cwr.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') AS norm_province,
        zgp.group_code,
        zg.group_name
    FROM charge_weight_rounded cwr
    LEFT JOIN zone_group_provinces zgp
        ON zgp.template_id = cwr.template_id
       AND REGEXP_REPLACE(zgp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
         = REGEXP_REPLACE(cwr.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
    LEFT JOIN zone_groups zg
        ON zg.template_id = zgp.template_id
       AND zg.group_code = zgp.group_code
),
matched_tier AS (
    SELECT
        mz.*,
        tp.tier_code,
        tp.tier_name,
        tp.first_weight,
        tp.first_price,
        tp.additional_unit,
        tp.additional_price,
        tp.sort_order
    FROM matched_zone mz
    LEFT JOIN tiered_pricing tp
        ON tp.template_id = mz.template_id
       AND tp.group_code = mz.group_code
       AND mz.charge_weight > tp.min_weight
       AND mz.charge_weight <= tp.max_weight
),
tier_max AS (
    SELECT template_id, group_code,
           first_weight AS max_first_weight,
           first_price AS max_first_price,
           additional_unit AS max_additional_unit,
           additional_price AS max_additional_price,
           max_weight AS max_tier_weight
    FROM tiered_pricing t1
    WHERE sort_order = (SELECT MAX(sort_order) FROM tiered_pricing t2
                         WHERE t2.template_id = t1.template_id
                           AND t2.group_code = t1.group_code)
),
base_fee_calc AS (
    SELECT
        mt.*,
        CASE
            WHEN mt.charge_weight <= 0 OR mt.charge_weight IS NULL
                THEN mt.default_no_weight_fee
            WHEN mt.group_code IS NULL THEN 0
            WHEN mt.charge_weight <= mt.first_weight THEN mt.first_price
            WHEN mt.tier_code IS NOT NULL
                THEN mt.first_price
                     + GREATEST(0, CEIL(
                         (mt.charge_weight - mt.first_weight)
                         / GREATEST(COALESCE(mt.eff_additional_unit, mt.additional_unit, 1.0), 0.0001)
                     )) * mt.additional_price
            ELSE
                COALESCE(tm.max_first_price, 0) +
                GREATEST(0, CEIL(
                    (mt.charge_weight - COALESCE(tm.max_first_weight, 0))
                    / GREATEST(COALESCE(mt.eff_additional_unit, tm.max_additional_unit, 1.0), 0.0001)
                )) * COALESCE(tm.max_additional_price, 0)
        END AS base_fee__tier,
        NULL::DOUBLE AS __lajz_stub
    FROM matched_tier mt
    LEFT JOIN tier_max tm
        ON tm.template_id = mt.template_id
       AND tm.group_code = mt.group_code
),
)SQL");

    // ========== 拉均重 + 燃油/偏远/策略/final_merged + SELECT 输出列拼接 ==========
    const QString sql_tail = QStringLiteral(R"SQL(
fuel_surcharge_calc AS (
    SELECT
        lf.*,
        COALESCE((
            SELECT fs.rate
            FROM fuel_surcharge fs
            WHERE fs.template_id = lf.template_id
              AND fs.is_active = 1
              AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                         WHERE template_id = lf.template_id
                                           AND is_active = 1
                                           AND effective_date <= CURRENT_DATE)
            LIMIT 1
        ), (
            SELECT fs.rate
            FROM fuel_surcharge fs
            WHERE fs.template_id = '*'
              AND fs.is_active = 1
              AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                         WHERE template_id = '*'
                                           AND is_active = 1
                                           AND effective_date <= CURRENT_DATE)
            LIMIT 1
        ), 0) * lf.base_fee__tier AS fuel_surcharge
    FROM lajz_final lf
),
remote_area_calc AS (
    SELECT
        fsc.*,
        COALESCE(NULLIF((
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = fsc.template_id
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND REGEXP_REPLACE(ra.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                       = REGEXP_REPLACE(fsc.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                   AND (ra.city IS NULL OR ra.city = ''
                        OR REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                           = REGEXP_REPLACE(fsc.dest_city, '(市|区|县|旗|自治县|林区)$', ''))
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                       = REGEXP_REPLACE(fsc.dest_city, '(市|区|县|旗|自治县|林区)$', '')
                   AND (ra.district IS NULL OR ra.district = ''))
              )
        ), 0), (
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = '*'
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND REGEXP_REPLACE(ra.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                       = REGEXP_REPLACE(fsc.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                   AND (ra.city IS NULL OR ra.city = ''
                        OR REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                           = REGEXP_REPLACE(fsc.dest_city, '(市|区|县|旗|自治县|林区)$', ''))
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND REGEXP_REPLACE(ra.city, '(市|区|县|旗|自治县|林区)$', '')
                       = REGEXP_REPLACE(fsc.dest_city, '(市|区|县|旗|自治县|林区)$', '')
                   AND (ra.district IS NULL OR ra.district = ''))
              )
        ), 0) AS remote_surcharge
    FROM fuel_surcharge_calc fsc
),
strategy_surcharge_calc AS (
    SELECT
        rac.*,
        COALESCE((
            SELECT SUM(
                CASE s.strategy_type
                    WHEN 'fixed' THEN s.amount
                    WHEN 'percentage' THEN rac.base_fee__tier * s.amount
                    WHEN 'per_weight' THEN rac.charge_weight * s.amount
                    WHEN 'per_volume' THEN COALESCE(NULLIF(rac.vol_weight, 0), rac.charge_weight) * s.amount
                    ELSE 0
                END
            )
            FROM surcharge_strategies s
            LEFT JOIN surcharge_provinces sp ON sp.strategy_id = s.strategy_id
            LEFT JOIN surcharge_customers sc ON sc.strategy_id = s.strategy_id
            LEFT JOIN surcharge_date_ranges sd ON sd.strategy_id = s.strategy_id
            WHERE s.is_active = 1
              AND (s.strategy_scope IN ('global', 'template') OR s.template_id = rac.template_id)
              AND (
                  s.strategy_scope IN ('global', 'template')
                  OR (s.strategy_scope = 'province'
                      AND REGEXP_REPLACE(sp.province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                        = REGEXP_REPLACE(rac.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', ''))
                  OR (s.strategy_scope = 'customer' AND sc.customer_id = rac.customer_id)
              )
              AND (sd.strategy_id IS NULL
                   OR (CURRENT_DATE BETWEEN sd.start_date AND sd.end_date))
              AND (s.min_weight IS NULL OR rac.charge_weight >= s.min_weight)
              AND (s.max_weight IS NULL OR s.max_weight = 0 OR rac.charge_weight <= s.max_weight)
        ), 0) AS strategy_surcharge
    FROM remote_area_calc rac
),
final_merged AS (
    SELECT ssc.*,
        CASE WHEN (SELECT enable_avg_weight FROM __lajz_global) = TRUE
                  AND ssc.lajz_used = TRUE
             THEN COALESCE(ssc.lajz_fee_per_ticket, ssc.base_fee__tier)
             ELSE ssc.base_fee__tier END                    AS base_fee,
        CASE WHEN (SELECT enable_avg_weight FROM __lajz_global) = TRUE
                  AND ssc.lajz_used = TRUE
             THEN ROUND(COALESCE(ssc.base_fee__tier, 0)
                      - COALESCE(ssc.lajz_fee_per_ticket, 0), 2)
             ELSE 0 END                                      AS lajz_save_vs_tier
    FROM strategy_surcharge_calc ssc
)

SELECT
    order_id                                                AS "订单号",
    customer_id                                             AS "客户编号",
    dest_province                                           AS "目的省份",
    dest_city                                               AS "目的城市",
    weight                                                  AS "实际重量(KG)",
    vol_weight                                              AS "体积重量(KG)",
    charge_weight                                           AS "计费重量(KG)",
    ROUND(base_fee, 2)                                      AS "基础运费",
    ROUND(fuel_surcharge, 2)                                AS "燃油附加费",
    ROUND(remote_surcharge, 2)                              AS "地区加价",
    ROUND(strategy_surcharge, 2)                            AS "其他附加费",
    ROUND(base_fee + fuel_surcharge + remote_surcharge + strategy_surcharge, 2) AS "总运费",
    'CNY'                                                   AS "币种"
)SQL")
        + LAJZ_OUTPUT_COLS
        + QStringLiteral("\nFROM final_merged\n");

    QString sql = (sql_head + LAJZ_CTES + sql_tail).arg(output_table, input_table);

    qCritical() << "[DIAG] 最终 SQL __lajz_global 附近 300 字符：\n"
                << sql.mid(sql.indexOf("__lajz_global"), 300);

    return sql;
}

QStringList CalcService::GetTableColumns(const QString &table_name) {
    QStringList columns;
    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();
        // 用 DESCRIBE 代替 information_schema，更可靠
        auto res = con.Query(
            QString("DESCRIBE SELECT * FROM %1").arg(table_name).toStdString());
        for (idx_t i = 0; i < res->RowCount(); i++) {
            columns << QString::fromStdString(res->GetValue(0, i).ToString());
        }
    } catch (const std::exception &e) {
        qCritical() << "GetTableColumns failed:" << e.what();
    }
    return columns;
}

QString CalcService::NormalizeColumns(const QString &input_table) {
    QStringList actual_cols = GetTableColumns(input_table);
    if (actual_cols.isEmpty()) {
        qCritical() << "No columns found in table:" << input_table;
        return QString();
    }

    QMap<QString, QString> col_map = AutoMapColumns(actual_cols);

    // 检查必须有省份和重量列
    if (!col_map.contains("dest_province") || !col_map.contains("weight")) {
        qCritical() << "Auto-mapping failed: required columns missing";
        return QString();
    }

    return CreateNormalizedTable(input_table, col_map);
}

QMap<QString, QString> CalcService::AutoMapColumns(const QStringList &actual_cols) {
    auto &cfg = core::AppConfig::Instance();
    const auto eff_map = cfg.GetEffectiveMappingKeywords();
    const auto &order = core::AppConfig::StandardColumnOrder();

    struct ColumnMapping {
        QString standard_name;
        QStringList keywords;
    };
    QList<ColumnMapping> mappings;
    mappings.reserve(order.size());
    for (const QString &std_name : order) {
        ColumnMapping m{std_name, eff_map.value(std_name)};
        mappings.append(std::move(m));
    }

    QMap<QString, QString> col_map;
    for (const auto &m : mappings) {
        for (const auto &actual : actual_cols) {
            QString actual_lower = actual.toLower();
            for (const auto &kw : m.keywords) {
                if (actual_lower == kw.toLower()) {
                    if (!col_map.contains(m.standard_name)) {
                        col_map[m.standard_name] = actual;
                        qDebug() << "  Mapped(exact):" << m.standard_name << "<-" << actual;
                    }
                    break;
                }
            }
        }
    }
    for (const auto &m : mappings) {
        if (col_map.contains(m.standard_name)) continue;
        for (const auto &actual : actual_cols) {
            QString actual_lower = actual.toLower();
            for (const auto &kw : m.keywords) {
                if (actual_lower.contains(kw.toLower())) {
                    if (!col_map.contains(m.standard_name)) {
                        col_map[m.standard_name] = actual;
                        qDebug() << "  Mapped(substring):" << m.standard_name << "<-" << actual;
                    }
                    break;
                }
            }
        }
    }
    return col_map;
}

void CalcService::RememberMapping(const QMap<QString, QString> &confirmed,
                                  const QStringList &actual_cols) {
    QSet<QString> actual_set;
    for (const QString &c : actual_cols) actual_set.insert(c);
    auto &cfg = core::AppConfig::Instance();
    for (auto it = confirmed.begin(); it != confirmed.end(); ++it) {
        const QString &std_col = it.key();
        const QString &actual = it.value();
        if (actual.isEmpty()) continue;
        if (!actual_set.contains(actual)) continue;
        if (actual.compare(std_col, Qt::CaseInsensitive) == 0) continue;
        cfg.AddMappingKeyword(std_col, actual);
    }
}

QString CalcService::CreateNormalizedTable(const QString &input_table,
                                            const QMap<QString, QString> &col_map) {
    QStringList select_parts;
    // 如果用户 Excel 没有某列（col_map 中为空字符串），用常量空串/0 兜底，不要 COALESCE("") 触发标识符错误
    auto col_or_literal_str = [&](const QString &key) {
        QString c = col_map.value(key, "");
        if (c.isEmpty()) return QString("''");
        return QString("COALESCE(\"%1\", '')").arg(c);
    };
    auto col_or_literal_num = [&](const QString &key) {
        QString c = col_map.value(key, "");
        if (c.isEmpty()) return QString("0.0");
        // 用 TRY_CAST 处理空字符串/非数字字符（COALESCE 不能捕获 CAST 错误）
        return QString("COALESCE(TRY_CAST(\"%1\" AS DOUBLE), 0)").arg(c);
    };

    select_parts << QString("%1 AS order_id").arg(col_or_literal_str("order_id"));
    select_parts << QString("%1 AS customer_id").arg(col_or_literal_str("customer_id"));
    select_parts << QString("COALESCE(\"%1\", '') AS dest_province").arg(col_map.value("dest_province"));
    select_parts << QString("%1 AS dest_city").arg(col_or_literal_str("dest_city"));
    select_parts << QString("%1 AS weight").arg(col_or_literal_num("weight"));
    select_parts << QString("%1 AS vol_weight").arg(col_or_literal_num("vol_weight"));

    QString normalized_table = "_input_normalized";
    QString sql = QString("CREATE OR REPLACE TABLE %1 AS SELECT %2 FROM %3")
        .arg(normalized_table, select_parts.join(", "), input_table);

    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();
        con.Query(sql.toStdString());

        auto cnt_res = con.Query(QString("SELECT COUNT(*) FROM %1").arg(normalized_table).toStdString());
        int64_t count = cnt_res->GetValue(0, 0).GetValue<int64_t>();
        qDebug() << "Normalized table created:" << normalized_table << "with" << count << "rows";

        return normalized_table;
    } catch (const std::exception &e) {
        qCritical() << "CreateNormalizedTable failed:" << e.what();
        return QString();
    }
}

QStringList CalcService::GetPreviewHeaders(const QString &table_name) {
    return GetTableColumns(table_name);
}

QList<QStringList> CalcService::GetPreviewRows(const QString &table_name, int max_rows) {
    QList<QStringList> rows;
    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();
        auto res = con.Query(
            QString("SELECT * FROM %1 LIMIT %2").arg(table_name).arg(max_rows).toStdString());

        int col_count = res->ColumnCount();
        int row_count = res->RowCount();
        for (int r = 0; r < row_count; r++) {
            QStringList row;
            for (int c = 0; c < col_count; c++) {
                try {
                    row << QString::fromStdString(res->GetValue(c, r).ToString());
                } catch (...) {
                    row << "";
                }
            }
            rows << row;
        }
    } catch (const std::exception &e) {
        qCritical() << "GetPreviewRows failed:" << e.what();
    }
    return rows;
}

} // namespace freight::services
