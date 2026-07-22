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
    double strategy_surcharge = 0.0;
    double total_fee = 0.0;
    QString currency = "CNY";
    bool success = true;
    QString error_msg;
};

} // namespace freight::core
