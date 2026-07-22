#include "services/calc_service.hpp"
#include "db/duckdb_manager.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantList>

namespace freight::services {

CalcService::CalcService(QObject *parent) : QObject(parent) {}

core::CalcResult CalcService::CalcSingle(const QString &province,
                                          double weight,
                                          double vol_weight,
                                          const QString &template_id,
                                          const QString &city,
                                          const QString &customer_id) {
    core::CalcResult result;
    result.dest_province = province;
    result.weight = weight;
    result.vol_weight = vol_weight;

    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        double charge_weight = (vol_weight > 0 && vol_weight > weight) ? vol_weight : weight;

        QRegularExpression province_suffix_re(R"((省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$)");
        QString norm_province = province;
        norm_province.remove(province_suffix_re);

        QString sql = QString(R"SQL(
WITH
template_info AS (
    SELECT COALESCE(default_no_weight_fee, 0) AS default_no_weight_fee
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
      AND zgp.province = '%2'
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
                     + %3 * (SELECT additional_price FROM matched_tier)
            ELSE
                COALESCE((SELECT max_first_price FROM tier_max), 0) +
                %3 * COALESCE((SELECT max_additional_price FROM tier_max), 0)
        END AS base_fee
),
fuel_surcharge_calc AS (
    SELECT
        bfc.base_fee,
        COALESCE(fs.rate, 0) * bfc.base_fee AS fuel_surcharge
    FROM base_fee_calc bfc
    LEFT JOIN fuel_surcharge fs
        ON fs.template_id = '%1'
       AND fs.is_active = 1
       AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                 WHERE template_id = '%1'
                                   AND is_active = 1
                                   AND effective_date <= CURRENT_DATE)
),
remote_area_calc AS (
    SELECT
        fsc.*,
        COALESCE((
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = '%1'
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND ra.province = '%2'
                   AND (ra.city IS NULL OR ra.city = '' OR ra.city = '%4')
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND ra.city = '%4'
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
                    ELSE 0
                END
            )
            FROM surcharge_strategies s
            LEFT JOIN surcharge_provinces sp ON sp.strategy_id = s.strategy_id
            WHERE s.is_active = 1
              AND s.template_id = '%1'
              AND (
                  s.strategy_scope = 'global'
                  OR (s.strategy_scope = 'province' AND sp.province = '%2')
              )
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
        .arg(charge_weight)
        .arg(city);

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
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        QString sql = BuildCalcSQL(input_table, output_table);
        con.Query(sql.toStdString());

        return true;
    } catch (const std::exception &e) {
        qCritical() << "CalcBatch failed:" << e.what();
        return false;
    }
}

bool CalcService::CalcFromFile(const QString &input_file,
                               const QString &output_file) {
    try {
        auto &dbm = db::DuckDBManager::Instance();

        QFileInfo fi(input_file);
        QString input_table = "_input_tmp";
        QString output_table = "_output_tmp";

        if (!dbm.ImportFromFile(input_table, input_file)) {
            return false;
        }

        QString normalized_table = NormalizeColumns(input_table);
        if (normalized_table.isEmpty()) {
            return false;
        }

        if (!CalcBatch(normalized_table, output_table)) {
            return false;
        }

        if (!dbm.ExportToFile(output_table, output_file)) {
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        qCritical() << "CalcFromFile failed:" << e.what();
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
        END AS charge_weight
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
        COALESCE(ft.default_no_weight_fee, 0) AS default_no_weight_fee
    FROM customer_template_lookup ctl
    LEFT JOIN freight_templates ft ON ft.template_id = ctl.template_id
),
matched_zone AS (
    SELECT
        ti.*,
        REGEXP_REPLACE(ti.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '') AS norm_province,
        zgp.group_code,
        zg.group_name
    FROM template_info ti
    LEFT JOIN zone_group_provinces zgp
        ON zgp.template_id = ti.template_id
       AND zgp.province = REGEXP_REPLACE(ti.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
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
                THEN mt.first_price + mt.charge_weight * mt.additional_price
            ELSE
                COALESCE(tm.max_first_price, 0) +
                mt.charge_weight * COALESCE(tm.max_additional_price, 0)
        END AS base_fee
    FROM matched_tier mt
    LEFT JOIN tier_max tm
        ON tm.template_id = mt.template_id
       AND tm.group_code = mt.group_code
),
fuel_surcharge_calc AS (
    SELECT
        bfc.*,
        COALESCE(fs.rate, 0) * bfc.base_fee AS fuel_surcharge
    FROM base_fee_calc bfc
    LEFT JOIN fuel_surcharge fs
        ON fs.template_id = bfc.template_id
       AND fs.is_active = 1
       AND fs.effective_date = (SELECT MAX(effective_date) FROM fuel_surcharge
                                 WHERE template_id = bfc.template_id
                                   AND is_active = 1
                                   AND effective_date <= CURRENT_DATE)
),
remote_area_calc AS (
    SELECT
        fsc.*,
        COALESCE((
            SELECT SUM(ra.surcharge)
            FROM remote_areas ra
            WHERE ra.template_id = fsc.template_id
              AND ra.is_active = 1
              AND (
                  (ra.province IS NOT NULL AND ra.province <> ''
                   AND ra.province = REGEXP_REPLACE(fsc.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', '')
                   AND (ra.city IS NULL OR ra.city = '' OR ra.city = fsc.dest_city)
                   AND (ra.district IS NULL OR ra.district = ''))
                  OR
                  (ra.city IS NOT NULL AND ra.city <> ''
                   AND ra.city = fsc.dest_city
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
                    ELSE 0
                END
            )
            FROM surcharge_strategies s
            LEFT JOIN surcharge_provinces sp ON sp.strategy_id = s.strategy_id
            WHERE s.is_active = 1
              AND s.template_id = rac.template_id
              AND (
                  s.strategy_scope = 'global'
                  OR (s.strategy_scope = 'province' AND sp.province = REGEXP_REPLACE(rac.dest_province, '(省|市|维吾尔自治区|回族自治区|壮族自治区|自治区)$', ''))
              )
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
    struct ColumnMapping {
        QString standard_name;
        QStringList keywords;
    };

    QList<ColumnMapping> mappings = {
        {"order_id",     {"order_id", "order_no", "waybill", "订单号", "订单编号", "运单号", "快递单号", "单号"}},
        {"dest_province",{"dest_province", "to_province", "province", "目的省份", "省份", "收件省份", "到达省份", "收货省份"}},
        {"dest_city",    {"dest_city", "to_city", "city", "目的城市", "城市", "收件城市", "到达城市", "收货城市"}},
        {"weight",       {"weight", "actual_weight", "gross_weight", "real_weight", "结算重量", "重量", "实际重量", "实重", "毛重", "计费重量"}},
        {"vol_weight",   {"vol_weight", "volume_weight", "volumetric_weight", "体积重量", "体积重", "体积", "抛重"}},
        {"customer_id",  {"customer_id", "cust_id", "customer", "客户id", "客户编号", "客户", "客户名称", "客户名"}},
    };

    QMap<QString, QString> col_map;
    // 第1轮：完全相等匹配（精确优先，避免 "订单客户" 误匹配 "客户"）
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
    // 第2轮：子串匹配（仅对未匹配的标准列）
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

QString CalcService::CreateNormalizedTable(const QString &input_table,
                                            const QMap<QString, QString> &col_map) {
    QStringList select_parts;
    select_parts << QString("COALESCE(\"%1\", '') AS order_id").arg(col_map.value("order_id", ""));
    select_parts << QString("COALESCE(\"%1\", '') AS customer_id").arg(col_map.value("customer_id", ""));
    select_parts << QString("COALESCE(\"%1\", '') AS dest_province").arg(col_map.value("dest_province"));
    select_parts << QString("COALESCE(\"%1\", '') AS dest_city").arg(col_map.value("dest_city", ""));
    // 用 TRY_CAST 处理空字符串/非数字字符（COALESCE 不能捕获 CAST 错误）
    select_parts << QString("COALESCE(TRY_CAST(\"%1\" AS DOUBLE), 0) AS weight").arg(col_map.value("weight"));
    select_parts << QString("COALESCE(TRY_CAST(\"%1\" AS DOUBLE), 0) AS vol_weight").arg(col_map.value("vol_weight", ""));

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
