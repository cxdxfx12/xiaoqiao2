#include "db/sqlite_rule_repository.hpp"
#include <QSqlError>
#include <QSqlRecord>
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

    // schema 升级到 11：增加无重量默认运费
    // 说明：此处及以下分段版本升级统一用 current_version < N 判断，
    //       目的是让 v10→v15 大跨度升级时 11/12/13/14/15 每个阶段都能被触发。
    //       每个阶段用 tryAddCol 容错 duplicate column，可重入。
    if (current_version < 11) {
        qInfo() << "Upgrading schema -> 11";
        QSqlQuery aq(db_);
        aq.exec("ALTER TABLE freight_templates ADD COLUMN default_no_weight_fee REAL DEFAULT 0");
    }

    // 默认数据统一只初始化一次
    // （旧版本 schema 升级 <10 必须初始化，新版本第一次运行也初始化）
    bool need_init_default = (current_version < 10) || IsFirstRun();
    if (need_init_default) {
        if (current_version < 10) {
            qInfo() << "Schema upgrade init: generating default data";
        } else {
            qInfo() << "First run: generating default data";
        }
        InitDefaultData();
        // 修复前4个一口价阶梯的 additional_price 为0
        // （新版 CreateDefaultTemplate 已正确写入 add_price=0，此修复主要兼容旧数据残留的情况）
        QSqlQuery fix_q(db_);
        fix_q.exec("UPDATE tiered_pricing SET additional_price = 0 WHERE tier_code IN ('tier_0_0.5', 'tier_0.5_1', 'tier_1_2', 'tier_2_3')");
    }

    // 完整性兜底校验：即使 schema_version=11，但核心空表也视为损坏，全量补全默认数据
    if (!need_init_default) {
        if (!ValidateIntegrity()) {
            qWarning() << "Integrity check failed: core tables empty, reinitializing default data...";
            InitDefaultData();
            QSqlQuery fix_q(db_);
            fix_q.exec("UPDATE tiered_pricing SET additional_price = 0 WHERE tier_code IN ('tier_0_0.5', 'tier_0.5_1', 'tier_1_2', 'tier_2_3')");
        }
    }

    // ============================================================
    // Schema 11→12 数据一致性迁移：
    // 早期 AddCustomer 创建完『cust_{customer_id}』专属报价模板后，
    // 忘记 UPDATE customers.default_template = tpl_id，导致批量结算
    // COALESCE(c.default_template, 'zto_standard') 全回退到中通标准，
    // 燃油/加价策略/地区加价全部按中通而不是客户专属算。
    // 这里自动回填：template_id = 'cust_' + customer_id，且模板存在就回填。
    // ============================================================
    {
        QSqlQuery qsel(db_);
        qsel.exec("SELECT customer_id FROM customers WHERE default_template IS NULL OR default_template = ''");
        QVariantList custs_to_fix;
        while (qsel.next()) custs_to_fix << qsel.value(0);

        if (!custs_to_fix.isEmpty()) {
            qInfo() << "[data-fix] 检测到" << custs_to_fix.size() << "条 customers.default_template 空记录，尝试按 cust_{id} 匹配专属模板回填";
            QSqlQuery qexists(db_);
            QSqlQuery qupdate(db_);
            qexists.prepare("SELECT 1 FROM freight_templates WHERE template_id = ? LIMIT 1");
            qupdate.prepare("UPDATE customers SET default_template = ?, updated_at = CURRENT_TIMESTAMP WHERE customer_id = ?");
            int fixed = 0;
            for (const auto &cidv : custs_to_fix) {
                QString cid = cidv.toString();
                QString tpl_id = "cust_" + cid;
                qexists.addBindValue(tpl_id);
                if (qexists.exec() && qexists.next()) {
                    qupdate.addBindValue(tpl_id);
                    qupdate.addBindValue(cid);
                    if (qupdate.exec()) fixed++;
                }
            }
            qInfo() << "[data-fix] 成功回填" << fixed << "条 customers.default_template";
        }
    }

    // ============================================================
    // Schema 12→13 全局模板迁移：
    // 早期 fuel_surcharge / remote_areas 只绑定到 zto_standard，
    // 新版代码支持 template_id='*' 代表全局（所有模板通用），
    // 但存量老用户数据仍然全是 zto_standard，导致切客户模板
    // （蜜丝婷/珀莱雅专属报价）时 IN(当前模板, '*') 查不到记录
    // → 运费计算漏 1 元地区加价 / 燃油 %。
    // 迁移策略：
    //   1. remote_areas：对每个 (province,city,district) 组合，
    //      若 template_id='*' 不存在，就把 zto_standard 的记录
    //      INSERT OR IGNORE 为 *=全局版本，保留 zto_standard
    //      原记录（不删，怕用户确实要在中通模板差异化）。金额相同时
    //      查询 UNION IN(当前,*) 会按模板优先级各加一次，通常
    //      用户会手动删除 zto_standard 多余那条，不删也能接受。
    //   2. fuel_surcharge：同理，每个 effective_date，若 *=全局
    //      不存在就把 zto_standard 的 rate 复制到 *=全局。
    // ============================================================
    if (current_version < 13) {
        qInfo() << "[data-fix] Schema 12→13: 升级 zto_standard 绑定的 remote/fuel 记录为全局 * 生效模板";
        {
            QSqlQuery qmig(db_);
            qmig.prepare(R"SQL(
                INSERT OR IGNORE INTO remote_areas
                    (template_id, province, city, district, surcharge, is_active)
                SELECT '*', province, city, district, surcharge, is_active
                FROM remote_areas AS src
                WHERE src.template_id = 'zto_standard'
                  AND NOT EXISTS (
                      SELECT 1 FROM remote_areas AS t
                      WHERE t.template_id = '*'
                        AND COALESCE(t.province, '') = COALESCE(src.province, '')
                        AND COALESCE(t.city, '')     = COALESCE(src.city, '')
                        AND COALESCE(t.district, '') = COALESCE(src.district, '')
                  )
            )SQL");
            if (!qmig.exec())
                qCritical() << "remote_areas migrate failed:" << qmig.lastError().text();
            else
                qInfo() << "  remote_areas 迁移" << qmig.numRowsAffected() << "行 → 模板=*";
        }
        {
            QSqlQuery qmig(db_);
            qmig.prepare(R"SQL(
                INSERT OR IGNORE INTO fuel_surcharge
                    (template_id, effective_date, rate, is_active)
                SELECT '*', effective_date, rate, is_active
                FROM fuel_surcharge AS src
                WHERE src.template_id = 'zto_standard'
                  AND NOT EXISTS (
                      SELECT 1 FROM fuel_surcharge AS t
                      WHERE t.template_id = '*'
                        AND t.effective_date = src.effective_date
                  )
            )SQL");
            if (!qmig.exec())
                qCritical() << "fuel_surcharge migrate failed:" << qmig.lastError().text();
            else
                qInfo() << "  fuel_surcharge 迁移" << qmig.numRowsAffected() << "行 → 模板=*";
        }
    }

    // ============================================================
    // Schema 13→14 模板计费参数升级：
    // 新增 freight_templates.tpl_rounding_mode / tpl_additional_unit / tpl_vol_divisor
    // 三列，把历史 additional_unit / vol_weight_ratio 老字段值回填到新列。
    // 原因：续重进位规则(ceil_0_1kg等)、续重单位预设下拉、体积重除数预设下拉
    // 三参数在 template_edit_dialog 已独立为计费参数卡片 UI，独立三列读取
    // 比老 REAL 类型 additional_unit/vol_weight_ratio 更直观，保留老列同时
    // 读写新列保证向后兼容（80% 普通散客代码也能继续读老列兜底）。
    // ============================================================
    if (current_version < 14) {
        qInfo() << "[data-fix] Schema 13→14: freight_templates 计费参数三列升级(tpl_rounding/tpl_add_unit/tpl_vol)";
        auto tryAddCol = [&](const QString &addSql) {
            QSqlQuery qt(db_);
            if (!qt.exec(addSql)) {
                const QString err = qt.lastError().text().toLower();
                // SQLite 报 duplicate column 正常跳过（列已存在）
                if (err.contains("duplicate column") || err.contains("already exists"))
                    return true;
                qCritical() << "add column failed:" << qt.lastError().text() << "SQL=" << addSql;
                return false;
            }
            return true;
        };
        bool okMig = true;
        okMig &= tryAddCol("ALTER TABLE freight_templates ADD COLUMN tpl_rounding_mode VARCHAR(30) DEFAULT 'ceil_0_1kg'");
        okMig &= tryAddCol("ALTER TABLE freight_templates ADD COLUMN tpl_additional_unit REAL DEFAULT 1.0");
        okMig &= tryAddCol("ALTER TABLE freight_templates ADD COLUMN tpl_vol_divisor INTEGER DEFAULT 6000");
        if (okMig) {
            // 回填老数据：additional_unit → tpl_additional_unit；vol_weight_ratio(CAST INT) → tpl_vol_divisor
            {
                QSqlQuery qt(db_);
                qt.prepare(R"SQL(
                    UPDATE freight_templates SET
                      tpl_rounding_mode    = COALESCE(NULLIF(tpl_rounding_mode, ''), 'ceil_0_1kg'),
                      tpl_additional_unit  = CASE WHEN COALESCE(tpl_additional_unit, 0) > 0 THEN tpl_additional_unit
                                                  ELSE COALESCE(NULLIF(additional_unit, 0), 1.0) END,
                      tpl_vol_divisor      = CASE WHEN COALESCE(tpl_vol_divisor, 0) > 0 THEN tpl_vol_divisor
                                                  ELSE COALESCE(CAST(NULLIF(vol_weight_ratio, 0) AS INTEGER), 6000) END
                )SQL");
                if (!qt.exec())
                    qCritical() << "backfill failed:" << qt.lastError().text();
                else
                    qInfo() << "  freight_templates 回填" << qt.numRowsAffected() << "行计费参数";
            }
        }
    }

    // ============================================================
    // Schema 14→15 客户级覆写 + 独立拉均重合同表（DESIGN_v1.2）：
    //   1. customers 加 5 列客户覆写 + 拉均重外键
    //      （cust_rounding_mode / cust_additional_unit / cust_vol_divisor
    //       / avg_weight_tpl_id / cust_contract_no）
    //   2. freight_templates 不加额外列（13→14 已经加了 tpl_rounding/
    //      add_unit/vol_divisor；v1.2 起已不再用 v1.0 的 tpl_avg_* 三列）
    //   3. 新建 avg_weight_templates 21 列 + avg_weight_zones 4 列 + 索引
    //      （独立拉均重合同表，快递小管家/快宝同构）
    // 所有 ADD COLUMN 用 tryAddCol 容错 duplicate column，保证多次升级可重入。
    // ============================================================
    if (current_version < 15) {
        qInfo() << "[migrate] Schema 14→15: customers+5列、新增独立拉均重合同两张表";
        auto tryAddCol = [&](const QString &addSql) {
            QSqlQuery qt(db_);
            if (!qt.exec(addSql)) {
                const QString err = qt.lastError().text().toLower();
                if (err.contains("duplicate column") || err.contains("already exists"))
                    return true;
                qCritical() << "  ALTER failed:" << qt.lastError().text() << "SQL=" << addSql;
                return false;
            }
            return true;
        };
        bool okMig = true;
        okMig &= tryAddCol("ALTER TABLE customers ADD COLUMN cust_rounding_mode VARCHAR(30) DEFAULT ''");
        okMig &= tryAddCol("ALTER TABLE customers ADD COLUMN cust_additional_unit REAL DEFAULT 0");
        okMig &= tryAddCol("ALTER TABLE customers ADD COLUMN cust_vol_divisor INTEGER DEFAULT 0");
        okMig &= tryAddCol("ALTER TABLE customers ADD COLUMN avg_weight_tpl_id VARCHAR(60)");
        okMig &= tryAddCol("ALTER TABLE customers ADD COLUMN cust_contract_no VARCHAR(60) DEFAULT ''");
        if (!okMig) {
            qWarning() << "  [warn] 部分 customers 列 ADD 失败，可能已存在；继续建表";
        }
        // 建两张独立均重合同表（CREATE TABLE IF NOT EXISTS，天然可重入）
        {
            QSqlQuery qc(db_);
            qc.exec(R"SQL(
                CREATE TABLE IF NOT EXISTS avg_weight_templates (
                    avg_tpl_id          VARCHAR(60) PRIMARY KEY,
                    template_id         VARCHAR(100) NOT NULL,
                    name                VARCHAR(200) NOT NULL,
                    version             INTEGER     DEFAULT 1,
                    effective_from      DATE        NOT NULL DEFAULT CURRENT_DATE,
                    effective_to        DATE,
                    avg_pool_min_kg     REAL        DEFAULT 0.0,
                    avg_pool_max_kg     REAL        DEFAULT 1.0,
                    base_avg_kg         REAL        DEFAULT 0.3,
                    avg_fee_cap_kg      REAL        DEFAULT 1.0,
                    over_cap_mode       INTEGER     DEFAULT 0,
                    threshold_kg        REAL        DEFAULT 1.0,
                    base_fee            REAL        NOT NULL DEFAULT 2.7,
                    step_kg             REAL        DEFAULT 0.1,
                    step_fee            REAL        DEFAULT 0.2,
                    min_tickets         INTEGER     DEFAULT 50,
                    reuse_zone_groups   INTEGER     DEFAULT 1,
                    period_type         VARCHAR(10) DEFAULT 'month',
                    contract_no         VARCHAR(60) DEFAULT '',
                    is_active           INTEGER     DEFAULT 1,
                    created_at          TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
                    updated_at          TIMESTAMP   DEFAULT CURRENT_TIMESTAMP
                )
            )SQL");
            if (qc.lastError().isValid())
                qCritical() << "  CREATE avg_weight_templates failed:" << qc.lastError().text();
            else
                qInfo() << "  ✓ avg_weight_templates 表就绪（21列）";
        }
        {
            QSqlQuery qc(db_);
            qc.exec(R"SQL(
                CREATE TABLE IF NOT EXISTS avg_weight_zones (
                    id         INTEGER PRIMARY KEY AUTOINCREMENT,
                    avg_tpl_id VARCHAR(60) NOT NULL,
                    zone_code  VARCHAR(20) NOT NULL,
                    province   VARCHAR(50) NOT NULL,
                    UNIQUE(avg_tpl_id, zone_code, province)
                )
            )SQL");
            if (qc.lastError().isValid())
                qCritical() << "  CREATE avg_weight_zones failed:" << qc.lastError().text();
            else
                qInfo() << "  ✓ avg_weight_zones 表就绪";
            qc.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id)");
        }
        {
            QSqlQuery qc(db_);
            qc.exec(R"SQL(
                CREATE TABLE IF NOT EXISTS avg_weight_zone_tpl_groups (
                    avg_tpl_id  VARCHAR(60) NOT NULL,
                    template_id VARCHAR(100) NOT NULL,
                    group_code  VARCHAR(20) NOT NULL,
                    PRIMARY KEY (avg_tpl_id, template_id, group_code)
                )
            )SQL");
            if (qc.lastError().isValid())
                qCritical() << "  CREATE avg_weight_zone_tpl_groups failed:" << qc.lastError().text();
            else
                qInfo() << "  ✓ avg_weight_zone_tpl_groups 表就绪（方案A勾选模板分区）";
        }
        {
            QSqlQuery qc(db_);
            qc.exec(R"SQL(
                CREATE TABLE IF NOT EXISTS avg_weight_zone_excludes (
                    avg_tpl_id  VARCHAR(60) NOT NULL,
                    template_id VARCHAR(100) NOT NULL,
                    group_code  VARCHAR(20) NOT NULL,
                    province    VARCHAR(50) NOT NULL,
                    PRIMARY KEY (avg_tpl_id, template_id, group_code, province)
                )
            )SQL");
            if (qc.lastError().isValid())
                qCritical() << "  CREATE avg_weight_zone_excludes failed:" << qc.lastError().text();
            else
                qInfo() << "  ✓ avg_weight_zone_excludes 表就绪（方案A排除省）";
            qc.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zone_excludes_tpl ON avg_weight_zone_excludes(avg_tpl_id)");
        }
    }

    // 更新 schema 版本
    int new_version = 15;
    if (current_version < new_version) {
        vq.exec("DELETE FROM schema_version");
        vq.prepare("INSERT INTO schema_version VALUES (?)");
        vq.addBindValue(new_version);
        vq.exec();
        qInfo() << "[migrate] Schema version 升级完成 → v" << new_version;
    }

    // ============================================================
    // 【兜底三层保险·第二层】  拉均重合同 4 张核心表强制补全
    //   不依赖任何 schema_version 判断：
    //   - 老版本库 schema_version 已冲到 15，但当时 CreateTables 缺
    //     avg_weight_zone_tpl_groups / avg_weight_zone_excludes
    //   - 迁移块 CREATE TABLE 执行失败但 lastError() 丢失
    //   → 所有这些情况，启动时都会在这里被 CREATE TABLE IF NOT EXISTS 兜底补建
    // ============================================================
    {
        auto try_exec = [&](const QString &name, const QString &sql) {
            QSqlQuery qc(db_);
            if (!qc.exec(sql)) {
                const QString err = qc.lastError().text().toLower();
                if (err.contains("already exists") || err.contains("duplicate") || err.contains("already"))
                    return true;
                qCritical() << "[lajz-ensure-schema]" << name << "CREATE failed:" << qc.lastError().text();
                return false;
            }
            qInfo() << "[lajz-ensure-schema]  ✓ " << name << "表就绪";
            return true;
        };
        try_exec("avg_weight_templates", R"SQL(
            CREATE TABLE IF NOT EXISTS avg_weight_templates (
                avg_tpl_id          VARCHAR(60) PRIMARY KEY,
                template_id         VARCHAR(100) NOT NULL,
                name                VARCHAR(200) NOT NULL,
                version             INTEGER     DEFAULT 1,
                effective_from      DATE        NOT NULL DEFAULT CURRENT_DATE,
                effective_to        DATE,
                avg_pool_min_kg     REAL        DEFAULT 0.0,
                avg_pool_max_kg     REAL        DEFAULT 1.0,
                base_avg_kg         REAL        DEFAULT 0.3,
                avg_fee_cap_kg      REAL        DEFAULT 1.0,
                over_cap_mode       INTEGER     DEFAULT 0,
                threshold_kg        REAL        DEFAULT 1.0,
                base_fee            REAL        NOT NULL DEFAULT 2.7,
                step_kg             REAL        DEFAULT 0.1,
                step_fee            REAL        DEFAULT 0.2,
                min_tickets         INTEGER     DEFAULT 50,
                reuse_zone_groups   INTEGER     DEFAULT 1,
                period_type         VARCHAR(10) DEFAULT 'month',
                contract_no         VARCHAR(60) DEFAULT '',
                is_active           INTEGER     DEFAULT 1,
                created_at          TIMESTAMP   DEFAULT CURRENT_TIMESTAMP,
                updated_at          TIMESTAMP   DEFAULT CURRENT_TIMESTAMP
            )
        )SQL");
        try_exec("avg_weight_zones", R"SQL(
            CREATE TABLE IF NOT EXISTS avg_weight_zones (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                avg_tpl_id VARCHAR(60) NOT NULL,
                zone_code  VARCHAR(20) NOT NULL,
                province   VARCHAR(50) NOT NULL,
                UNIQUE(avg_tpl_id, zone_code, province)
            )
        )SQL");
        try_exec("avg_weight_zones_idx",
                 "CREATE INDEX IF NOT EXISTS idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id)");
        try_exec("avg_weight_zone_tpl_groups", R"SQL(
            CREATE TABLE IF NOT EXISTS avg_weight_zone_tpl_groups (
                avg_tpl_id  VARCHAR(60) NOT NULL,
                template_id VARCHAR(100) NOT NULL,
                group_code  VARCHAR(20) NOT NULL,
                PRIMARY KEY (avg_tpl_id, template_id, group_code)
            )
        )SQL");
        try_exec("avg_weight_zone_excludes", R"SQL(
            CREATE TABLE IF NOT EXISTS avg_weight_zone_excludes (
                avg_tpl_id  VARCHAR(60) NOT NULL,
                template_id VARCHAR(100) NOT NULL,
                group_code  VARCHAR(20) NOT NULL,
                province    VARCHAR(50) NOT NULL,
                PRIMARY KEY (avg_tpl_id, template_id, group_code, province)
            )
        )SQL");
        try_exec("avg_weight_zone_excludes_idx",
                 "CREATE INDEX IF NOT EXISTS idx_avg_weight_zone_excludes_tpl ON avg_weight_zone_excludes(avg_tpl_id)");
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
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "cust_rounding_mode VARCHAR(30) DEFAULT '',"
           "cust_additional_unit REAL DEFAULT 0,"
           "cust_vol_divisor INTEGER DEFAULT 0,"
           "avg_weight_tpl_id VARCHAR(60),"
           "cust_contract_no VARCHAR(60) DEFAULT '')");

    q.exec("CREATE TABLE IF NOT EXISTS freight_templates ("
           "template_id VARCHAR(100) PRIMARY KEY,"
           "template_name VARCHAR(200) NOT NULL,"
           "carrier_name VARCHAR(100),"
           "first_weight REAL DEFAULT 1.0,"
           "additional_unit REAL DEFAULT 1.0,"
           "vol_weight_ratio REAL DEFAULT 6000.0,"
           "default_no_weight_fee REAL DEFAULT 0,"
           "description TEXT,"
           "is_default INTEGER DEFAULT 0,"
           "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "tpl_rounding_mode VARCHAR(30) DEFAULT 'ceil_0_1kg',"
           "tpl_additional_unit REAL DEFAULT 1.0,"
           "tpl_vol_divisor INTEGER DEFAULT 6000)");

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
           "template_id VARCHAR(100) DEFAULT '*' NOT NULL,"
           "effective_date DATE NOT NULL,"
           "rate REAL NOT NULL,"
           "is_active INTEGER DEFAULT 1)");

    q.exec("CREATE TABLE IF NOT EXISTS remote_areas ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "template_id VARCHAR(100) DEFAULT '*' NOT NULL,"
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

    q.exec("CREATE TABLE IF NOT EXISTS avg_weight_templates ("
           "avg_tpl_id VARCHAR(60) PRIMARY KEY,"
           "template_id VARCHAR(100) NOT NULL,"
           "name VARCHAR(200) NOT NULL,"
           "version INTEGER DEFAULT 1,"
           "effective_from DATE NOT NULL DEFAULT CURRENT_DATE,"
           "effective_to DATE,"
           "avg_pool_min_kg REAL DEFAULT 0.0,"
           "avg_pool_max_kg REAL DEFAULT 1.0,"
           "base_avg_kg REAL DEFAULT 0.3,"
           "avg_fee_cap_kg REAL DEFAULT 1.0,"
           "over_cap_mode INTEGER DEFAULT 0,"
           "threshold_kg REAL DEFAULT 1.0,"
           "base_fee REAL NOT NULL DEFAULT 2.7,"
           "step_kg REAL DEFAULT 0.1,"
           "step_fee REAL DEFAULT 0.2,"
           "min_tickets INTEGER DEFAULT 50,"
           "reuse_zone_groups INTEGER DEFAULT 1,"
           "period_type VARCHAR(10) DEFAULT 'month',"
           "contract_no VARCHAR(60) DEFAULT '',"
           "is_active INTEGER DEFAULT 1,"
           "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");

    q.exec("CREATE TABLE IF NOT EXISTS avg_weight_zones ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "avg_tpl_id VARCHAR(60) NOT NULL,"
           "zone_code VARCHAR(20) NOT NULL,"
           "province VARCHAR(50) NOT NULL,"
           "UNIQUE(avg_tpl_id, zone_code, province))");
    if (q.lastError().isValid()) {
        qCritical() << "CREATE avg_weight_zones failed:" << q.lastError().text();
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id)");

    q.exec("CREATE TABLE IF NOT EXISTS avg_weight_zone_tpl_groups ("
           "avg_tpl_id  VARCHAR(60) NOT NULL,"
           "template_id VARCHAR(100) NOT NULL,"
           "group_code  VARCHAR(20) NOT NULL,"
           "PRIMARY KEY (avg_tpl_id, template_id, group_code))");
    if (q.lastError().isValid()) {
        qCritical() << "CREATE avg_weight_zone_tpl_groups failed:" << q.lastError().text();
    }

    q.exec("CREATE TABLE IF NOT EXISTS avg_weight_zone_excludes ("
           "avg_tpl_id  VARCHAR(60) NOT NULL,"
           "template_id VARCHAR(100) NOT NULL,"
           "group_code  VARCHAR(20) NOT NULL,"
           "province    VARCHAR(50) NOT NULL,"
           "PRIMARY KEY (avg_tpl_id, template_id, group_code, province))");
    if (q.lastError().isValid()) {
        qCritical() << "CREATE avg_weight_zone_excludes failed:" << q.lastError().text();
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zone_excludes_tpl ON avg_weight_zone_excludes(avg_tpl_id)");

    return true;
}

void SqliteRuleRepository::InitDefaultData() {
    qDebug() << "Initializing default data...";

    CreateDefaultTemplate();
    CreateDefaultCustomers();
    CreateDefaultSurcharges();
    qDebug() << "Default data initialized.";
}

bool SqliteRuleRepository::CreateDefaultTemplate() {
    QSqlQuery q(db_);
    QString tpl_id = "zto_standard";

    q.prepare("INSERT OR REPLACE INTO freight_templates "
              "(template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, default_no_weight_fee, is_default, description) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(tpl_id);
    q.addBindValue("中通标准快递");
    q.addBindValue("中通");
    q.addBindValue(1.0);
    q.addBindValue(1.0);
    q.addBindValue(6000.0);
    q.addBindValue(0.0);
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
            {"tier_0.5_1", "0.5-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
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

    addStrategy("remote_xz_xj", "新疆西藏地区加价",
                "province", "per_weight", 2.0, 20,
                "新疆、西藏地区每公斤加收2元",
                {"新疆", "西藏"});

    addStrategy("peak_season", "旺季附加费",
                "global", "percentage", 0.10, 5,
                "旺季运费上浮10%（暂未启用日期限制）");

    QString tpl_id = "*";

    q.prepare("INSERT OR IGNORE INTO fuel_surcharge (template_id, effective_date, rate, is_active) VALUES (?,?,?,1)");
    QList<QPair<QString, double>> fuel_list = {
        {"2026-01-01", 0.00},
        {"2026-02-01", 0.02},
        {"2026-03-01", 0.03},
        {"2026-04-01", 0.025},
        {"2026-05-01", 0.015},
    };
    for (const auto &f : fuel_list) {
        q.addBindValue(tpl_id); q.addBindValue(f.first); q.addBindValue(f.second);
        q.exec();
    }

    q.prepare("INSERT OR IGNORE INTO remote_areas (template_id, province, city, district, surcharge, is_active) VALUES (?,?,?,?,?,1)");
    QList<QPair<QString, double>> remote_list = {
        {"新疆", 15.0},
        {"西藏", 20.0},
    };
    for (const auto &r : remote_list) {
        q.addBindValue(tpl_id); q.addBindValue(r.first);
        q.addBindValue(""); q.addBindValue(""); q.addBindValue(r.second);
        q.exec();
    }

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
        m["default_no_weight_fee"] = q.value("default_no_weight_fee").toDouble();
        m["is_default"] = q.value("is_default").toInt() == 1;
        m["description"] = q.value("description");

        // 新 3 列（schema 14）：优先新列，其次兼容老列
        QVariant rmode;
        bool newColPresent = false;
        for (int i = 0; i < q.record().count(); ++i) {
            const QString cn = q.record().fieldName(i).toLower();
            if (cn == "tpl_rounding_mode")   { rmode = q.value(i); newColPresent = true; }
        }
        QString rounding_mode = rmode.toString().trimmed();
        if (rounding_mode.isEmpty()) rounding_mode = "ceil_0_1kg";
        m["tpl_rounding_mode"] = rounding_mode;

        // tpl_additional_unit（新列兼容老 additional_unit）
        double tpl_add = q.record().contains("tpl_additional_unit")
                                ? q.value("tpl_additional_unit").toDouble()
                                : 0.0;
        if (tpl_add <= 0) tpl_add = m["additional_unit"].toDouble();
        if (tpl_add <= 0) tpl_add = 1.0;
        m["tpl_additional_unit"] = tpl_add;

        // tpl_vol_divisor（新列兼容老 vol_weight_ratio）
        int tpl_vol = q.record().contains("tpl_vol_divisor")
                            ? q.value("tpl_vol_divisor").toInt()
                            : 0;
        if (tpl_vol <= 0) {
            double v = m["vol_weight_ratio"].toDouble();
            tpl_vol = v > 1 ? static_cast<int>(v) : 6000;
        }
        m["tpl_vol_divisor"] = tpl_vol;
        Q_UNUSED(newColPresent);
    }
    return m;
}

bool SqliteRuleRepository::AddTemplate(const QVariantMap &tpl) {
    QSqlQuery q(db_);
    QString tpl_id = tpl["template_id"].toString();

    // 新3列 + 老3列 双写
    const QString rmode = tpl.value("tpl_rounding_mode", "ceil_0_1kg").toString().trimmed();
    const double add_unit = tpl.contains("tpl_additional_unit")
                                ? tpl["tpl_additional_unit"].toDouble()
                                : tpl.value("additional_unit", 1.0).toDouble();
    const int vol_div = tpl.contains("tpl_vol_divisor")
                            ? tpl["tpl_vol_divisor"].toInt()
                            : static_cast<int>(tpl.value("vol_weight_ratio", 6000.0).toDouble());

    // 先尝试 11 列（含新3列）schema 14 全写；失败时 fallback 到老 8 列（旧 DB schema）
    QString sqlInsert = QStringLiteral(
        "INSERT INTO freight_templates "
        "(template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio,"
        " default_no_weight_fee, description,"
        " tpl_rounding_mode, tpl_additional_unit, tpl_vol_divisor)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    q.prepare(sqlInsert);
    q.addBindValue(tpl_id);
    q.addBindValue(tpl["template_name"].toString());
    q.addBindValue(tpl["carrier_name"].toString());
    q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
    q.addBindValue(add_unit);
    q.addBindValue(static_cast<double>(vol_div));
    q.addBindValue(tpl.value("default_no_weight_fee", 0.0).toDouble());
    q.addBindValue(tpl["description"].toString());
    q.addBindValue(rmode);
    q.addBindValue(add_unit);
    q.addBindValue(vol_div);
    if (!q.exec()) {
        // fallback：老 schema（没有新3列的 DB），只用老 8 列 INSERT
        q.clear();
        q.prepare("INSERT INTO freight_templates "
                  "(template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio,"
                  " default_no_weight_fee, description) VALUES (?,?,?,?,?,?,?,?)");
        q.addBindValue(tpl_id);
        q.addBindValue(tpl["template_name"].toString());
        q.addBindValue(tpl["carrier_name"].toString());
        q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
        q.addBindValue(add_unit);
        q.addBindValue(static_cast<double>(vol_div));
        q.addBindValue(tpl.value("default_no_weight_fee", 0.0).toDouble());
        q.addBindValue(tpl["description"].toString());
        if (!q.exec()) return false;
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
            {"tier_0.5_1", "0.5-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
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

    const QString rmode = tpl.value("tpl_rounding_mode", "ceil_0_1kg").toString().trimmed();
    const double add_unit = tpl.contains("tpl_additional_unit")
                                ? tpl["tpl_additional_unit"].toDouble()
                                : tpl.value("additional_unit", 1.0).toDouble();
    const int vol_div = tpl.contains("tpl_vol_divisor")
                            ? tpl["tpl_vol_divisor"].toInt()
                            : static_cast<int>(tpl.value("vol_weight_ratio", 6000.0).toDouble());

    // 尝试 schema 14：更新老6列 + 新3列；失败 fallback 老8列
    q.prepare("UPDATE freight_templates SET"
              " template_name=?, carrier_name=?, first_weight=?, additional_unit=?, vol_weight_ratio=?,"
              " default_no_weight_fee=?, description=?, updated_at=CURRENT_TIMESTAMP,"
              " tpl_rounding_mode=?, tpl_additional_unit=?, tpl_vol_divisor=?"
              " WHERE template_id=?");
    q.addBindValue(tpl["template_name"].toString());
    q.addBindValue(tpl["carrier_name"].toString());
    q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
    q.addBindValue(add_unit);
    q.addBindValue(static_cast<double>(vol_div));
    q.addBindValue(tpl.value("default_no_weight_fee", 0.0).toDouble());
    q.addBindValue(tpl.value("description", "").toString());
    q.addBindValue(rmode);
    q.addBindValue(add_unit);
    q.addBindValue(vol_div);
    q.addBindValue(tpl["template_id"].toString());
    if (!q.exec()) {
        q.clear();
        q.prepare("UPDATE freight_templates SET template_name=?, carrier_name=?, first_weight=?, additional_unit=?, vol_weight_ratio=?, default_no_weight_fee=?, description=?, updated_at=CURRENT_TIMESTAMP WHERE template_id=?");
        q.addBindValue(tpl["template_name"].toString());
        q.addBindValue(tpl["carrier_name"].toString());
        q.addBindValue(tpl.value("first_weight", 1.0).toDouble());
        q.addBindValue(add_unit);
        q.addBindValue(static_cast<double>(vol_div));
        q.addBindValue(tpl.value("default_no_weight_fee", 0.0).toDouble());
        q.addBindValue(tpl.value("description", "").toString());
        q.addBindValue(tpl["template_id"].toString());
        return q.exec();
    }
    return true;
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
    const QString cid = customer_id.trimmed();
    // 先按主键 customer_id 精确查；查不到再按 customer_name（别名/中文名称）精确 fallback——用户Excel里客户编号列常填中文名
    q.prepare("SELECT * FROM customers WHERE customer_id = ?");
    q.addBindValue(cid);
    bool ok = false;
    if (q.exec() && q.next()) {
        ok = true;
    } else {
        q.prepare("SELECT * FROM customers WHERE TRIM(customer_name) = ?");
        q.addBindValue(cid);
        if (q.exec() && q.next()) ok = true;
    }
    if (ok) {
        m["customer_id"]        = q.value("customer_id");
        m["customer_name"]      = q.value("customer_name");
        m["discount_rate"]      = q.value("discount_rate").toDouble();
        m["default_template"]   = q.value("default_template");
        m["contact_person"]     = q.value("contact_person");
        m["contact_phone"]      = q.value("contact_phone");
        m["address"]            = q.value("address");
        // Step2 customers +5 列 支持读回
        QSqlRecord rec = q.record();
        auto hasCol = [&](const char *col) {
            return rec.indexOf(QString::fromUtf8(col)) >= 0;
        };
        if (hasCol("cust_rounding_mode")) m["cust_rounding_mode"]   = q.value("cust_rounding_mode");
        if (hasCol("cust_additional_unit")) m["cust_additional_unit"] = q.value("cust_additional_unit");
        if (hasCol("cust_vol_divisor"))   m["cust_vol_divisor"]     = q.value("cust_vol_divisor");
        if (hasCol("avg_weight_tpl_id"))  m["avg_weight_tpl_id"]    = q.value("avg_weight_tpl_id");
        if (hasCol("cust_contract_no"))   m["cust_contract_no"]     = q.value("cust_contract_no");
    }
    return m;
}

bool SqliteRuleRepository::AddCustomer(const QVariantMap &cust) {
    QSqlQuery q(db_);
    QString cust_id = cust["customer_id"].toString();

    q.prepare("INSERT INTO customers (customer_id, customer_name, discount_rate, default_template,"
              " contact_person, contact_phone, address,"
              " cust_rounding_mode, cust_additional_unit, cust_vol_divisor, avg_weight_tpl_id, cust_contract_no)"
              " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(cust_id);
    q.addBindValue(cust["customer_name"].toString());
    q.addBindValue(cust.value("discount_rate", 1.0).toDouble());
    q.addBindValue(cust["default_template"].toString());
    q.addBindValue(cust["contact_person"].toString());
    q.addBindValue(cust["contact_phone"].toString());
    q.addBindValue(cust["address"].toString());
    q.addBindValue(cust.value("cust_rounding_mode", "").toString());
    q.addBindValue(cust.value("cust_additional_unit", 0.0).toDouble());
    q.addBindValue(cust.value("cust_vol_divisor", 0).toInt());
    q.addBindValue(cust.value("avg_weight_tpl_id", "").toString());
    q.addBindValue(cust.value("cust_contract_no", "").toString());
    if (!q.exec()) {
        return false;
    }

    // 为客户创建专属报价表（复制默认模板）
    QString tpl_id = "cust_" + cust_id;
    QString tpl_name = cust["customer_name"].toString() + "专属报价";

    q.prepare("INSERT INTO freight_templates (template_id, template_name, carrier_name, first_weight, additional_unit, vol_weight_ratio, default_no_weight_fee, description) VALUES (?,?,?,?,?,?,?,?)");
    q.addBindValue(tpl_id);
    q.addBindValue(tpl_name);
    q.addBindValue("客户专属");
    q.addBindValue(1.0);
    q.addBindValue(1.0);
    q.addBindValue(6000.0);
    q.addBindValue(0.0);
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
            {"tier_0.5_1", "0.5-1KG", 0.5, 1.0, 1.0, z.t1_first, 0.5, 0, 2},
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

    // ============================================================
    // Bug fix: 创建完客户专属报价表后，把 default_template 回写到 customers
    // 否则批量结算 COALESCE(c.default_template, 'zto_standard') 会回退到中通标准，
    // 燃油/区域加价/加价策略 全部按中通算而不是客户专属算
    // ============================================================
    QSqlQuery q_upd(db_);
    q_upd.prepare("UPDATE customers SET default_template = ?, updated_at = CURRENT_TIMESTAMP WHERE customer_id = ?");
    q_upd.addBindValue(tpl_id);
    q_upd.addBindValue(cust_id);
    if (!q_upd.exec()) {
        qWarning() << "[SqliteRuleRepository::AddCustomer] 回写 default_template 失败:" << q_upd.lastError().text();
    }

    return true;
}

bool SqliteRuleRepository::UpdateCustomer(const QVariantMap &cust) {
    QSqlQuery q(db_);
    q.prepare("UPDATE customers SET customer_name=?, discount_rate=?, default_template=?, contact_person=?, contact_phone=?, address=?,"
              " cust_rounding_mode=?, cust_additional_unit=?, cust_vol_divisor=?, avg_weight_tpl_id=?, cust_contract_no=?,"
              " updated_at=CURRENT_TIMESTAMP WHERE customer_id=?");
    q.addBindValue(cust["customer_name"].toString());
    q.addBindValue(cust["discount_rate"].toDouble());
    q.addBindValue(cust["default_template"].toString());
    q.addBindValue(cust["contact_person"].toString());
    q.addBindValue(cust["contact_phone"].toString());
    q.addBindValue(cust["address"].toString());
    q.addBindValue(cust.value("cust_rounding_mode", "").toString());
    q.addBindValue(cust.value("cust_additional_unit", 0.0).toDouble());
    q.addBindValue(cust.value("cust_vol_divisor", 0).toInt());
    q.addBindValue(cust.value("avg_weight_tpl_id", "").toString());
    q.addBindValue(cust.value("cust_contract_no", "").toString());
    q.addBindValue(cust["customer_id"].toString());
    return q.exec();
}

bool SqliteRuleRepository::DeleteCustomer(const QString &customer_id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM customers WHERE customer_id = ?");
    q.addBindValue(customer_id);
    return q.exec();
}

// ====== 拉均重合同 (avg_weight_templates) ======
QVariantList SqliteRuleRepository::ListAvgWeightTemplates() {
    QVariantList result;
    QSqlQuery q(db_);
    q.exec("SELECT avg_tpl_id,template_id,name,version,effective_from,effective_to,"
           "contract_no,base_avg_kg,avg_pool_max_kg,avg_fee_cap_kg,base_fee,step_kg,step_fee,"
           "min_tickets,over_cap_mode,reuse_zone_groups,period_type,is_active,"
           "created_at,updated_at FROM avg_weight_templates ORDER BY is_active DESC, avg_tpl_id");
    auto hasCol = [&](const char *col) { return q.record().indexOf(QString::fromUtf8(col)) >= 0; };
    while (q.next()) {
        QVariantMap m;
        m["avg_tpl_id"]          = q.value("avg_tpl_id").toString();
        m["template_id"]         = q.value("template_id").toString();
        m["name"]                = q.value("name").toString();
        m["version"]             = q.value("version").toInt();
        m["effective_from"]      = q.value("effective_from").toString();
        m["effective_to"]        = q.value("effective_to").toString();
        m["contract_no"]         = q.value("contract_no").toString();
        m["base_avg_kg"]         = q.value("base_avg_kg").toDouble();
        m["avg_pool_max_kg"]     = q.value("avg_pool_max_kg").toDouble();
        m["avg_fee_cap_kg"]      = q.value("avg_fee_cap_kg").toDouble();
        m["base_fee"]            = q.value("base_fee").toDouble();
        m["step_kg"]             = q.value("step_kg").toDouble();
        m["step_fee"]            = q.value("step_fee").toDouble();
        m["min_tickets"]         = q.value("min_tickets").toInt();
        m["over_cap_mode"]       = q.value("over_cap_mode").toInt();
        m["reuse_zone_groups"]   = q.value("reuse_zone_groups").toInt();
        if (hasCol("period_type")) m["period_type"] = q.value("period_type").toString();
        else                        m["period_type"] = QStringLiteral("month");
        m["is_active"]           = q.value("is_active").toInt() == 1;
        result << m;
    }
    return result;
}

QVariantMap SqliteRuleRepository::GetAvgWeightTemplate(const QString &avg_tpl_id) {
    QVariantList all = ListAvgWeightTemplates();
    for (const auto &a : all) {
        const auto m = a.toMap();
        if (m["avg_tpl_id"].toString() == avg_tpl_id) return m;
    }
    return {};
}

bool SqliteRuleRepository::SaveAvgWeightTemplate(const QVariantMap &tpl) {
    QSqlQuery q(db_);
    QString avg_tpl_id = tpl["avg_tpl_id"].toString().trimmed();
    if (avg_tpl_id.isEmpty()) return false;

    // 版本自增：如已存在同 ID 则 version = MAX(version)+1
    int next_version = tpl.value("version", 1).toInt();
    {
        QSqlQuery qv(db_);
        qv.prepare("SELECT MAX(version) FROM avg_weight_templates WHERE avg_tpl_id=?");
        qv.addBindValue(avg_tpl_id);
        if (qv.exec() && qv.next()) next_version = qv.value(0).toInt() + 1;
    }
    // 如果显式传 version 且比自增大，用显式
    if (tpl.contains("version") && tpl["version"].toInt() > next_version)
        next_version = tpl["version"].toInt();

    q.prepare("INSERT OR REPLACE INTO avg_weight_templates("
              "avg_tpl_id,template_id,name,version,effective_from,effective_to,"
              "contract_no,base_avg_kg,avg_pool_max_kg,avg_fee_cap_kg,base_fee,step_kg,step_fee,"
              "min_tickets,over_cap_mode,reuse_zone_groups,is_active,updated_at)"
              " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,CURRENT_TIMESTAMP)");
    q.addBindValue(avg_tpl_id);
    q.addBindValue(tpl.value("template_id", "").toString());
    q.addBindValue(tpl.value("name", "").toString());
    q.addBindValue(next_version);
    q.addBindValue(tpl.value("effective_from", QDate::currentDate().toString(Qt::ISODate)).toString());
    q.addBindValue(tpl.value("effective_to", "").toString());
    q.addBindValue(tpl.value("contract_no", "").toString());
    q.addBindValue(tpl.value("base_avg_kg", 0.3).toDouble());
    q.addBindValue(tpl.value("avg_pool_max_kg", 1.0).toDouble());
    q.addBindValue(tpl.value("avg_fee_cap_kg", 1.0).toDouble());
    q.addBindValue(tpl.value("base_fee", 2.7).toDouble());
    q.addBindValue(tpl.value("step_kg", 0.1).toDouble());
    q.addBindValue(tpl.value("step_fee", 0.2).toDouble());
    q.addBindValue(tpl.value("min_tickets", 50).toInt());
    q.addBindValue(tpl.value("over_cap_mode", 0).toInt());
    q.addBindValue(tpl.value("reuse_zone_groups", 1).toInt());
    q.addBindValue(tpl.value("is_active", true).toBool() ? 1 : 0);
    bool exec_ok = q.exec();
    if (!exec_ok) {
        qCritical() << "[SaveAvgWeightTemplate] SQL 失败：" << q.lastError().text()
                    << "\n    绑定的列：avg_tpl_id=" << avg_tpl_id
                    << "，template_id=" << tpl.value("template_id", "").toString()
                    << "，name=" << tpl.value("name", "").toString();
    }
    return exec_ok;
}

bool SqliteRuleRepository::DeleteAvgWeightTemplate(const QString &avg_tpl_id) {
    QSqlQuery q(db_);
    q.prepare("DELETE FROM avg_weight_templates WHERE avg_tpl_id=?");
    q.addBindValue(avg_tpl_id);
    return q.exec();
}

bool SqliteRuleRepository::SetAvgWeightTemplateActive(const QString &avg_tpl_id, bool active) {
    QSqlQuery q(db_);
    q.prepare("UPDATE avg_weight_templates SET is_active=?, updated_at=CURRENT_TIMESTAMP WHERE avg_tpl_id=?");
    q.addBindValue(active ? 1 : 0);
    q.addBindValue(avg_tpl_id);
    return q.exec();
}

// ====== 拉均重方案A：勾选模板分区 + 排除省 ======
QVariantList SqliteRuleRepository::GetAvgWeightTplGroups(const QString &avg_tpl_id) {
    QVariantList out;
    if (!db_.isOpen()) {
        qCritical() << "[GetAvgWeightTplGroups-FATAL] db_ is NOT OPEN! avg_tpl_id=" << avg_tpl_id;
        return out;
    }
    {
        QSqlQuery cq(db_);
        cq.prepare("SELECT COUNT(*) FROM avg_weight_zone_tpl_groups WHERE avg_tpl_id=?");
        cq.addBindValue(avg_tpl_id);
        if (cq.exec() && cq.next()) {
            qInfo() << "[GetAvgWeightTplGroups-DIAG] BEFORE_SELECT_COUNT] avg_tpl_id=" << avg_tpl_id
                     << " DB_count=" << cq.value(0).toInt()
                     << " conn_open=" << db_.isOpen()
                     << " lastError=" << db_.lastError().text();
        } else {
            qCritical() << "[GetAvgWeightTplGroups-DIAG] COUNT query failed:" << cq.lastError().text();
        }
    }
    QSqlQuery q(db_);
    q.prepare("SELECT template_id, group_code FROM avg_weight_zone_tpl_groups WHERE avg_tpl_id=? ORDER BY template_id, group_code");
    q.addBindValue(avg_tpl_id);
    qDebug() << "[GetAvgWeightTplGroups-DIAG] bound avg_tpl_id=" << avg_tpl_id
             << " last query:" << q.lastQuery();
    bool ok = q.exec();
    if (!ok) {
        qCritical() << "[GetAvgWeightTplGroups] SELECT failed:" << q.lastError().text() << " boundValue(0)=" << q.boundValue(0).toString();
        return out;
    }
    int row = 0;
    while (q.next()) {
        QVariantMap m;
        m["template_id"] = q.value(0).toString();
        m["group_code"]  = q.value(1).toString();
        out << m;
        row++;
    }
    qInfo() << "[GetAvgWeightTplGroups-DIAG] AFTER] iterated rows=" << row << " out.size=" << out.size();
    return out;
}

namespace {
// ===================================================================
// 兜底三层保险·第三层：写入时自动重建缺失的拉均重合同相关表
//   三张表各一个独立 helper，返回值为「是否尝试过建表」。
//   规则：
//     1. 检测到 PREPARE 失败且 lastError 含 no such table 时，调用对应 helper
//     2. helper 会执行 CREATE TABLE IF NOT EXISTS + 索引
//     3. SetXxx 函数内 helper 返回后 retry 整个 Set 操作一次（仅一次）
// ===================================================================
static inline void ensure_lajz_tpl_groups_table(QSqlDatabase &db) {
    QSqlQuery cq(db);
    cq.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS avg_weight_zone_tpl_groups (
            avg_tpl_id  VARCHAR(60) NOT NULL,
            template_id VARCHAR(100) NOT NULL,
            group_code  VARCHAR(20) NOT NULL,
            PRIMARY KEY (avg_tpl_id, template_id, group_code)
        )
    )SQL");
    if (cq.lastError().isValid())
        qCritical() << "[ensure-lajz-schema] avg_weight_zone_tpl_groups CREATE failed:" << cq.lastError().text();
    else
        qInfo() << "[ensure-lajz-schema]  ✓ avg_weight_zone_tpl_groups 已自动重建";
}

static inline void ensure_lajz_excludes_table(QSqlDatabase &db) {
    QSqlQuery cq(db);
    cq.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS avg_weight_zone_excludes (
            avg_tpl_id  VARCHAR(60) NOT NULL,
            template_id VARCHAR(100) NOT NULL,
            group_code  VARCHAR(20) NOT NULL,
            province    VARCHAR(50) NOT NULL,
            PRIMARY KEY (avg_tpl_id, template_id, group_code, province)
        )
    )SQL");
    if (cq.lastError().isValid())
        qCritical() << "[ensure-lajz-schema] avg_weight_zone_excludes CREATE failed:" << cq.lastError().text();
    else
        qInfo() << "[ensure-lajz-schema]  ✓ avg_weight_zone_excludes 已自动重建";
    QSqlQuery iq(db);
    iq.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zone_excludes_tpl ON avg_weight_zone_excludes(avg_tpl_id)");
}

static inline void ensure_lajz_zones_table(QSqlDatabase &db) {
    QSqlQuery cq(db);
    cq.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS avg_weight_zones (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            avg_tpl_id VARCHAR(60) NOT NULL,
            zone_code  VARCHAR(20) NOT NULL,
            province   VARCHAR(50) NOT NULL,
            UNIQUE(avg_tpl_id, zone_code, province)
        )
    )SQL");
    if (cq.lastError().isValid())
        qCritical() << "[ensure-lajz-schema] avg_weight_zones CREATE failed:" << cq.lastError().text();
    else
        qInfo() << "[ensure-lajz-schema]  ✓ avg_weight_zones 已自动重建";
    QSqlQuery iq(db);
    iq.exec("CREATE INDEX IF NOT EXISTS idx_avg_weight_zones_tpl ON avg_weight_zones(avg_tpl_id)");
}

static inline bool err_is_no_such_table(const QString &err) {
    return err.toLower().contains("no such table");
}
} // anon namespace

bool SqliteRuleRepository::SetAvgWeightTplGroups(const QString &avg_tpl_id, const QVariantList &groups, QString *out_err) {
    int attempt = 0;
    QString inner_err;
    QString *perr = out_err ? out_err : &inner_err;
try_again:
    attempt++;
    QSqlQuery dq(db_), iq(db_), vq(db_);
    static const QString kDelSql = QStringLiteral("DELETE FROM avg_weight_zone_tpl_groups WHERE avg_tpl_id=?");
    if (!dq.prepare(kDelSql)) {
        QString msg = QStringLiteral("[PREPARE DELETE]%1 (sql=%2)").arg(dq.lastError().text(), kDelSql);
        qCritical() << "[SetAvgWeightTplGroups]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightTplGroups-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_tpl_groups_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    dq.addBindValue(avg_tpl_id);
    if (!dq.exec()) {
        QString msg = QStringLiteral("[DELETE]%1").arg(dq.lastError().text());
        qCritical() << "[SetAvgWeightTplGroups]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightTplGroups-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_tpl_groups_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int del_rows = dq.numRowsAffected();
    static const QString kInsSql = QStringLiteral("INSERT INTO avg_weight_zone_tpl_groups (avg_tpl_id, template_id, group_code) VALUES (?,?,?)");
    if (!iq.prepare(kInsSql)) {
        QString msg = QStringLiteral("[PREPARE INSERT]%1 (sql=%2)").arg(iq.lastError().text(), kInsSql);
        qCritical() << "[SetAvgWeightTplGroups]" << msg;
        if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
            qInfo() << "[SetAvgWeightTplGroups-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_tpl_groups_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int ins_attempted = 0, ins_skipped = 0, ins_succeeded = 0;
    for (const auto &g : groups) {
        const auto m = g.toMap();
        QString tid = m["template_id"].toString().trimmed();
        QString gcode = m["group_code"].toString().trimmed();
        if (tid.isEmpty() || gcode.isEmpty()) { ins_skipped++; continue; }
        ins_attempted++;
        iq.addBindValue(avg_tpl_id); iq.addBindValue(tid); iq.addBindValue(gcode);
        if (!iq.exec()) {
            QString msg = QStringLiteral("[INSERT avg_tpl_id=%1,template_id=%2,group_code=%3]%4")
                            .arg(avg_tpl_id).arg(tid).arg(gcode).arg(iq.lastError().text());
            qCritical() << "[SetAvgWeightTplGroups]" << msg;
            if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
                qInfo() << "[SetAvgWeightTplGroups-RETRY] 检测到表缺失，自动建表后重试一次";
                ensure_lajz_tpl_groups_table(db_);
                goto try_again;
            }
            *perr = msg;
            return false;
        }
        if (iq.numRowsAffected() > 0) ins_succeeded++;
        else {
            qCritical() << "[SetAvgWeightTplGroups] INSERT 未影响任何行（可能主键冲突？）"
                        << " avg_tpl_id=" << avg_tpl_id << " template_id=" << tid << " group_code=" << gcode
                        << " lastError=" << iq.lastError().text();
        }
    }
    static const QString kCountSql = QStringLiteral("SELECT COUNT(*) FROM avg_weight_zone_tpl_groups WHERE avg_tpl_id=?");
    if (!vq.prepare(kCountSql)) {
        QString msg = QStringLiteral("[PREPARE COUNT]%1 (sql=%2)").arg(vq.lastError().text(), kCountSql);
        qWarning() << "[SetAvgWeightTplGroups]" << msg;
        if (attempt == 1 && err_is_no_such_table(vq.lastError().text())) {
            qInfo() << "[SetAvgWeightTplGroups-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_tpl_groups_table(db_);
            goto try_again;
        }
        if (perr->isEmpty()) *perr = msg;
    } else {
        vq.addBindValue(avg_tpl_id);
        int actual_count = -1;
        if (vq.exec() && vq.next()) actual_count = vq.value(0).toInt();
        qInfo().noquote() << QString("[SetAvgWeightTplGroups-DIAG] attempt=%2 | avg_tpl_id=%1 input_groups=%3 del_rows=%4 "
                                      "ins_attempted=%5 ins_skipped=%6 ins_succeeded=%7 actual_DB_count=%8")
                             .arg(avg_tpl_id).arg(attempt).arg(groups.size()).arg(del_rows)
                             .arg(ins_attempted).arg(ins_skipped).arg(ins_succeeded).arg(actual_count);
        if (actual_count != ins_succeeded) {
            QString msg = QStringLiteral("[VERIFY COUNT] 预期INSERT成功%1行，但DB实得%2行（%3成功%4跳过%5尝试，DELETE删%6行）")
                            .arg(ins_succeeded).arg(actual_count).arg(ins_succeeded).arg(ins_skipped)
                            .arg(ins_attempted).arg(del_rows);
            qWarning() << "[SetAvgWeightTplGroups]" << msg;
            if (perr->isEmpty()) *perr = msg;
        }
    }
    if (attempt > 1) {
        qInfo() << "[SetAvgWeightTplGroups]  自动修复成功（原因为缺失 avg_weight_zone_tpl_groups 表） attempt=" << attempt;
    }
    return true;
}

QVariantList SqliteRuleRepository::GetAvgWeightExcludes(const QString &avg_tpl_id) {
    QVariantList out;
    if (!db_.isOpen()) {
        qCritical() << "[GetAvgWeightExcludes-FATAL] db_ is NOT OPEN! avg_tpl_id=" << avg_tpl_id;
        return out;
    }
    {
        QSqlQuery cq(db_);
        cq.prepare("SELECT COUNT(*) FROM avg_weight_zone_excludes WHERE avg_tpl_id=?");
        cq.addBindValue(avg_tpl_id);
        if (cq.exec() && cq.next()) {
            qInfo() << "[GetAvgWeightExcludes-DIAG] BEFORE_COUNT] avg_tpl_id=" << avg_tpl_id
                     << " DB_count=" << cq.value(0).toInt();
        }
    }
    QSqlQuery q(db_);
    q.prepare("SELECT template_id, group_code, province FROM avg_weight_zone_excludes WHERE avg_tpl_id=? ORDER BY template_id, group_code, province");
    q.addBindValue(avg_tpl_id);
    bool ok = q.exec();
    if (!ok) {
        qCritical() << "[GetAvgWeightExcludes] SELECT failed:" << q.lastError().text();
        return out;
    }
    int row = 0;
    while (q.next()) {
        QVariantMap m;
        m["template_id"] = q.value(0).toString();
        m["group_code"]  = q.value(1).toString();
        m["province"]    = q.value(2).toString();
        out << m;
        row++;
    }
    qInfo() << "[GetAvgWeightExcludes-DIAG] AFTER] rows=" << row << " out.size=" << out.size();
    return out;
}

bool SqliteRuleRepository::SetAvgWeightExcludes(const QString &avg_tpl_id, const QVariantList &excludes, QString *out_err) {
    int attempt = 0;
    QString inner_err;
    QString *perr = out_err ? out_err : &inner_err;
try_again:
    attempt++;
    QSqlQuery dq(db_), iq(db_), vq(db_);
    static const QString kDelSql = QStringLiteral("DELETE FROM avg_weight_zone_excludes WHERE avg_tpl_id=?");
    if (!dq.prepare(kDelSql)) {
        QString msg = QStringLiteral("[PREPARE DELETE]%1 (sql=%2)").arg(dq.lastError().text(), kDelSql);
        qCritical() << "[SetAvgWeightExcludes]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightExcludes-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_excludes_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    dq.addBindValue(avg_tpl_id);
    if (!dq.exec()) {
        QString msg = QStringLiteral("[DELETE]%1").arg(dq.lastError().text());
        qCritical() << "[SetAvgWeightExcludes]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightExcludes-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_excludes_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int del_rows = dq.numRowsAffected();
    static const QString kInsSql = QStringLiteral("INSERT INTO avg_weight_zone_excludes (avg_tpl_id, template_id, group_code, province) VALUES (?,?,?,?)");
    if (!iq.prepare(kInsSql)) {
        QString msg = QStringLiteral("[PREPARE INSERT]%1 (sql=%2)").arg(iq.lastError().text(), kInsSql);
        qCritical() << "[SetAvgWeightExcludes]" << msg;
        if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
            qInfo() << "[SetAvgWeightExcludes-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_excludes_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int ins_attempted = 0, ins_skipped = 0, ins_succeeded = 0;
    for (const auto &e : excludes) {
        const auto m = e.toMap();
        QString tid = m["template_id"].toString().trimmed();
        QString gcode = m["group_code"].toString().trimmed();
        QString prov  = m["province"].toString().trimmed();
        if (tid.isEmpty() || gcode.isEmpty() || prov.isEmpty()) { ins_skipped++; continue; }
        ins_attempted++;
        iq.addBindValue(avg_tpl_id); iq.addBindValue(tid); iq.addBindValue(gcode); iq.addBindValue(prov);
        if (!iq.exec()) {
            QString msg = QStringLiteral("[INSERT avg_tpl_id=%1,template_id=%2,group_code=%3,province=%4]%5")
                            .arg(avg_tpl_id).arg(tid).arg(gcode).arg(prov).arg(iq.lastError().text());
            qCritical() << "[SetAvgWeightExcludes]" << msg;
            if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
                qInfo() << "[SetAvgWeightExcludes-RETRY] 检测到表缺失，自动建表后重试一次";
                ensure_lajz_excludes_table(db_);
                goto try_again;
            }
            *perr = msg;
            return false;
        }
        if (iq.numRowsAffected() > 0) ins_succeeded++;
        else {
            qCritical() << "[SetAvgWeightExcludes] INSERT 未影响任何行（主键冲突？）"
                        << " avg_tpl_id=" << avg_tpl_id << " tpl=" << tid << " grp=" << gcode << " prov=" << prov
                        << " lastError=" << iq.lastError().text();
        }
    }
    static const QString kCountSql = QStringLiteral("SELECT COUNT(*) FROM avg_weight_zone_excludes WHERE avg_tpl_id=?");
    if (!vq.prepare(kCountSql)) {
        QString msg = QStringLiteral("[PREPARE COUNT]%1 (sql=%2)").arg(vq.lastError().text(), kCountSql);
        qWarning() << "[SetAvgWeightExcludes]" << msg;
        if (attempt == 1 && err_is_no_such_table(vq.lastError().text())) {
            qInfo() << "[SetAvgWeightExcludes-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_excludes_table(db_);
            goto try_again;
        }
        if (perr->isEmpty()) *perr = msg;
    } else {
        vq.addBindValue(avg_tpl_id);
        int actual_count = -1;
        if (vq.exec() && vq.next()) actual_count = vq.value(0).toInt();
        qInfo().noquote() << QString("[SetAvgWeightExcludes-DIAG] attempt=%2 | avg_tpl_id=%1 input=%3 del=%4 "
                                      "ins_att=%5 ins_skip=%6 ins_succ=%7 actual=%8")
                             .arg(avg_tpl_id).arg(attempt).arg(excludes.size()).arg(del_rows)
                             .arg(ins_attempted).arg(ins_skipped).arg(ins_succeeded).arg(actual_count);
        if (actual_count != ins_succeeded) {
            QString msg = QStringLiteral("[VERIFY COUNT] 预期INSERT成功%1行，但DB实得%2行（%3成功%4跳过%5尝试，DELETE删%6行）")
                            .arg(ins_succeeded).arg(actual_count).arg(ins_succeeded).arg(ins_skipped)
                            .arg(ins_attempted).arg(del_rows);
            qWarning() << "[SetAvgWeightExcludes]" << msg;
            if (perr->isEmpty()) *perr = msg;
        }
    }
    if (attempt > 1) {
        qInfo() << "[SetAvgWeightExcludes]  自动修复成功（原因为缺失 avg_weight_zone_excludes 表） attempt=" << attempt;
    }
    return true;
}

// ====== 拉均重方案B：自定义省份（avg_weight_zones） ======
QVariantList SqliteRuleRepository::GetAvgWeightZones(const QString &avg_tpl_id) {
    QVariantList out;
    if (!db_.isOpen()) {
        qCritical() << "[GetAvgWeightZones-FATAL] db_ is NOT OPEN! avg_tpl_id=" << avg_tpl_id;
        return out;
    }
    {
        QSqlQuery cq(db_);
        cq.prepare("SELECT COUNT(*) FROM avg_weight_zones WHERE avg_tpl_id=?");
        cq.addBindValue(avg_tpl_id);
        if (cq.exec() && cq.next()) {
            qInfo() << "[GetAvgWeightZones-DIAG] BEFORE_COUNT] avg_tpl_id=" << avg_tpl_id
                     << " DB_row_count=" << cq.value(0).toInt();
        }
    }
    // 1) 取所有 zone_code（按 id 从小到大保序）
    QMap<QString, QStringList> zone_provs;
    QMap<QString, int> zone_order;
    QMap<QString, QString> zone_name;
    int total_raw_rows = 0;
    {
        QSqlQuery q(db_);
        q.prepare("SELECT zone_code, province, id FROM avg_weight_zones WHERE avg_tpl_id=? ORDER BY id ASC");
        q.addBindValue(avg_tpl_id);
        bool ok = q.exec();
        if (!ok) {
            qCritical() << "[GetAvgWeightZones] SELECT failed:" << q.lastError().text();
            return out;
        }
        int idx = 0;
        while (q.next()) {
            QString zc = q.value(0).toString();
            QString prov = q.value(1).toString();
            int id = q.value(2).toInt();
            zone_provs[zc] << prov;
            if (!zone_order.contains(zc)) zone_order[zc] = id;
            idx++;
            total_raw_rows++;
        }
    }
    // 2) 从 zone_groups 里捞可能对应的 zone_name（如果 avg_weight_zones.zone_code == zone_groups.group_code 且 template_id 匹配/通配）
    //    没匹配到就用 zone_code 本身作为 name
    for (auto it = zone_order.begin(); it != zone_order.end(); ++it) {
        const QString &zc = it.key();
        QVariantMap m;
        m["zone_code"] = zc;
        m["provinces"] = zone_provs[zc];
        out << m;
    }
    qInfo() << "[GetAvgWeightZones-DIAG] AFTER] total_raw_rows=" << total_raw_rows
            << " unique_zones=" << out.size()
            << " zone_codes=" << zone_order.keys();
    return out;
}

bool SqliteRuleRepository::SetAvgWeightZones(const QString &avg_tpl_id, const QVariantList &zones, QString *out_err) {
    int attempt = 0;
    QString inner_err;
    QString *perr = out_err ? out_err : &inner_err;
try_again:
    attempt++;
    QSqlQuery dq(db_), iq(db_), vq(db_);
    static const QString kDelSql = QStringLiteral("DELETE FROM avg_weight_zones WHERE avg_tpl_id=?");
    if (!dq.prepare(kDelSql)) {
        QString msg = QStringLiteral("[PREPARE DELETE]%1 (sql=%2)").arg(dq.lastError().text(), kDelSql);
        qCritical() << "[SetAvgWeightZones]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightZones-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_zones_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    dq.addBindValue(avg_tpl_id);
    if (!dq.exec()) {
        QString msg = QStringLiteral("[DELETE]%1").arg(dq.lastError().text());
        qCritical() << "[SetAvgWeightZones]" << msg << "（avg_tpl_id=" << avg_tpl_id << "）";
        if (attempt == 1 && err_is_no_such_table(dq.lastError().text())) {
            qInfo() << "[SetAvgWeightZones-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_zones_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int del_rows = dq.numRowsAffected();
    static const QString kInsSql = QStringLiteral("INSERT INTO avg_weight_zones (avg_tpl_id, zone_code, province) VALUES (?,?,?)");
    if (!iq.prepare(kInsSql)) {
        QString msg = QStringLiteral("[PREPARE INSERT]%1 (sql=%2)").arg(iq.lastError().text(), kInsSql);
        qCritical() << "[SetAvgWeightZones]" << msg;
        if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
            qInfo() << "[SetAvgWeightZones-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_zones_table(db_);
            goto try_again;
        }
        *perr = msg;
        return false;
    }
    int zone_attempted = 0, zone_skipped_empty = 0, prov_attempted = 0, prov_succeeded = 0;
    for (const auto &z : zones) {
        const auto m = z.toMap();
        QString zc = m["zone_code"].toString().trimmed();
        if (zc.isEmpty()) { zone_skipped_empty++; continue; }
        zone_attempted++;
        const QStringList provs = m["provinces"].toStringList();
        qInfo() << "   [SetAvgWeightZones-TRACE] zone=" << zc << " provinces_list_size=" << provs.size() << " raw_provinces=" << provs;
        for (const auto &p : provs) {
            QString pp = p.trimmed();
            if (pp.isEmpty()) continue;
            prov_attempted++;
            iq.addBindValue(avg_tpl_id); iq.addBindValue(zc); iq.addBindValue(pp);
            if (!iq.exec()) {
                QString msg = QStringLiteral("[INSERT avg_tpl_id=%1,zone_code=%2,province=%3]%4")
                                .arg(avg_tpl_id).arg(zc).arg(pp).arg(iq.lastError().text());
                qCritical() << "[SetAvgWeightZones]" << msg;
                if (attempt == 1 && err_is_no_such_table(iq.lastError().text())) {
                    qInfo() << "[SetAvgWeightZones-RETRY] 检测到表缺失，自动建表后重试一次";
                    ensure_lajz_zones_table(db_);
                    goto try_again;
                }
                *perr = msg;
                return false;
            }
            if (iq.numRowsAffected() > 0) prov_succeeded++;
            else {
                qCritical() << "[SetAvgWeightZones] INSERT 未影响任何行（UNIQUE冲突？）"
                            << " avg_tpl_id=" << avg_tpl_id << " zone=" << zc << " prov=" << pp
                            << " lastError=" << iq.lastError().text();
            }
        }
    }
    static const QString kCountSql = QStringLiteral("SELECT COUNT(*) FROM avg_weight_zones WHERE avg_tpl_id=?");
    if (!vq.prepare(kCountSql)) {
        QString msg = QStringLiteral("[PREPARE COUNT]%1 (sql=%2)").arg(vq.lastError().text(), kCountSql);
        qWarning() << "[SetAvgWeightZones]" << msg;
        if (attempt == 1 && err_is_no_such_table(vq.lastError().text())) {
            qInfo() << "[SetAvgWeightZones-RETRY] 检测到表缺失，自动建表后重试一次";
            ensure_lajz_zones_table(db_);
            goto try_again;
        }
        if (perr->isEmpty()) *perr = msg;
    } else {
        vq.addBindValue(avg_tpl_id);
        int actual_count = -1;
        if (vq.exec() && vq.next()) actual_count = vq.value(0).toInt();
        qInfo().noquote() << QString("[SetAvgWeightZones-DIAG] attempt=%2 | avg_tpl_id=%1 input_zones=%3 del=%4 "
                                      "zone_att=%5 zone_skip_empty=%6 prov_att=%7 prov_succ=%8 actual_DB_rows=%9")
                             .arg(avg_tpl_id).arg(attempt).arg(zones.size()).arg(del_rows)
                             .arg(zone_attempted).arg(zone_skipped_empty).arg(prov_attempted)
                             .arg(prov_succeeded).arg(actual_count);
        if (actual_count != prov_succeeded) {
            QString msg = QStringLiteral("[VERIFY COUNT] 预期INSERT成功%1行，但DB实得%2行（分区%3尝试%4跳过，省份%5尝试%6成功，DELETE删%7行）")
                            .arg(prov_succeeded).arg(actual_count).arg(zone_attempted).arg(zone_skipped_empty)
                            .arg(prov_attempted).arg(prov_succeeded).arg(del_rows);
            qWarning() << "[SetAvgWeightZones]" << msg;
            if (perr->isEmpty()) *perr = msg;
        }
    }
    if (attempt > 1) {
        qInfo() << "[SetAvgWeightZones]  自动修复成功（原因为缺失 avg_weight_zones 表） attempt=" << attempt;
    }
    return true;
}

// ====== 辅助：ListCourierTemplatesWithZones ======
QVariantList SqliteRuleRepository::ListCourierTemplatesWithZones() {
    QVariantList templates = ListTemplates();
    QVariantList out;

    // 预加载所有 zone_groups + zone_group_provinces
    QMap<QString, QMap<QString, QVariantMap>> tpl_groups;   // tpl_id -> group_code -> group_info
    QMap<QString, QMap<QString, QStringList>> tpl_group_provs; // tpl_id -> group_code -> [prov]
    {
        QSqlQuery q(db_);
        q.exec("SELECT template_id, group_code, group_name, sort_order FROM zone_groups ORDER BY template_id, sort_order");
        while (q.next()) {
            QString tid = q.value(0).toString();
            QString gc  = q.value(1).toString();
            QString gn  = q.value(2).toString();
            int so      = q.value(3).toInt();
            QVariantMap g;
            g["group_code"] = gc; g["group_name"] = gn; g["sort_order"] = so;
            tpl_groups[tid][gc] = g;
        }
    }
    {
        QSqlQuery q(db_);
        q.exec("SELECT template_id, group_code, province FROM zone_group_provinces ORDER BY template_id, group_code");
        while (q.next()) {
            QString tid = q.value(0).toString();
            QString gc  = q.value(1).toString();
            QString p   = q.value(2).toString();
            tpl_group_provs[tid][gc] << p;
        }
    }

    for (const auto &t : templates) {
        QVariantMap tm = t.toMap();
        QString tid = tm["template_id"].toString();
        QVariantList groups;
        const auto gmap = tpl_groups.value(tid);
        for (auto it = gmap.begin(); it != gmap.end(); ++it) {
            QVariantMap g = it.value();
            const QStringList ps = tpl_group_provs.value(tid).value(it.key());
            g["province_count"] = ps.size();
            g["provinces"] = ps;
            groups << g;
        }
        tm["groups"] = groups;
        out << tm;
    }
    return out;
}

QVariantList SqliteRuleRepository::ListFuelSurcharges(const QString &template_id) {
    QVariantList result;
    QSqlQuery q(db_);
    QString sql = "SELECT id, template_id, effective_date, rate, is_active FROM fuel_surcharge WHERE 1=1";
    QVariantList binds;
    if (!template_id.isEmpty()) {
        sql += " AND template_id = ?";
        binds << template_id;
    }
    sql += " ORDER BY template_id, effective_date DESC";
    q.prepare(sql);
    for (const auto &b : binds) q.addBindValue(b);
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
    QString sql = "SELECT id, template_id, province, city, district, surcharge, is_active FROM remote_areas WHERE 1=1";
    QVariantList binds;
    if (!template_id.isEmpty()) {
        sql += " AND template_id = ?";
        binds << template_id;
    }
    sql += " ORDER BY template_id, province, city, district";
    q.prepare(sql);
    for (const auto &b : binds) q.addBindValue(b);
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

bool SqliteRuleRepository::CreateDefaultCustomers() {
    QSqlQuery q(db_);

    struct DefaultCustomer {
        QString id;
        QString name;
        double discount;
        QString def_tpl;
        QString contact;
        QString phone;
    };
    QList<DefaultCustomer> custs = {
        {"C001", "华东电商客户A", 1.00, "zto_standard", "张经理", "13800000001"},
        {"C002", "华南批发客户B", 0.95, "zto_standard", "李主管", "13800000002"},
        {"C003", "华北零售客户C", 1.00, "zto_standard", "王总",   "13800000003"},
    };

    for (const auto &c : custs) {
        q.prepare("INSERT OR IGNORE INTO customers "
                  "(customer_id, customer_name, discount_rate, default_template, contact_person, contact_phone, address) "
                  "VALUES (?,?,?,?,?,?,?)");
        q.addBindValue(c.id);
        q.addBindValue(c.name);
        q.addBindValue(c.discount);
        q.addBindValue(c.def_tpl);
        q.addBindValue(c.contact);
        q.addBindValue(c.phone);
        q.addBindValue("");
        if (!q.exec()) {
            qCritical() << "CreateDefaultCustomers failed for" << c.id << ":" << q.lastError().text();
            return false;
        }
    }

    qDebug() << "Default customers initialized:" << custs.size() << "records";
    return true;
}

bool SqliteRuleRepository::ValidateIntegrity() {
    QSqlQuery q(db_);

    q.exec("SELECT COUNT(*) FROM freight_templates");
    int tpl_count = q.next() ? q.value(0).toInt() : 0;
    if (tpl_count == 0) {
        qWarning() << "Integrity: freight_templates is EMPTY";
        return false;
    }

    q.exec("SELECT COUNT(*) FROM customers");
    int cust_count = q.next() ? q.value(0).toInt() : 0;
    if (cust_count == 0) {
        qWarning() << "Integrity: customers is EMPTY";
        return false;
    }

    q.exec("SELECT COUNT(*) FROM tiered_pricing");
    int tier_count = q.next() ? q.value(0).toInt() : 0;
    if (tier_count == 0) {
        qWarning() << "Integrity: tiered_pricing is EMPTY";
        return false;
    }

    q.exec("SELECT COUNT(*) FROM fuel_surcharge WHERE is_active = 1");
    int fs_count = q.next() ? q.value(0).toInt() : 0;
    if (fs_count == 0) {
        qWarning() << "Integrity: fuel_surcharge has NO active records";
        return false;
    }

    q.exec("SELECT COUNT(*) FROM zone_group_provinces");
    int zp_count = q.next() ? q.value(0).toInt() : 0;
    if (zp_count == 0) {
        qWarning() << "Integrity: zone_group_provinces is EMPTY";
        return false;
    }

    qDebug() << "Integrity check passed:"
             << "templates=" << tpl_count
             << "customers=" << cust_count
             << "tiers=" << tier_count
             << "fuel_active=" << fs_count
             << "zone_provinces=" << zp_count;
    return true;
}

} // namespace freight::db
