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
                                          double vol_height) {
    core::CalcResult result;
    result.dest_province = province;
    result.weight = weight;
    result.vol_weight = vol_weight;

    try {
        auto &cfg = core::AppConfig::Instance();
        db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
        repo.Init();

        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        // 先读模板计费三参数：续重进位、续重单位、体积重除数
        QString rounding_mode = "ceil_0_1kg";
        double tpl_additional_unit = 1.0;
        int vol_divisor = 6000;
        QVariantMap tpl = repo.GetTemplate(template_id);
        if (!tpl.isEmpty()) {
            rounding_mode = tpl.value("tpl_rounding_mode", "ceil_0_1kg").toString().trimmed();
            if (rounding_mode.isEmpty()) rounding_mode = "ceil_0_1kg";
            double au = tpl.value("tpl_additional_unit", 0.0).toDouble();
            if (au > 0) tpl_additional_unit = au;
            else {
                double au_old = tpl.value("additional_unit", 0.0).toDouble();
                if (au_old > 0) tpl_additional_unit = au_old;
            }
            int vd = tpl.value("tpl_vol_divisor", 6000).toInt();
            if (vd > 0) vol_divisor = vd;
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
    ROUND(base_fee + fuel_surcharge + remote_surcharge + strategy_surcharge, 2) AS total_fee
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
    } catch (const std::exception &e) {
        result.success = false;
        result.error_msg = QString::fromStdString(e.what());
        qCritical() << "CalcSingle failed:" << result.error_msg;
    }

    return result;
}

bool CalcService::CalcBatch(const QString &input_table,
                            const QString &output_table) {
    try {
        emit ProgressChanged(5);
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        QString sql = BuildCalcSQL(input_table, output_table);
        emit ProgressChanged(15);
        con.Query(sql.toStdString());

        emit ProgressChanged(100);
        emit CalcFinished(true, QStringLiteral("计算完成"));
        return true;
    } catch (const std::exception &e) {
        qCritical() << "CalcBatch failed:" << e.what();
        emit CalcFinished(false, QString::fromStdString(e.what()));
        return false;
    }
}

bool CalcService::CalcFromFile(const QString &input_file,
                               const QString &output_file) {
    try {
        emit ProgressChanged(0);
        auto &dbm = db::DuckDBManager::Instance();

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
        if (!CalcBatch(normalized_table, output_table)) {
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
                                  const QString &output_table) {
    QString sql = QString(R"SQL(
CREATE OR REPLACE TABLE %1 AS
WITH
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
        COALESCE(c.default_template, 'zto_standard') AS template_id
    FROM input_data i
    LEFT JOIN customers c ON c.customer_id = i.customer_id
),
template_info AS (
    SELECT
        ctl.*,
        COALESCE(ft.default_no_weight_fee, 0) AS default_no_weight_fee,
        COALESCE(NULLIF(ft.tpl_rounding_mode, ''), 'ceil_0_1kg') AS tpl_rounding_mode,
        COALESCE(NULLIF(ft.tpl_additional_unit, 0), NULLIF(ft.additional_unit, 0), 1.0) AS tpl_additional_unit,
        COALESCE(NULLIF(ft.tpl_vol_divisor, 0), CAST(NULLIF(ft.vol_weight_ratio, 0) AS INTEGER), 6000) AS tpl_vol_divisor
    FROM customer_template_lookup ctl
    LEFT JOIN freight_templates ft ON ft.template_id = ctl.template_id
),
-- 按模板 rounding_mode 对计费重量做进位（国标默认 0.1kg 进一）
charge_weight_rounded AS (
    SELECT
        ti.*,
        CASE COALESCE(ti.tpl_rounding_mode, 'ceil_0_1kg')
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
                         / GREATEST(COALESCE(mt.tpl_additional_unit, mt.additional_unit, 1.0), 0.0001)
                     )) * mt.additional_price
            ELSE
                COALESCE(tm.max_first_price, 0) +
                GREATEST(0, CEIL(
                    (mt.charge_weight - COALESCE(tm.max_first_weight, 0))
                    / GREATEST(COALESCE(mt.tpl_additional_unit, tm.max_additional_unit, 1.0), 0.0001)
                )) * COALESCE(tm.max_additional_price, 0)
        END AS base_fee
    FROM matched_tier mt
    LEFT JOIN tier_max tm
        ON tm.template_id = mt.template_id
       AND tm.group_code = mt.group_code
),
fuel_surcharge_calc AS (
    SELECT
        bfc.*,
        COALESCE((
            SELECT fs.rate
            FROM fuel_surcharge fs
            WHERE fs.template_id = bfc.template_id
              AND fs.is_active = 1
              AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                         WHERE template_id = bfc.template_id
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
                    WHEN 'percentage' THEN rac.base_fee * s.amount
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
)

SELECT
    order_id AS "订单号",
    customer_id AS "客户编号",
    dest_province AS "目的省份",
    dest_city AS "目的城市",
    weight AS "实际重量(KG)",
    vol_weight AS "体积重量(KG)",
    charge_weight AS "计费重量(KG)",
    ROUND(base_fee, 2) AS "基础运费",
    ROUND(fuel_surcharge, 2) AS "燃油附加费",
    ROUND(remote_surcharge, 2) AS "地区加价",
    ROUND(strategy_surcharge, 2) AS "其他附加费",
    ROUND(base_fee + fuel_surcharge + remote_surcharge + strategy_surcharge, 2) AS "总运费",
    'CNY' AS "币种"
FROM strategy_surcharge_calc
    )SQL").arg(output_table, input_table);

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
