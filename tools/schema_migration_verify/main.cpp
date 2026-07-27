#include "core/app_config.hpp"
#include "core/freight_types.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "db/duckdb_manager.hpp"
#include "services/rule_service.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace freight;

#define ASSERT_TRUE(x, label) \
    do { \
        if (!(x)) { \
            qCritical().noquote() << QStringLiteral("  ❌ FAIL %1").arg(label); \
            return 1; \
        } else { \
            qInfo().noquote() << QStringLiteral("  ✅ PASS %1").arg(label); \
        } \
    } while(0)

#define ASSERT_EQ_INT(a, b, label) \
    do { \
        qlonglong _a = (a); qlonglong _b = (b); \
        if (_a != _b) { \
            qCritical().noquote() << QStringLiteral("  ❌ FAIL %1: expected %2 got %3").arg(label).arg(_b).arg(_a); \
            return 1; \
        } else { \
            qInfo().noquote() << QStringLiteral("  ✅ PASS %1: %2").arg(label).arg(_a); \
        } \
    } while(0)

// 每个测试组用独立的 HOME 目录（隔离 AppConfig 路径），避免 AppConfig::Instance() 单例路径互串。
struct SandboxEnv {
    QString rulesDbPath;
    QString duckDbPath;
    QString rollbackSqlPath;
    QString tmpHome;
    SandboxEnv(const QString &tag) {
        tmpHome = QDir::tempPath() + "/xiaoqiao_s2_" + tag + "_" + QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(tmpHome);
        qputenv("HOME", tmpHome.toUtf8());
        qputenv("XDG_DATA_HOME", tmpHome.toUtf8());
        // 重新 Init AppConfig 让它读取新 HOME
        freight::core::AppConfig::Instance().Init();
        rulesDbPath = freight::core::AppConfig::Instance().GetRulesDbPath();
        duckDbPath  = freight::core::AppConfig::Instance().GetCacheDir() + "/schema_verify.duckdb";
        QFile::remove(rulesDbPath); QFile::remove(rulesDbPath + "-wal"); QFile::remove(rulesDbPath + "-shm");
        QFile::remove(duckDbPath);  QFile::remove(duckDbPath + ".wal");
        QDir().mkpath(QFileInfo(rulesDbPath).absolutePath());
        QDir().mkpath(QFileInfo(duckDbPath).absolutePath());
        // rollback 脚本绝对路径
        rollbackSqlPath = QString::fromUtf8(PROJECT_SOURCE_DIR) + "/docs/sql/rollback_customer_override_v15.sql";
    }
    ~SandboxEnv() {
        // 清鸭子连接：DuckDBManager 是单例，下次会被 Init(新路径) 覆盖，但老句柄如果还握着先释放
        try {
            auto &d = db::DuckDBManager::Instance();
            (void)d;
        } catch (...) {}
    }
};

// 用原生 QtSqlite 造 v10 老库（完全不经过 SqliteRuleRepository::Init，避免新列提前加）
static bool BuildV10LegacyDB(const QString &dbPath) {
    QFile::remove(dbPath);
    QString conn = "legacy_v10_" + QString::number(qHash(dbPath));
    auto db = QSqlDatabase::addDatabase("QSQLITE", conn);
    db.setDatabaseName(dbPath);
    if (!db.open()) { qCritical() << "open v10 fail:" << db.lastError().text(); return false; }
    { QSqlQuery q(db); q.exec("PRAGMA journal_mode=WAL"); }
    auto exe = [&](const char *sql){ QSqlQuery q(db); return q.exec(sql); };
    bool ok = true;
    ok &= exe("CREATE TABLE schema_version (version INTEGER)");
    { QSqlQuery q(db); q.prepare("INSERT INTO schema_version VALUES (10)"); ok &= q.exec(); }
    ok &= exe(R"SQL(
        CREATE TABLE customers (
            customer_id VARCHAR(100) PRIMARY KEY, customer_name VARCHAR(200) NOT NULL,
            discount_rate REAL DEFAULT 1.0, default_template VARCHAR(100),
            contact_person VARCHAR(100), contact_phone VARCHAR(50), address TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE freight_templates (
            template_id VARCHAR(100) PRIMARY KEY, template_name VARCHAR(200) NOT NULL,
            carrier_name VARCHAR(100), first_weight REAL DEFAULT 1.0, additional_unit REAL DEFAULT 1.0,
            vol_weight_ratio REAL DEFAULT 6000.0, default_no_weight_fee REAL DEFAULT 0, description TEXT,
            is_default INTEGER DEFAULT 0, created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE zone_groups (id INTEGER PRIMARY KEY AUTOINCREMENT,
            template_id VARCHAR(100) NOT NULL, group_code VARCHAR(20) NOT NULL,
            group_name VARCHAR(100) NOT NULL, sort_order INTEGER DEFAULT 0,
            UNIQUE(template_id, group_code))
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE zone_group_provinces (id INTEGER PRIMARY KEY AUTOINCREMENT,
            template_id VARCHAR(100) NOT NULL, group_code VARCHAR(20) NOT NULL,
            province VARCHAR(50) NOT NULL, UNIQUE(template_id, group_code, province))
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE tiered_pricing (id INTEGER PRIMARY KEY AUTOINCREMENT,
            template_id VARCHAR(100) NOT NULL, group_code VARCHAR(20) NOT NULL,
            tier_code VARCHAR(20) NOT NULL, tier_name VARCHAR(100),
            min_weight REAL NOT NULL, max_weight REAL NOT NULL,
            first_weight REAL DEFAULT 1.0, first_price REAL NOT NULL,
            additional_unit REAL DEFAULT 1.0, additional_price REAL NOT NULL,
            sort_order INTEGER DEFAULT 0, UNIQUE(template_id, group_code, tier_code))
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE fuel_surcharge (id INTEGER PRIMARY KEY AUTOINCREMENT,
            template_id VARCHAR(100) DEFAULT '*' NOT NULL, effective_date DATE NOT NULL,
            rate REAL NOT NULL, is_active INTEGER DEFAULT 1)
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE remote_areas (id INTEGER PRIMARY KEY AUTOINCREMENT,
            template_id VARCHAR(100) DEFAULT '*' NOT NULL, province VARCHAR(50),
            city VARCHAR(100), district VARCHAR(100), surcharge REAL DEFAULT 0,
            is_active INTEGER DEFAULT 1)
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE surcharge_strategies (id INTEGER PRIMARY KEY AUTOINCREMENT,
            strategy_id VARCHAR(100) UNIQUE NOT NULL, strategy_name VARCHAR(200) NOT NULL,
            strategy_scope VARCHAR(20) NOT NULL, template_id VARCHAR(100),
            strategy_type VARCHAR(20) NOT NULL, amount REAL NOT NULL DEFAULT 0,
            min_weight REAL, max_weight REAL, priority INTEGER NOT NULL DEFAULT 0,
            is_active INTEGER NOT NULL DEFAULT 1, description TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP, updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE surcharge_provinces (id INTEGER PRIMARY KEY AUTOINCREMENT,
            strategy_id VARCHAR(100) NOT NULL, province VARCHAR(50) NOT NULL,
            UNIQUE(strategy_id, province))
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE surcharge_customers (id INTEGER PRIMARY KEY AUTOINCREMENT,
            strategy_id VARCHAR(100) NOT NULL, customer_id VARCHAR(100) NOT NULL,
            UNIQUE(strategy_id, customer_id))
    )SQL");
    ok &= exe(R"SQL(
        CREATE TABLE surcharge_date_ranges (id INTEGER PRIMARY KEY AUTOINCREMENT,
            strategy_id VARCHAR(100) NOT NULL, start_date DATE NOT NULL,
            end_date DATE NOT NULL, week_days VARCHAR(20))
    )SQL");
    // 塞 v10 老客户（>=3 条）/老模板 + 区域/分区/阶梯/油费，绕过 ValidateIntegrity 防重灌
    auto insCust = [&](const QString &cid, const QString &cname, const QString &tid, double disc=1.0){
        QSqlQuery q(db);
        q.prepare("INSERT INTO customers (customer_id,customer_name,discount_rate,default_template,contact_person,contact_phone,address) VALUES (?,?,?,?,?,?,?)");
        q.addBindValue(cid); q.addBindValue(cname); q.addBindValue(disc);
        q.addBindValue(tid); q.addBindValue(""); q.addBindValue(""); q.addBindValue("");
        return q.exec();
    };
    ok &= insCust("v10_cust", "老客户V10", "zto_standard", 0.92);
    ok &= insCust("v10_keep1", "防重灌客户1", "zto_standard", 1.0);
    ok &= insCust("v10_keep2", "防重灌客户2", "zto_standard", 1.0);
    ok &= insCust("v10_keep3", "防重灌客户3", "zto_standard", 1.0);
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO freight_templates (template_id,template_name,carrier_name,first_weight,additional_unit,vol_weight_ratio,default_no_weight_fee,description,is_default) VALUES (?,?,?,?,?,?,?,?,1)");
        q.addBindValue("zto_standard"); q.addBindValue("中通标准"); q.addBindValue("中通");
        q.addBindValue(3.0); q.addBindValue(1.0); q.addBindValue(6000.0); q.addBindValue(3.0); q.addBindValue("老版");
        ok &= q.exec();
    }
    auto insZoneGrp = [&](const QString &code, const QString &name, int order){
        QSqlQuery q(db); q.prepare("INSERT INTO zone_groups (template_id,group_code,group_name,sort_order) VALUES ('zto_standard',?,?,?)");
        q.addBindValue(code); q.addBindValue(name); q.addBindValue(order);
        return q.exec();
    };
    auto insZoneProv = [&](const QString &code, const QString &prov){
        QSqlQuery q(db); q.prepare("INSERT INTO zone_group_provinces (template_id,group_code,province) VALUES ('zto_standard',?,?)");
        q.addBindValue(code); q.addBindValue(prov);
        return q.exec();
    };
    ok &= insZoneGrp("zone1", "江浙沪", 1);
    ok &= insZoneProv("zone1", "上海"); ok &= insZoneProv("zone1", "江苏"); ok &= insZoneProv("zone1", "浙江");
    auto insTier = [&](const QString &code, const QString &name, double mn, double mx, double fp, double ap, int order){
        QSqlQuery q(db);
        q.prepare("INSERT INTO tiered_pricing (template_id,group_code,tier_code,tier_name,min_weight,max_weight,first_weight,first_price,additional_unit,additional_price,sort_order) VALUES ('zto_standard','zone1',?,?,?,?,1.0,?,1.0,?,?)");
        q.addBindValue(code); q.addBindValue(name); q.addBindValue(mn); q.addBindValue(mx);
        q.addBindValue(fp); q.addBindValue(ap); q.addBindValue(order);
        return q.exec();
    };
    ok &= insTier("tier_0_0.5","0-0.5kg",0.0,0.5,5.0,0.0,1);
    ok &= insTier("tier_0.5_1","0.5-1kg",0.5,1.0,5.0,0.0,2);
    ok &= insTier("tier_1_3","1-3kg",1.0,3.0,5.0,1.0,3);
    ok &= insTier("tier_3_above","3kg+",3.0,9999.0,5.0,2.0,4);
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO fuel_surcharge (template_id,effective_date,rate,is_active) VALUES ('*','2025-01-01',3.0,1)");
        ok &= q.exec();
    }
    db.close();
    QSqlDatabase::removeDatabase(conn);
    return ok;
}

static int GetIntScalar(QSqlDatabase &db, const QString &sql) {
    QSqlQuery q(db); q.exec(sql);
    if (q.next()) return q.value(0).toInt();
    return -1;
}
static int GetIntScalar(QSqlQuery &q2, const char *sql) {
    q2.clear(); q2.exec(sql);
    if (q2.next()) return q2.value(0).toInt();
    return -1;
}
static qlonglong CountCols(QSqlDatabase &db, const QString &tbl) {
    QSqlQuery q(db);
    q.exec(QString("PRAGMA table_info(%1)").arg(tbl));
    qlonglong n = 0; while (q.next()) n++;
    return n;
}
static bool HasTable(QSqlDatabase &db, const QString &tbl) {
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
    q.addBindValue(tbl); return q.exec() && q.next();
}
static bool HasIndex(QSqlDatabase &db, const QString &idx) {
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM sqlite_master WHERE type='index' AND name=?");
    q.addBindValue(idx); return q.exec() && q.next();
}
static bool ColExists(QSqlDatabase &db, const QString &tbl, const QString &col) {
    QSqlQuery q(db);
    q.exec(QString("PRAGMA table_info(%1)").arg(tbl));
    while (q.next()) if (q.value(1).toString().compare(col, Qt::CaseInsensitive) == 0) return true;
    return false;
}

static int DoSyncDuckAndAssert(const QString &rulesDbPath, const QString &duckDbPath) {
    QFile::remove(duckDbPath); QFile::remove(duckDbPath + ".wal");
    QDir().mkpath(QFileInfo(duckDbPath).absolutePath());
    auto &dbm = db::DuckDBManager::Instance();
    ASSERT_TRUE(dbm.Init(duckDbPath), "DuckDB Init 成功");
    ASSERT_TRUE(dbm.ReloadRules(rulesDbPath), "DuckDB ReloadRules 成功");
    try {
        auto con = dbm.CreateConnection();
        auto r1 = con.Query("SELECT count(*) FROM pragma_table_info('customers')");
        auto ch1 = r1->Fetch();
        int64_t c1 = ch1 ? ch1->GetValue(0, 0).GetValue<int64_t>() : -1;
        ASSERT_EQ_INT((qlonglong)c1, 14, "DuckDB customers 列数=14");
        auto r2 = con.Query("SELECT count(*) FROM pragma_table_info('avg_weight_templates')");
        auto ch2 = r2->Fetch();
        int64_t c2 = ch2 ? ch2->GetValue(0, 0).GetValue<int64_t>() : -1;
        ASSERT_EQ_INT((qlonglong)c2, 22, "DuckDB avg_weight_templates 列数=22");
        auto r3 = con.Query("SELECT count(*) FROM pragma_table_info('avg_weight_zones')");
        auto ch3 = r3->Fetch();
        int64_t c3 = ch3 ? ch3->GetValue(0, 0).GetValue<int64_t>() : -1;
        ASSERT_EQ_INT((qlonglong)c3, 4, "DuckDB avg_weight_zones 列数=4");
        // 默认客户同步到了 DuckDB：行数至少 1
        auto r4 = con.Query("SELECT count(*) FROM customers");
        auto ch4 = r4->Fetch();
        int64_t n4 = ch4 ? ch4->GetValue(0, 0).GetValue<int64_t>() : 0;
        ASSERT_TRUE(n4 >= 1, "DuckDB customers 至少 1 行");
    } catch (const std::exception &e) {
        qCritical() << "DuckDB 列数异常：" << e.what();
        return 1;
    }
    return 0;
}

static int VerifyFreshDB() {
    qInfo() << "\n========= 测试 1：新库 Fresh 初始化（第一次运行）=========";
    SandboxEnv env("fresh");
    db::SqliteRuleRepository repo(env.rulesDbPath);
    ASSERT_TRUE(repo.Init(), "Repo Fresh Init 成功");
    auto &db = repo.Database();
    ASSERT_EQ_INT(GetIntScalar(db, "SELECT version FROM schema_version"), 15, "schema_version = 15");
    ASSERT_EQ_INT(CountCols(db, "customers"), 14, "customers 列数=14（9原 +5新）");
    ASSERT_TRUE(ColExists(db, "customers", "cust_rounding_mode"), "customers.cust_rounding_mode 存在");
    ASSERT_TRUE(ColExists(db, "customers", "cust_additional_unit"), "customers.cust_additional_unit 存在");
    ASSERT_TRUE(ColExists(db, "customers", "cust_vol_divisor"), "customers.cust_vol_divisor 存在");
    ASSERT_TRUE(ColExists(db, "customers", "avg_weight_tpl_id"), "customers.avg_weight_tpl_id 存在");
    ASSERT_TRUE(ColExists(db, "customers", "cust_contract_no"), "customers.cust_contract_no 存在");
    ASSERT_EQ_INT(CountCols(db, "freight_templates"), 14, "freight_templates 列数=14（11基础 + 3计费参数列）");
    ASSERT_TRUE(HasTable(db, "avg_weight_templates"), "avg_weight_templates 存在");
    ASSERT_TRUE(HasTable(db, "avg_weight_zones"), "avg_weight_zones 存在");
    ASSERT_EQ_INT(CountCols(db, "avg_weight_templates"), 22, "avg_weight_templates 22列（含版本+生效日+双层1kg上限）");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "version"), "version 版本号列存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "effective_from"), "effective_from 生效日存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "effective_to"), "effective_to 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "avg_pool_max_kg"), "L2 进池上限 avg_pool_max_kg 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "avg_fee_cap_kg"), "L4 加价封顶 avg_fee_cap_kg 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "base_avg_kg"), "约定基准 base_avg_kg 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "base_fee"), "基准价 base_fee 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "step_kg"), "每 step_kg 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "step_fee"), "每 step_fee 存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "min_tickets"), "min_tickets 样本量存在");
    ASSERT_TRUE(ColExists(db, "avg_weight_templates", "reuse_zone_groups"), "reuse_zone_groups 存在");
    ASSERT_TRUE(HasIndex(db, "idx_avg_weight_zones_tpl"), "索引 idx_avg_weight_zones_tpl 存在");
    ASSERT_TRUE(GetIntScalar(db, "SELECT COUNT(*) FROM freight_templates WHERE is_default=1") >= 1, "默认模板已初始化");
    ASSERT_TRUE(GetIntScalar(db, "SELECT COUNT(*) FROM customers") >= 1, "默认客户已初始化");
    int rc = DoSyncDuckAndAssert(env.rulesDbPath, env.duckDbPath); if (rc) return rc;
    qInfo().noquote() << "  ✅ 测试 1 新库 Fresh 通过";
    return 0;
}

static int VerifyLegacyUpgrade() {
    qInfo() << "\n========= 测试 2：老库 v10 升级到 v15（不丢数据、重入安全）=========";
    SandboxEnv env("legacy");
    ASSERT_TRUE(BuildV10LegacyDB(env.rulesDbPath), "构造 v10 老库成功");
    // 老库校验
    {
        QString cname = "precheck_" + QString::number(qHash(env.rulesDbPath));
        auto db = QSqlDatabase::addDatabase("QSQLITE", cname);
        db.setDatabaseName(env.rulesDbPath);
        ASSERT_TRUE(db.open(), "预检查老库打开成功");
        ASSERT_EQ_INT(CountCols(db, "customers"), 9, "升级前 customers 列数=9");
        ASSERT_TRUE(!ColExists(db, "customers", "avg_weight_tpl_id"), "升级前 customers 无 avg_weight_tpl_id");
        ASSERT_TRUE(!HasTable(db, "avg_weight_templates"), "升级前 avg_weight_templates 不存在");
        ASSERT_EQ_INT(GetIntScalar(db, "SELECT version FROM schema_version"), 10, "升级前 schema_version = 10");
        db.close();
        QSqlDatabase::removeDatabase(cname);
    }
    db::SqliteRuleRepository repo(env.rulesDbPath);
    ASSERT_TRUE(repo.Init(), "Repo Init 触发 10→11→12→13→14→15 升级成功");
    auto &db = repo.Database();
    ASSERT_EQ_INT(GetIntScalar(db, "SELECT version FROM schema_version"), 15, "升级后 schema_version = 15");
    ASSERT_EQ_INT(CountCols(db, "customers"), 14, "升级后 customers 列数=14");
    ASSERT_TRUE(HasTable(db, "avg_weight_templates") && HasTable(db, "avg_weight_zones"), "两张均重合同表已建");
    ASSERT_EQ_INT(CountCols(db, "avg_weight_templates"), 22, "avg_weight_templates 22列");
    ASSERT_TRUE(HasIndex(db, "idx_avg_weight_zones_tpl"), "分区索引存在");
    // 老数据保留
    {
        QSqlQuery q(db);
        q.exec("SELECT customer_id,customer_name,discount_rate,default_template FROM customers WHERE customer_id='v10_cust'");
        ASSERT_TRUE(q.next() && q.value(0).toString()=="v10_cust" && q.value(1).toString()=="老客户V10", "v10_cust 客户行保留");
        ASSERT_TRUE(q.value(2).toDouble() == 0.92, "v10 客户折扣 0.92 保留");
        ASSERT_TRUE(q.value(3).toString() == "zto_standard", "v10 客户默认模板 zto_standard 保留");
    }
    {
        QSqlQuery q(db);
        q.exec("SELECT template_id,carrier_name FROM freight_templates WHERE template_id='zto_standard'");
        ASSERT_TRUE(q.next() && q.value(1).toString()=="中通", "老模板 zto_standard 行保留");
    }
    // Schema 14→15 tryAddCol 可重入：第二次 Init 不报错，版本号仍为 15
    ASSERT_TRUE(repo.Init(), "Init 重入第二次（duplicate column 容错）成功");
    ASSERT_EQ_INT(GetIntScalar(db, "SELECT version FROM schema_version"), 15, "重入后版本号仍 15");
    int rc = DoSyncDuckAndAssert(env.rulesDbPath, env.duckDbPath); if (rc) return rc;
    qInfo().noquote() << "  ✅ 测试 2 老库 v10→v15 升级通过";
    return 0;
}

static int VerifyRollbackScript() {
    qInfo() << "\n========= 测试 3：R3 回滚脚本温和降级可运行（PRAGMA user_version=10 生效）=========";
    SandboxEnv env("rollback");
    // 先建 v15
    {
        db::SqliteRuleRepository repo(env.rulesDbPath);
        ASSERT_TRUE(repo.Init(), "v15 初始化成功");
    }
    ASSERT_TRUE(QFile::exists(env.rollbackSqlPath),
                QString("找到回滚脚本：%1").arg(env.rollbackSqlPath).toUtf8().constData());
    QFile rf(env.rollbackSqlPath);
    ASSERT_TRUE(rf.open(QIODevice::ReadOnly | QIODevice::Text), "回滚脚本可读");
    QString script = QString::fromUtf8(rf.readAll());
    // 1) 优先用官方推荐的 sqlite3 命令行跑：`sqlite3 rules.db < rollback.sql`
    //    因为脚本里有行内 `-- comment` 同行注释（如 `= 1.0, -- 上沿`），
    //    自己按分号切片容易误把注释行当未终止语句。
    bool scriptOk = false;
    QStringList sqlBinCandidates { "sqlite3", "/usr/bin/sqlite3", "/opt/homebrew/bin/sqlite3", "/usr/local/bin/sqlite3" };
    QString sqlBin;
    for (const auto &c : sqlBinCandidates) {
        if (QFile::exists(c) || (!c.startsWith('/') && !QStandardPaths::findExecutable(c).isEmpty())) {
            sqlBin = c; break;
        }
    }
    if (!sqlBin.isEmpty() && !sqlBin.startsWith('/')) {
        sqlBin = QStandardPaths::findExecutable(sqlBin);
    }
    if (!sqlBin.isEmpty()) {
        QProcess p;
        QFile rf2(env.rollbackSqlPath);
        rf2.open(QIODevice::ReadOnly);
        QByteArray scriptBytes = rf2.readAll();
        rf2.close();
        // sqlite3 对 .quit/.echo 这类点命令有独立解析，但本脚本只有标准SQL，喂 stdin 即可
        p.start(sqlBin, QStringList() << env.rulesDbPath);
        if (!p.waitForStarted(3000)) {
            qWarning() << "sqlite3 无法启动（" << sqlBin << "），fallback 到 QtSql 切片模式";
        } else {
            p.write(scriptBytes);
            p.closeWriteChannel();
            if (!p.waitForFinished(15000)) {
                qCritical() << "sqlite3 回滚执行超时";
                return 1;
            }
            QByteArray out = p.readAllStandardOutput();
            QByteArray err = p.readAllStandardError();
            if (p.exitCode() != 0 && !err.isEmpty()) {
                QString e = QString::fromUtf8(err).trimmed();
                // sqlite3 温和降级的预期错误：no such column / no such table 都正常
                QStringList elines = e.split('\n', Qt::SkipEmptyParts);
                bool benign = !elines.isEmpty();
                for (const auto &ln : elines) {
                    QString l = ln.trimmed().toLower();
                    if (l.isEmpty()) continue;
                    if (l.startsWith("--")) continue;
                    if (l.contains("no such column") || l.contains("no such table") ||
                        l.contains("duplicate column") || l.contains("already exists")) continue;
                    if (l.contains("usage:") || l.contains("error")) { benign = false; break; }
                    benign = false; break;
                }
                if (!benign) {
                    qCritical() << "sqlite3 回滚失败(stdout=" << out.left(240) << "stderr=" << err.left(480) << ")";
                    return 1;
                } else {
                    qInfo() << "  sqlite3 回滚执行完成（含温和降级忽略的错误，见 stderr）";
                }
            } else {
                qInfo() << "  sqlite3 回滚脚本执行成功（exit 0）";
            }
            scriptOk = true;
        }
    }
    // 2) Fallback：QtSql 自己解析（去行内注释，去整行注释，按 ; 切片）
    //    先打开独立 QtSql 连接
    QString cname = "rb_" + QString::number(qHash(env.rulesDbPath));
    QSqlDatabase fallbackDb;
    if (!scriptOk) {
        fallbackDb = QSqlDatabase::addDatabase("QSQLITE", cname);
        fallbackDb.setDatabaseName(env.rulesDbPath);
        ASSERT_TRUE(fallbackDb.open(), "QtSql fallback 回滚阶段数据库打开");
    }
    if (!scriptOk) {
        qInfo() << "  [fallback] QtSql 自解析回滚脚本（已去行内注释）";
        // 去整行注释、去行尾 `-- xxx` 同行注释
        QStringList lines = script.split('\n');
        for (auto &ln : lines) {
            int dashDash = -1;
            bool inString = false;
            for (int i = 0; i+1 < ln.size(); i++) {
                QChar c = ln.at(i);
                if (c == '\'' && (i+1 >= ln.size() || ln.at(i+1) != '\'') &&
                    (i==0 || ln.at(i-1) != '\'')) { inString = !inString; continue; }
                if (!inString && c == '-' && ln.at(i+1) == '-') { dashDash = i; break; }
            }
            if (dashDash >= 0) ln = ln.left(dashDash);
        }
        QString cleaned = lines.join('\n');
        QStringList blocks; QString cur;
        for (int i = 0; i < cleaned.size(); i++) {
            QChar c = cleaned.at(i);
            if (c == ';') { blocks << cur.trimmed(); cur.clear(); }
            else cur += c;
        }
        if (!cur.trimmed().isEmpty()) blocks << cur.trimmed();
        for (const auto &b : blocks) {
            if (b.isEmpty()) continue;
            QString head = b.trimmed().left(12).toLower();
            if (head.startsWith('.')) continue;
            QSqlQuery q(fallbackDb);
            if (!q.exec(b)) {
                QString e = q.lastError().text().toLower();
                if (e.contains("no such column") || e.contains("no such table") ||
                    e.contains("duplicate column") || e.contains("already exists")) continue;
                qCritical() << "回滚语句失败(" << q.lastError().text() << "):\n  " << b.left(200);
                return 1;
            }
        }
    }
    // 温和降级的核心标志：PRAGMA user_version 回到 10。若用 sqlite3 成功，也读一下验证
    int v = -1;
    {
        QString cname2 = "rb_uv_" + QString::number(qHash(env.rulesDbPath));
        auto dbChk = QSqlDatabase::addDatabase("QSQLITE", cname2);
        dbChk.setDatabaseName(env.rulesDbPath);
        if (dbChk.open()) {
            QSqlQuery uv(dbChk);
            uv.exec("PRAGMA user_version"); if (uv.next()) v = uv.value(0).toInt();
            dbChk.close();
        }
        QSqlDatabase::removeDatabase(cname2);
    }
    if (!scriptOk) {
        if (fallbackDb.isOpen()) fallbackDb.close();
        QSqlDatabase::removeDatabase(cname);
    }
    ASSERT_EQ_INT(v, 10, "温和降级后 PRAGMA user_version = 10");
    qInfo().noquote() << "  ✅ 测试 3 R3 回滚脚本温和降级通过";
    return 0;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qSetMessagePattern("%{type} %{message}");
    int rc = 0;
    rc = VerifyFreshDB();         if (rc) return rc;
    rc = VerifyLegacyUpgrade();   if (rc) return rc;
    rc = VerifyRollbackScript();  if (rc) return rc;
    qInfo() << "\n🎉 Step2 DB Schema：3 大测试全部通过（新库 Fresh / v10→v15 升级 / R3 回滚）";
    return 0;
}
