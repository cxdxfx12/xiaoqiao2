#pragma once

#include <QString>
#include <QDateTime>
#include <QVariantMap>

namespace freight::core {

enum class StrategyScope {
    GLOBAL = 0,
    TEMPLATE = 1,
    PROVINCE = 2,
    CUSTOMER = 3,
};

enum class StrategyType {
    FIXED = 0,
    PERCENTAGE = 1,
    PER_WEIGHT = 2,
    PER_VOLUME = 3,
};

struct SurchargeStrategy {
    QString strategy_id;
    QString strategy_name;
    StrategyScope scope = StrategyScope::GLOBAL;
    QString template_id;
    StrategyType type = StrategyType::FIXED;
    double amount = 0.0;
    double min_weight = 0.0;
    double max_weight = 0.0;
    int priority = 0;
    bool is_active = true;
    QString description;

    static StrategyScope ScopeFromString(const QString &s) {
        if (s == "template") return StrategyScope::TEMPLATE;
        if (s == "province") return StrategyScope::PROVINCE;
        if (s == "customer") return StrategyScope::CUSTOMER;
        return StrategyScope::GLOBAL;
    }

    static QString ScopeToString(StrategyScope s) {
        switch (s) {
            case StrategyScope::GLOBAL: return "global";
            case StrategyScope::TEMPLATE: return "template";
            case StrategyScope::PROVINCE: return "province";
            case StrategyScope::CUSTOMER: return "customer";
        }
        return "global";
    }

    static StrategyType TypeFromString(const QString &s) {
        if (s == "percentage") return StrategyType::PERCENTAGE;
        if (s == "per_weight") return StrategyType::PER_WEIGHT;
        if (s == "per_volume") return StrategyType::PER_VOLUME;
        return StrategyType::FIXED;
    }

    static QString TypeToString(StrategyType t) {
        switch (t) {
            case StrategyType::FIXED: return "fixed";
            case StrategyType::PERCENTAGE: return "percentage";
            case StrategyType::PER_WEIGHT: return "per_weight";
            case StrategyType::PER_VOLUME: return "per_volume";
        }
        return "fixed";
    }

    QVariantMap ToMap() const {
        return {
            {"strategy_id", strategy_id},
            {"strategy_name", strategy_name},
            {"strategy_scope", ScopeToString(scope)},
            {"template_id", template_id},
            {"strategy_type", TypeToString(type)},
            {"amount", amount},
            {"min_weight", min_weight},
            {"max_weight", max_weight},
            {"priority", priority},
            {"is_active", is_active},
            {"description", description},
        };
    }
};

struct CalcResult {
    QString order_id;
    QString customer_id;
    QString dest_province;
    QString dest_city;
    double weight = 0.0;
    double vol_weight = 0.0;
    double charge_weight = 0.0;
    double base_fee = 0.0;
    double fuel_surcharge = 0.0;
    double remote_surcharge = 0.0;
    double strategy_surcharge = 0.0;
    double total_fee = 0.0;
    QString currency = "CNY";
    bool success = true;
    QString error_msg;

    // ---- L6 拉均重扩展（Step5 接入）----
    bool    lajz_in_pool          = false;   // 本单是否命中拉均重池
    QString lajz_avg_tpl_id       = "";      // 命中的合同 avg_tpl_id
    QString lajz_contract_no      = "";      // 命中的合同编号
    double  lajz_pool_avg_kg      = 0.0;     // 池均重（kg）
    double  lajz_pool_max_kg      = 0.0;     // L2 进池上限（kg）
    double  lajz_base_avg_kg      = 0.0;     // 约定基准均重（kg）
    double  lajz_fee_cap_kg       = 0.0;     // L4 加价封顶 kg
    double  lajz_base_fee         = 0.0;     // 基准价（元/票）
    double  lajz_step_kg          = 0.0;     // 超基准步长 kg
    double  lajz_step_fee         = 0.0;     // 每步加价 元
    int     lajz_over_cap_mode    = 0;       // 0=按cap封顶 1=超cap整池回阶梯
    bool    lajz_used             = false;   // 本单最终是否采用了拉均重总价（=true 时total_fee用拉均重；=false时total_fee用阶梯）
    double  lajz_fee_per_ticket   = 0.0;     // 拉均重单票总价（不含燃油/偏远/策略；与L6决策独立保存方便对账）
    double  lajz_save_vs_tier     = 0.0;     // 相对阶梯 base_fee 的节省（正数=便宜了多少 元/票；负数=贵了）
};

} // namespace freight::core
