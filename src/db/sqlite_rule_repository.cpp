#include "db/sqlite_rule_repository.hpp"
#include <QSqlError>
#include <QDebug>
#include <QFileInfo>
#include <QUuid>

namespace freight::db {

SqliteRuleRepository::SqliteRuleRepository(const QString &db_path)
    : db_path_(db_path) {
    // 每个实例使用唯一的连接名，避免冲突
    static int conn_counter = 0;
    conn_name_ = QString("rules_conn_%1").arg(++conn_counter);
}

SqliteRuleRepository::~SqliteRuleRepository() {
    if (db_.isOpen()) {
        db_.close();
    }
    if (!conn_name_.isEmpty()) {
        QSqlDatabase::removeDatabase(conn_name_);
    }
}

bool SqliteRuleRepository::Init() {
    db_ = QSqlDatabase::addDatabase("QSQLITE", conn_name_);
    db_.setDatabaseName(db_path_);

    if (!db_.open()) {
        qCritical() << "Failed to open rules db:" << db_.lastError().text();
        return false;
    }

    db_.exec("PRAGMA journal_mode=WAL");
    db_.exec("PRAGMA synchronous=NORMAL");
    db_.exec("PRAGMA foreign_keys=ON");

    // 检查 schema 版本，旧版本需要重建
    QSqlQuery vq(db_);
    vq.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER)");
    vq.exec("SELECT version FROM schema_version");
    int current_version = 0;
    if (vq.next()) {
        current_version = vq.value(0).toInt();
    }

    if (current_version < 10) {
        // 更新报价表数据
        qInfo() << "Upgrading schema from version" << current_version << "to 10";
        // 关闭外键约束检查，确保 DROP 能成功
        db_.exec("PRAGMA foreign_keys=OFF");
        QSqlQuery dq(db_);
        QStringList drop_tables = {
            "surcharge_date_ranges", "surcharge_customers", "surcharge_provinces",
            "surcharge_strategies", "remote_areas", "fuel_surcharge",
            "tiered_pricing", "zone_group_provinces", "zone_groups",
            "freight_templates", "customers"
        };
        for (const auto &tbl : drop_tables) {
            QString sql = QString("DROP TABLE IF EXISTS %1").arg(tbl);
            if (!dq.exec(sql)) {
                qCritical() << "DROP TABLE failed for" << tbl << ":" << dq.lastError().text();
            }
        }
        db_.exec("PRAGMA foreign_keys=ON");
    }

    bool ok = CreateTables();

    // schema 升级后强制重新生成默认数据
    if (current_version < 10) {
        InitDefaultData();
    }

    // 强制修复前4个一口价阶梯的 additional_price 为0，避免历史数据错误
    QSqlQuery fix_q(db_);
    fix_q.exec("UPDATE tiered_pricing SET additional_price = 0 WHERE tier_code IN ('tier_0_0.5', 'tier_0.5_1', 'tier_1_2', 'tier_2_3')");

    // 更新 schema 版本
    if (current_version < 10) {
        vq.exec("DELETE FROM schema_version");
        vq.exec("INSERT INTO schema_version VALUES (10)");
    }

    return ok;
}

bool SqliteRuleRepository::IsFirstRun() {
    QSqlQuery q = db_.exec("SELECT COUNT(*) FROM freight_templates");
    if (q.next()) {
        return q.value(0).toInt() == 0;
    }
    return true;
}

bool SqliteRuleRepository::CreateTables() {
    QSqlQuery q(db_);

    q.exec("CREATE TABLE IF NOT EXISTS customers ("
           "customer_id VARCHAR(100) PRIMARY KEY,"
           "customer_name VARCHAR(200) NOT NULL,"
           "discount_rate REAL DEFAULT 1.0,"
           "default_template VARCHAR(100),"
           "contact_person VARCHAR(100),"
           "contact_phone VARCHAR(50),"
           "address TEXT,"
           "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");

    q.exec("CREATE TABLE IF NOT EXISTS freight_templates ("
           "template_id VARCHAR(100) PRIMARY KEY,"
           "template_name VARCHAR(200) NOT NULL,"
           "carrier_name VARCHAR(100),"
           "first_weight REAL DEFAULT 1.0,"
           "additional_unit REAL DEFAULT 1.0,"
           "vol_weight_ratio REAL DEFAULT 6000.0,"
           "description TEXT,"
           "is_default INTEGER DEFAULT 0,"
           "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");

    q.exec("CREATE TABLE IF NOT EXISTS zone_groups ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) NOT NULL,"
           "group_code VARCHAR(20) NOT NULL,"
           "group_name VARCHAR(100) NOT NULL,"
           "sort_order INTEGER DEFAULT 0,"
           "UNIQUE(template_id, group_code))");

    q.exec("CREATE TABLE IF NOT EXISTS zone_group_provinces ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) NOT NULL,"
           "group_code VARCHAR(20) NOT NULL,"
           "province VARCHAR(50) NOT NULL,"
           "UNIQUE(template_id, group_code, province))");

    q.exec("CREATE TABLE IF NOT EXISTS tiered_pricing ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) NOT NULL,"
           "group_code VARCHAR(20) NOT NULL,"
           "tier_code VARCHAR(20) NOT NULL,"
           "tier_name VARCHAR(100),"
           "min_weight REAL NOT NULL,"
           "max_weight REAL NOT NULL,"
           "first_weight REAL DEFAULT 1.0,"
           "first_price REAL NOT NULL,"
           "additional_unit REAL DEFAULT 1.0,"
           "additional_price REAL NOT NULL,"
           "sort_order INTEGER DEFAULT 0,"
           "UNIQUE(template_id, group_code, tier_code))");

    q.exec("CREATE TABLE IF NOT EXISTS fuel_surcharge ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) NOT NULL,"
           "effective_date DATE NOT NULL,"
           "rate REAL NOT NULL,"
           "is_active INTEGER DEFAULT 1)");

    q.exec("CREATE TABLE IF NOT EXISTS remote_areas ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) NOT NULL,"
           "province VARCHAR(50),"
           "city VARCHAR(100),"
           "district VARCHAR(100),"
           "surcharge REAL DEFAULT 0,"
           "is_active INTEGER DEFAULT 1)");

    q.exec("CREATE TABLE IF NOT EXISTS surcharge_strategies ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "strategy_id VARCHAR(100) UNIQUE NOT NULL,"
           "strategy_name VARCHAR(200) NOT NULL,"
           "strategy_scope VARCHAR(20) NOT NULL,"
           "template_id VARCHAR(100),"
           "strategy_type VARCHAR(20) NOT NULL,"
           "amount REAL NOT NULL DEFAULT 0,"
           "min_weight REAL,"
           "max_weight REAL,"
           "priority INTEGER NOT NULL DEFAULT 0,"
           "is_active INTEGER NOT NULL DEFAULT 1,"
           "description TEXT,"
           "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");

    q.exec("CREATE TABLE IF NOT EXISTS surcharge_provinces ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "strategy_id VARCHAR(100) NOT NULL,"
           "province VARCHAR(50) NOT NULL,"
           "UNIQUE(strategy_id, province))");

    q.exec("CREATE TABLE IF NOT EXISTS surcharge_customers ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "strategy_id VARCHAR(100) NOT NULL,"
           "customer_id VARCHAR(100) NOT NULL,"
           "UNIQUE(strategy_id, customer_id))");

    q.exec("CREATE TABLE IF NOT EXISTS surcharge_date_ranges ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "strategy_id VARCHAR(100) NOT NULL,"
           "start_date DATE NOT NULL,"
           "end_date DATE NOT NULL,"
           "week_days VARCHAR(20))");

    if (IsFirstRun()) {
        InitDefaultData();
    }

    return true;
}

void SqliteRuleRepository::InitDefaultData() {
    qDebug() << "Initializing default data...";

    CreateDefaultTemplate();
    CreateDefaultSurcharges();
    qDebug() << "Default data initialized.";
}

bool SqliteRuleRepository::CreateDefaultTemplate() {
    QSqlQuery q(db_);
    QString tpl_id = "zto_standard";

    q.prepare("INSERT OR REPLACE INTO freight_templates "
              "(template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, is_default, description) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(tpl_id);
    q.addBindValue("中通标准快递");
    q.addBindValue("中通");
    q.addBindValue(1.0);
    q.addBindValue(1.0);
    q.addBindValue(6000.0);
    q.addBindValue(1);
    q.addBindValue("中通标准快递报价，内置默认模板");
    if (!q.exec()) {
        qCritical() << "Create template failed:" << q.lastError().text();
        return false;
    }

    struct ZoneGroup {
        QString code;
        QString name;
        QStringList provinces;
        double t05_first;  // 0-0.5kg
        double t1_first;   // 0.5-1kg
        double t2_first;   // 1-2kg
        double t3_first;   // 2-3kg
        double mid_first;  // 3-30kg 首重
        double mid_add;    // 3-30kg 续重
        double big_first;  // 30kg+ 首重
        double big_add;    // 30kg+ 续重
    };

    QList<ZoneGroup> zones = {
        {"zone1", "一区", {"江苏","浙江","安徽","上海"},
            2.26, 2.46, 3.56, 4.76, 3.76, 0.8, 3.86, 0.8},
        {"zone2", "二区", {"山东","广东","福建","北京","河南","湖北","湖南","江西","天津","河北"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.1, 4.06, 1.3},
        {"zone3", "三区", {"山西","广西","四川","重庆","陕西","贵州","辽宁","吉林","黑龙江","云南"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.5, 4.06, 1.6},
        {"zone4", "四区", {"海南","甘肃","青海","内蒙古","宁夏"},
            2.56, 3.56, 4.06, 5.06, 3.76, 2.5, 4.06, 4.3},
        {"zone5", "五区-新疆", {"新疆"},
            10.0, 13.0, 20.0, 25.0, 15.0, 15.0, 15.0, 15.0},
        {"zone6", "五区-西藏", {"西藏"},
            13.0, 15.0, 25.0, 30.0, 15.0, 15.0, 15.0, 15.0},
    };

    for (int i = 0; i < zones.size(); i++) {
        const auto &z = zones[i];

        q.prepare("INSERT INTO zone_groups (template_id, group_code, group_name, sort_order) VALUES (?,?,?,?)");
        q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(z.name); q.addBindValue(i+1);
        q.exec();

        for (const auto &prov : z.provinces) {
            q.prepare("INSERT INTO zone_group_provinces (template_id, group_code, province) VALUES (?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(prov);
            q.exec();
        }

        struct Tier {
            QString code; QString name;
            double min_w; double max_w;
            double first_w; double first_p;
            double add_u; double add_p;
            int sort;
        };

        QList<Tier> tiers = {
            {"tier_0_0.5", "0-0.5KG", 0, 0.5, 0.5, z.t05_first, 0.5, 0, 1},
            {"tier_0.5_1", "0.51KG-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
            {"tier_1_2", "1-2KG", 1.0, 2.0, 1.0, z.t2_first, 1.0, 0, 3},
            {"tier_2_3", "2-3KG", 2.0, 3.0, 1.0, z.t3_first, 1.0, 0, 4},
            {"tier_3_30", "3-30KG", 3.0, 30.0, 1.0, z.mid_first, 1.0, z.mid_add, 5},
            {"tier_30_plus", "30KG以上", 30.0, 9999.0, 1.0, z.big_first, 1.0, z.big_add, 6},
        };

        for (const auto &t : tiers) {
            q.prepare("INSERT INTO tiered_pricing "
                      "(template_id, group_code, tier_code, tier_name, min_weight, max_weight, "
                      " first_weight, first_price, additional_unit, additional_price, sort_order) "
                      "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code);
            q.addBindValue(t.code); q.addBindValue(t.name);
            q.addBindValue(t.min_w); q.addBindValue(t.max_w);
            q.addBindValue(t.first_w); q.addBindValue(t.first_p);
            q.addBindValue(t.add_u); q.addBindValue(t.add_p);
            q.addBindValue(t.sort);
            q.exec();
        }
    }

    q.prepare("INSERT INTO fuel_surcharge (template_id, effective_date, rate) VALUES (?,?,?)");
    q.addBindValue(tpl_id); q.addBindValue("2026-01-01"); q.addBindValue(0.0);
    q.exec();

    return true;
}

bool SqliteRuleRepository::CreateDefaultSurcharges() {
    QSqlQuery q(db_);

    auto addStrategy = [&](const QString &id, const QString &name,
                           const QString &scope, const QString &type,
                           double amount, int priority,
                           const QString &desc,
                           const QStringList &provinces = QStringList()) {
        q.prepare("INSERT OR IGNORE INTO surcharge_strategies "
                  "(strategy_id, strategy_name, strategy_scope, template_id, strategy_type, "
                  " amount, priority, is_active, description) "
                  "VALUES (?,?,?,?,?,?,?,?,?)");
        q.addBindValue(id); q.addBindValue(name);
        q.addBindValue(scope); q.addBindValue("zto_standard");
        q.addBindValue(type); q.addBindValue(amount);
        q.addBindValue(priority); q.addBindValue(1);
        q.addBindValue(desc);
        q.exec();

        for (const auto &p : provinces) {
            q.prepare("INSERT OR IGNORE INTO surcharge_provinces (strategy_id, province) VALUES (?,?)");
            q.addBindValue(id); q.addBindValue(p);
            q.exec();
        }
    };

    addStrategy("packing_fee", "包装服务费",
                "global", "fixed", 1.0, 10,
                "每个包裹收取1元包装服务费");

    addStrategy("remote_xz_xj", "新疆西藏偏远附加费",
                "province", "per_weight", 2.0, 20,
                "新疆、西藏地区每公斤加收2元",
                {"新疆", "西藏"});

    addStrategy("peak_season", "旺季附加费",
                "global", "percentage", 0.10, 5,
                "旺季运费上浮10%（暂未启用日期限制）");

    return true;
}

QVariantList SqliteRuleRepository::ListTemplates() {
    QVariantList result;
    QSqlQuery q("SELECT * FROM freight_templates ORDER BY is_default DESC, created_at DESC", db_);
    while (q.next()) {
        QVariantMap m;
        m["template_id"] = q.value("template_id");
        m["template_name"] = q.value("template_name");
        m["carrier_name"] = q.value("carrier_name");
        m["is_default"] = q.value("is_default").toInt() == 1;
        m["description"] = q.value("description");
        result << m;
    }
    return result;
}

QVariantMap SqliteRuleRepository::GetTemplate(const QString &template_id) {
    QVariantMap m;
    QSqlQuery q(db_);
    q.prepare("SELECT * FROM freight_templates WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    if (q.next()) {
        m["template_id"] = q.value("template_id");
        m["template_name"] = q.value("template_name");
        m["carrier_name"] = q.value("carrier_name");
        m["first_weight"] = q.value("first_weight").toDouble();
        m["additional_unit"] = q.value("additional_unit").toDouble();
        m["vol_weight_ratio"] = q.value("vol_weight_ratio").toDouble();
        m["is_default"] = q.value("is_default").toInt() == 1;
        m["description"] = q.value("description");
    }
    return m;
}

bool SqliteRuleRepository::AddTemplate(const QVariantMap &tpl) {
    QSqlQuery q(db_);
    QString tpl_id = tpl["template_id"].toString();

    q.prepare("INSERT INTO freight_templates (template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, description) VALUES (?,?,?,?,?,?,?)");
    q.addBindValue(tpl_id);
    q.addBindValue(tpl["template_name"].toString());
    q.addBindValue(tpl["carrier_name"].toString());
    q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
    q.addBindValue(tpl.value("additional_unit", 1.0).toDouble());
    q.addBindValue(tpl.value("vol_weight_ratio", 6000.0).toDouble());
    q.addBindValue(tpl["description"].toString());
    if (!q.exec()) {
        return false;
    }

    // 创建默认分区和阶梯定价
    struct ZoneGroup {
        QString code; QString name; QStringList provinces;
        double t05_first; double t1_first; double t2_first; double t3_first;
        double mid_first; double mid_add; double big_first; double big_add;
    };

    QList<ZoneGroup> zones = {
        {"zone1", "一区", {"江苏","浙江","安徽","上海"},
            2.26, 2.46, 3.56, 4.76, 3.76, 0.8, 3.86, 0.8},
        {"zone2", "二区", {"山东","广东","福建","北京","河南","湖北","湖南","江西","天津","河北"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.1, 4.06, 1.3},
        {"zone3", "三区", {"山西","广西","四川","重庆","陕西","贵州","辽宁","吉林","黑龙江","云南"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.5, 4.06, 1.6},
        {"zone4", "四区", {"海南","甘肃","青海","内蒙古","宁夏"},
            2.56, 3.56, 4.06, 5.06, 3.76, 2.5, 4.06, 4.3},
        {"zone5", "五区-新疆", {"新疆"},
            10.0, 13.0, 20.0, 25.0, 15.0, 15.0, 15.0, 15.0},
        {"zone6", "五区-西藏", {"西藏"},
            13.0, 15.0, 25.0, 30.0, 15.0, 15.0, 15.0, 15.0},
    };

    for (int i = 0; i < zones.size(); i++) {
        const auto &z = zones[i];

        q.prepare("INSERT INTO zone_groups (template_id, group_code, group_name, sort_order) VALUES (?,?,?,?)");
        q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(z.name); q.addBindValue(i+1);
        q.exec();

        for (const auto &prov : z.provinces) {
            q.prepare("INSERT INTO zone_group_provinces (template_id, group_code, province) VALUES (?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(prov);
            q.exec();
        }

        struct Tier {
            QString code; QString name;
            double min_w; double max_w;
            double first_w; double first_p;
            double add_u; double add_p;
            int sort;
        };

        QList<Tier> tiers = {
            {"tier_0_0.5", "0-0.5KG", 0, 0.5, 0.5, z.t05_first, 0.5, 0, 1},
            {"tier_0.5_1", "0.51KG-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
            {"tier_1_2", "1-2KG", 1.0, 2.0, 1.0, z.t2_first, 1.0, 0, 3},
            {"tier_2_3", "2-3KG", 2.0, 3.0, 1.0, z.t3_first, 1.0, 0, 4},
            {"tier_3_30", "3-30KG", 3.0, 30.0, 1.0, z.mid_first, 1.0, z.mid_add, 5},
            {"tier_30_plus", "30KG以上", 30.0, 9999.0, 1.0, z.big_first, 1.0, z.big_add, 6},
        };

        for (const auto &t : tiers) {
            q.prepare("INSERT INTO tiered_pricing "
                      "(template_id, group_code, tier_code, tier_name, min_weight, max_weight, "
                      " first_weight, first_price, additional_unit, additional_price, sort_order) "
                      "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code);
            q.addBindValue(t.code); q.addBindValue(t.name);
            q.addBindValue(t.min_w); q.addBindValue(t.max_w);
            q.addBindValue(t.first_w); q.addBindValue(t.first_p);
            q.addBindValue(t.add_u); q.addBindValue(t.add_p);
            q.addBindValue(t.sort);
            q.exec();
        }
    }

    // 创建默认燃油附加费（费率为0）
    q.prepare("INSERT INTO fuel_surcharge (template_id, effective_date, rate) VALUES (?,?,?)");
    q.addBindValue(tpl_id); q.addBindValue("2026-01-01"); q.addBindValue(0.0);
    q.exec();

    return true;
}

bool SqliteRuleRepository::UpdateTemplate(const QVariantMap &tpl) {
    QSqlQuery q(db_);
    q.prepare("UPDATE freight_templates SET template_name=?, carrier_name=?, first_weight=?, additional_unit=?, vol_weight_ratio=?, description=?, updated_at=CURRENT_TIMESTAMP WHERE template_id=?");
    q.addBindValue(tpl["template_name"].toString());
    q.addBindValue(tpl["carrier_name"].toString());
    q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
    q.addBindValue(tpl.value("additional_unit", 1.0).toDouble());
    q.addBindValue(tpl.value("vol_weight_ratio", 6000.0).toDouble());
    q.addBindValue(tpl.value("description", "").toString());
    q.addBindValue(tpl["template_id"].toString());
    return q.exec();
}

bool SqliteRuleRepository::DeleteTemplate(const QString &template_id) {
    QSqlQuery q(db_);
    // 级联删除关联数据
    q.prepare("DELETE FROM tiered_pricing WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    q.prepare("DELETE FROM zone_group_provinces WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    q.prepare("DELETE FROM zone_groups WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    q.prepare("DELETE FROM fuel_surcharge WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    q.prepare("DELETE FROM remote_areas WHERE template_id = ?");
    q.addBindValue(template_id);
    q.exec();
    // 删除模板本身
    q.prepare("DELETE FROM freight_templates WHERE template_id = ?");
    q.addBindValue(template_id);
    return q.exec();
}

QVariantList SqliteRuleRepository::ListSurchargeStrategies(const QString &scope, bool only_active) {
    QVariantList result;
    QSqlQuery q(db_);
    QString sql = "SELECT * FROM surcharge_strategies WHERE 1=1";
    QVariantList binds;
    if (!scope.isEmpty()) {
        sql += " AND strategy_scope = ?";
        binds << scope;
    }
    if (only_active) {
        sql += " AND is_active = 1";
    }
    sql += " ORDER BY priority DESC, created_at DESC";
    q.prepare(sql);
    for (const auto &b : binds) q.addBindValue(b);
    q.exec();
    while (q.next()) {
        QVariantMap m;
        m["strategy_id"] = q.value("strategy_id");
        m["strategy_name"] = q.value("strategy_name");
        m["strategy_scope"] = q.value("strategy_scope");
        m["template_id"] = q.value("template_id");
        m["strategy_type"] = q.value("strategy_type");
        m["amount"] = q.value("amount").toDouble();
        m["min_weight"] = q.value("min_weight").toDouble();
        m["max_weight"] = q.value("max_weight").toDouble();
        m["priority"] = q.value("priority").toInt();
        m["is_active"] = q.value("is_active").toInt() == 1;
        m["description"] = q.value("description");
        result << m;
    }
    return result;
}

QVariantMap SqliteRuleRepository::GetSurchargeStrategy(const QString &strategy_id) {
    QVariantMap m;
    QSqlQuery q(db_);
    q.prepare("SELECT * FROM surcharge_strategies WHERE strategy_id = ?");
    q.addBindValue(strategy_id);
    q.exec();
    if (q.next()) {
        m["strategy_id"] = q.value("strategy_id");
        m["strategy_name"] = q.value("strategy_name");
        m["strategy_scope"] = q.value("strategy_scope");
        m["template_id"] = q.value("template_id");
        m["strategy_type"] = q.value("strategy_type");
        m["amount"] = q.value("amount").toDouble();
        m["min_weight"] = q.value("min_weight").toDouble();
        m["max_weight"] = q.value("max_weight").toDouble();
        m["priority"] = q.value("priority").toInt();
        m["is_active"] = q.value("is_active").toInt() == 1;
        m["description"] = q.value("description");
    }
    return m;
}

bool SqliteRuleRepository::AddSurchargeStrategy(const QVariantMap &strategy) {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO surcharge_strategies "
              "(strategy_id, strategy_name, strategy_scope, template_id, strategy_type, "
              " amount, min_weight, max_weight, priority, is_active, description) "
              "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(strategy["strategy_id"].toString());
    q.addBindValue(strategy["strategy_name"].toString());
    q.addBindValue(strategy["strategy_scope"].toString());
    q.addBindValue(strategy["template_id"].toString());
    q.addBindValue(strategy["strategy_type"].toString());
    q.addBindValue(strategy["amount"].toDouble());
    double min_w = strategy["min_weight"].toDouble();
    double max_w = strategy["max_weight"].toDouble();
    q.addBindValue(min_w > 0 ? QVariant(min_w) : QVariant(QVariant::Double));
    q.addBindValue(max_w > 0 ? QVariant(max_w) : QVariant(QVariant::Double));
    q.addBindValue(strategy["priority"].toInt());
    q.addBindValue(strategy["is_active"].toBool() ? 1 : 0);
    q.addBindValue(strategy["description"].toString());
    return q.exec();
}

bool SqliteRuleRepository::UpdateSurchargeStrategy(const QVariantMap &strategy) {
    QSqlQuery q(db_);
    q.prepare("UPDATE surcharge_strategies SET "
              "strategy_name=?, strategy_scope=?, template_id=?, strategy_type=?, "
              "amount=?, min_weight=?, max_weight=?, priority=?, is_active=?, "
              "description=?, updated_at=CURRENT_TIMESTAMP "
              "WHERE strategy_id=?");
    q.addBindValue(strategy["strategy_name"].toString());
    q.addBindValue(strategy["strategy_scope"].toString());
    q.addBindValue(strategy["template_id"].toString());
    q.addBindValue(strategy["strategy_type"].toString());
    q.addBindValue(strategy["amount"].toDouble());
    double min_w = strategy["min_weight"].toDouble();
    double max_w = strategy["max_weight"].toDouble();
    q.addBindValue(min_w > 0 ? QVariant(min_w) : QVariant(QVariant::Double));
    q.addBindValue(max_w > 0 ? QVariant(max_w) : QVariant(QVariant::Double));
    q.addBindValue(strategy["priority"].toInt());
    q.addBindValue(strategy["is_active"].toBool() ? 1 : 0);
    q.addBindValue(strategy["description"].toString());
    q.addBindValue(strategy["strategy_id"].toString());
    return q.exec();
}

bool SqliteRuleRepository::DeleteSurchargeStrategy(const QString &strategy_id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM surcharge_provinces WHERE strategy_id = ?");
    q.addBindValue(strategy_id); q.exec();
    q.prepare("DELETE FROM surcharge_customers WHERE strategy_id = ?");
    q.addBindValue(strategy_id); q.exec();
    q.prepare("DELETE FROM surcharge_date_ranges WHERE strategy_id = ?");
    q.addBindValue(strategy_id); q.exec();
    q.prepare("DELETE FROM surcharge_strategies WHERE strategy_id = ?");
    q.addBindValue(strategy_id);
    return q.exec();
}

bool SqliteRuleRepository::SetSurchargeActive(const QString &strategy_id, bool active) {
    QSqlQuery q(db_);
    q.prepare("UPDATE surcharge_strategies SET is_active=?, updated_at=CURRENT_TIMESTAMP WHERE strategy_id=?");
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(strategy_id);
    return q.exec();
}

QStringList SqliteRuleRepository::GetSurchargeProvinces(const QString &strategy_id) {
    QStringList result;
    QSqlQuery q(db_);
    q.prepare("SELECT province FROM surcharge_provinces WHERE strategy_id = ?");
    q.addBindValue(strategy_id);
    while (q.next()) {
        result << q.value(0).toString();
    }
    return result;
}

bool SqliteRuleRepository::SetSurchargeProvinces(const QString &strategy_id, const QStringList &provinces) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM surcharge_provinces WHERE strategy_id = ?");
    q.addBindValue(strategy_id);
    q.exec();

    for (const auto &p : provinces) {
        q.prepare("INSERT INTO surcharge_provinces (strategy_id, province) VALUES (?,?)");
        q.addBindValue(strategy_id);
        q.addBindValue(p);
        q.exec();
    }
    return true;
}

QVariantList SqliteRuleRepository::ListCustomers() {
    QVariantList result;
    QSqlQuery q("SELECT * FROM customers ORDER BY created_at DESC", db_);
    while (q.next()) {
        QVariantMap m;
        m["customer_id"] = q.value("customer_id");
        m["customer_name"] = q.value("customer_name");
        m["discount_rate"] = q.value("discount_rate").toDouble();
        m["default_template"] = q.value("default_template");
        m["contact_person"] = q.value("contact_person");
        m["contact_phone"] = q.value("contact_phone");
        result << m;
    }
    return result;
}

QVariantMap SqliteRuleRepository::GetCustomer(const QString &customer_id) {
    QVariantMap m;
    QSqlQuery q(db_);
    q.prepare("SELECT * FROM customers WHERE customer_id = ?");
    q.addBindValue(customer_id);
    if (q.next()) {
        m["customer_id"] = q.value("customer_id");
        m["customer_name"] = q.value("customer_name");
        m["discount_rate"] = q.value("discount_rate").toDouble();
        m["default_template"] = q.value("default_template");
        m["contact_person"] = q.value("contact_person");
        m["contact_phone"] = q.value("contact_phone");
        m["address"] = q.value("address");
    }
    return m;
}

bool SqliteRuleRepository::AddCustomer(const QVariantMap &cust) {
    QSqlQuery q(db_);
    QString cust_id = cust["customer_id"].toString();

    q.prepare("INSERT INTO customers (customer_id, customer_name, discount_rate, default_template, contact_person, contact_phone, address) VALUES (?,?,?,?,?,?,?)");
    q.addBindValue(cust_id);
    q.addBindValue(cust["customer_name"].toString());
    q.addBindValue(cust.value("discount_rate", 1.0).toDouble());
    q.addBindValue(cust["default_template"].toString());
    q.addBindValue(cust["contact_person"].toString());
    q.addBindValue(cust["contact_phone"].toString());
    q.addBindValue(cust["address"].toString());
    if (!q.exec()) {
        return false;
    }

    // 为客户创建专属报价表（复制默认模板）
    QString tpl_id = "cust_" + cust_id;
    QString tpl_name = cust["customer_name"].toString() + "专属报价";

    q.prepare("INSERT INTO freight_templates (template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, description) VALUES (?,?,?,?,?,?,?)");
    q.addBindValue(tpl_id);
    q.addBindValue(tpl_name);
    q.addBindValue("客户专属");
    q.addBindValue(1.0);
    q.addBindValue(1.0);
    q.addBindValue(6000.0);
    q.addBindValue("客户专属报价表");
    q.exec();

    struct ZoneGroup {
        QString code; QString name; QStringList provinces;
        double t05_first; double t1_first; double t2_first; double t3_first;
        double mid_first; double mid_add; double big_first; double big_add;
    };

    QList<ZoneGroup> zones = {
        {"zone1", "一区", {"江苏","浙江","安徽","上海"},
            2.26, 2.46, 3.56, 4.76, 3.76, 0.8, 3.86, 0.8},
        {"zone2", "二区", {"山东","广东","福建","北京","河南","湖北","湖南","江西","天津","河北"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.1, 4.06, 1.3},
        {"zone3", "三区", {"山西","广西","四川","重庆","陕西","贵州","辽宁","吉林","黑龙江","云南"},
            2.26, 2.46, 3.56, 4.76, 3.76, 1.5, 4.06, 1.6},
        {"zone4", "四区", {"海南","甘肃","青海","内蒙古","宁夏"},
            2.56, 3.56, 4.06, 5.06, 3.76, 2.5, 4.06, 4.3},
        {"zone5", "五区-新疆", {"新疆"},
            10.0, 13.0, 20.0, 25.0, 15.0, 15.0, 15.0, 15.0},
        {"zone6", "五区-西藏", {"西藏"},
            13.0, 15.0, 25.0, 30.0, 15.0, 15.0, 15.0, 15.0},
    };

    for (int i = 0; i < zones.size(); i++) {
        const auto &z = zones[i];

        q.prepare("INSERT INTO zone_groups (template_id, group_code, group_name, sort_order) VALUES (?,?,?,?)");
        q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(z.name); q.addBindValue(i+1);
        q.exec();

        for (const auto &prov : z.provinces) {
            q.prepare("INSERT INTO zone_group_provinces (template_id, group_code, province) VALUES (?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code); q.addBindValue(prov);
            q.exec();
        }

        struct Tier {
            QString code; QString name;
            double min_w; double max_w;
            double first_w; double first_p;
            double add_u; double add_p;
            int sort;
        };

        QList<Tier> tiers = {
            {"tier_0_0.5", "0-0.5KG", 0, 0.5, 0.5, z.t05_first, 0.5, 0, 1},
            {"tier_0.5_1", "0.51KG-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
            {"tier_1_2", "1-2KG", 1.0, 2.0, 1.0, z.t2_first, 1.0, 0, 3},
            {"tier_2_3", "2-3KG", 2.0, 3.0, 1.0, z.t3_first, 1.0, 0, 4},
            {"tier_3_30", "3-30KG", 3.0, 30.0, 1.0, z.mid_first, 1.0, z.mid_add, 5},
            {"tier_30_plus", "30KG以上", 30.0, 9999.0, 1.0, z.big_first, 1.0, z.big_add, 6},
        };

        for (const auto &t : tiers) {
            q.prepare("INSERT INTO tiered_pricing "
                      "(template_id, group_code, tier_code, tier_name, min_weight, max_weight, "
                      " first_weight, first_price, additional_unit, additional_price, sort_order) "
                      "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
            q.addBindValue(tpl_id); q.addBindValue(z.code);
            q.addBindValue(t.code); q.addBindValue(t.name);
            q.addBindValue(t.min_w); q.addBindValue(t.max_w);
            q.addBindValue(t.first_w); q.addBindValue(t.first_p);
            q.addBindValue(t.add_u); q.addBindValue(t.add_p);
            q.addBindValue(t.sort);
            q.exec();
        }
    }

    q.prepare("INSERT INTO fuel_surcharge (template_id, effective_date, rate) VALUES (?,?,?)");
    q.addBindValue(tpl_id); q.addBindValue("2026-01-01"); q.addBindValue(0.0);
    q.exec();

    return true;
}

bool SqliteRuleRepository::UpdateCustomer(const QVariantMap &cust) {
    QSqlQuery q(db_);
    q.prepare("UPDATE customers SET customer_name=?, discount_rate=?, default_template=?, contact_person=?, contact_phone=?, address=?, updated_at=CURRENT_TIMESTAMP WHERE customer_id=?");
    q.addBindValue(cust["customer_name"].toString());
    q.addBindValue(cust["discount_rate"].toDouble());
    q.addBindValue(cust["default_template"].toString());
    q.addBindValue(cust["contact_person"].toString());
    q.addBindValue(cust["contact_phone"].toString());
    q.addBindValue(cust["address"].toString());
    q.addBindValue(cust["customer_id"].toString());
    return q.exec();
}

bool SqliteRuleRepository::DeleteCustomer(const QString &customer_id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM customers WHERE customer_id = ?");
    q.addBindValue(customer_id);
    return q.exec();
}

QVariantList SqliteRuleRepository::ListFuelSurcharges(const QString &template_id) {
    QVariantList result;
    QSqlQuery q(db_);
    q.prepare("SELECT id, template_id, effective_date, rate, is_active FROM fuel_surcharge WHERE template_id = ? ORDER BY effective_date DESC");
    q.addBindValue(template_id);
    q.exec();
    while (q.next()) {
        QVariantMap m;
        m["id"] = q.value(0).toInt();
        m["template_id"] = q.value(1).toString();
        m["effective_date"] = q.value(2).toString();
        m["rate"] = q.value(3).toDouble();
        m["is_active"] = q.value(4).toInt() == 1;
        result << m;
    }
    return result;
}

bool SqliteRuleRepository::AddFuelSurcharge(const QVariantMap &fuel) {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO fuel_surcharge (template_id, effective_date, rate, is_active) VALUES (?,?,?,1)");
    q.addBindValue(fuel["template_id"].toString());
    q.addBindValue(fuel["effective_date"].toString());
    q.addBindValue(fuel["rate"].toDouble());
    if (!q.exec()) {
        qCritical() << "AddFuelSurcharge failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool SqliteRuleRepository::UpdateFuelSurcharge(int id, const QVariantMap &fuel) {
    QSqlQuery q(db_);
    q.prepare("UPDATE fuel_surcharge SET effective_date=?, rate=? WHERE id=?");
    q.addBindValue(fuel["effective_date"].toString());
    q.addBindValue(fuel["rate"].toDouble());
    q.addBindValue(id);
    return q.exec();
}

bool SqliteRuleRepository::DeleteFuelSurcharge(int id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM fuel_surcharge WHERE id = ?");
    q.addBindValue(id);
    return q.exec();
}

bool SqliteRuleRepository::SetFuelSurchargeActive(int id, bool active) {
    QSqlQuery q(db_);
    q.prepare("UPDATE fuel_surcharge SET is_active=? WHERE id=?");
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

QVariantList SqliteRuleRepository::ListRemoteAreas(const QString &template_id) {
    QVariantList result;
    QSqlQuery q(db_);
    q.prepare("SELECT id, template_id, province, city, district, surcharge, is_active FROM remote_areas WHERE template_id = ? ORDER BY province, city, district");
    q.addBindValue(template_id);
    q.exec();
    while (q.next()) {
        QVariantMap m;
        m["id"] = q.value(0).toInt();
        m["template_id"] = q.value(1).toString();
        m["province"] = q.value(2).toString();
        m["city"] = q.value(3).toString();
        m["district"] = q.value(4).toString();
        m["surcharge"] = q.value(5).toDouble();
        m["is_active"] = q.value(6).toInt() == 1;
        result << m;
    }
    return result;
}

bool SqliteRuleRepository::AddRemoteArea(const QVariantMap &area) {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO remote_areas (template_id, province, city, district, surcharge, is_active) VALUES (?,?,?,?,?,1)");
    q.addBindValue(area["template_id"].toString());
    q.addBindValue(area["province"].toString());
    q.addBindValue(area["city"].toString());
    q.addBindValue(area["district"].toString());
    q.addBindValue(area["surcharge"].toDouble());
    if (!q.exec()) {
        qCritical() << "AddRemoteArea failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool SqliteRuleRepository::UpdateRemoteArea(int id, const QVariantMap &area) {
    QSqlQuery q(db_);
    q.prepare("UPDATE remote_areas SET province=?, city=?, district=?, surcharge=? WHERE id=?");
    q.addBindValue(area["province"].toString());
    q.addBindValue(area["city"].toString());
    q.addBindValue(area["district"].toString());
    q.addBindValue(area["surcharge"].toDouble());
    q.addBindValue(id);
    return q.exec();
}

bool SqliteRuleRepository::DeleteRemoteArea(int id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM remote_areas WHERE id = ?");
    q.addBindValue(id);
    return q.exec();
}

bool SqliteRuleRepository::SetRemoteAreaActive(int id, bool active) {
    QSqlQuery q(db_);
    q.prepare("UPDATE remote_areas SET is_active=? WHERE id=?");
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

} // namespace freight::db
