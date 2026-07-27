#include "core/app_config.hpp"
#include "core/license_manager.hpp"
#include "db/duckdb_manager.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "services/calc_service.hpp"
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QDebug>
#include <QElapsedTimer>

using namespace freight;

static int ProcessOne(const QString &input, const QString &output) {
    services::CalcService calc_svc;
    QElapsedTimer t;
    t.start();
    if (!calc_svc.CalcFromFile(input, output)) {
        qCritical() << "  [FAIL] 计算失败:" << input;
        return 1;
    }

    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();
        auto cnt_res = con.Query("SELECT COUNT(*) FROM _output_tmp");
        int64_t rows = cnt_res->GetValue(0, 0).GetValue<int64_t>();
        double total_fee = 0.0;
        try {
            auto fee = con.Query("SELECT SUM(总运费) FROM _output_tmp");
            if (fee->RowCount() > 0) total_fee = fee->GetValue(0, 0).GetValue<double>();
        } catch (...) {}
        qInfo().noquote() << QString("  [OK]  行数=%1  合计运费=%2 (元)  耗时=%3 s -> %4")
                                 .arg(rows)
                                 .arg(total_fee, 0, 'f', 2)
                                 .arg(t.elapsed() / 1000.0, 0, 'f', 1)
                                 .arg(output);
    } catch (...) {}
    return 0;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("小乔运费结算");
    QCoreApplication::setOrganizationName("杭州喵喵至家网络有限公司");

    auto &cfg = core::AppConfig::Instance();
    if (!cfg.Init()) {
        qCritical() << "AppConfig::Init failed";
        return 2;
    }

    auto &lic = core::LicenseManager::Instance();
    lic.Init();
    qInfo() << "授权状态:" << (lic.GetLicenseInfo().type == core::LicenseType::Permanent ? QStringLiteral("永久版") :
                               lic.GetLicenseInfo().type == core::LicenseType::Personal  ? QStringLiteral("个人版") :
                               lic.GetLicenseInfo().type == core::LicenseType::Enterprise ? QStringLiteral("企业版") : QStringLiteral("试用"))
            << "剩余天数:" << lic.GetLicenseInfo().DaysRemaining();

    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    if (!repo.Init()) {
        qCritical() << "SqliteRuleRepository::Init failed";
        return 3;
    }

    auto &dbm = db::DuckDBManager::Instance();
    if (!dbm.Init(cfg.GetDataDir() + "/calc.duckdb")) {
        qCritical() << "DuckDBManager::Init failed";
        return 4;
    }
    // 每次 batch_runner 从干净 duckdb 启动，避免历史 75 万行 _output_tmp 被误导出
    dbm.ResetDB();
    // ResetDB 之后需要重设内存/线程配置
    {
        auto &cfg2 = core::AppConfig::Instance();
        try {
            auto con = dbm.CreateConnection();
            QString mem_limit = QString("%1MB").arg(cfg2.GetMemoryLimitMB());
            QString threads   = QString::number(cfg2.GetThreadCount());
            con.Query(QString("SET memory_limit = '%1'").arg(mem_limit).toStdString());
            con.Query(QString("SET threads = %1").arg(threads).toStdString());
            try { con.Query("LOAD excel"); } catch (...) {}
        } catch (...) {}
    }
    if (!dbm.LoadRulesFromSQLite(cfg.GetRulesDbPath())) {
        qCritical() << "LoadRulesFromSQLite failed";
        return 5;
    }

    QStringList args = app.arguments().mid(1);
    QStringList inputs;
    QString out_dir;
    for (int i = 0; i < args.size(); i++) {
        if (args[i] == "-o" && i + 1 < args.size()) { out_dir = args[++i]; continue; }
        inputs << args[i];
    }

    if (inputs.isEmpty()) {
        qInfo() << "用法: batch_runner [-o 输出目录] 输入文件1 [输入文件2 ...]";
        qInfo() << "示例: batch_runner -o /Users/cxd/帐单 /Users/cxd/帐单/*.xlsx";
        qInfo().noquote() << QString("  输出文件名规则：<原文件名>_<当前日期YYYYMMDD>_已结算.xlsx  例：珀莱雅-4月发件账单_%1_已结算.xlsx")
                             .arg(QDate::currentDate().toString("yyyyMMdd"));
        qInfo() << "  （自动剥除历史的 _结果/_运费结果/已结算 等重复后缀）";
        return 0;
    }

    if (out_dir.isEmpty()) {
        out_dir = QFileInfo(inputs.first()).absolutePath();
    }
    QDir().mkpath(out_dir);
    out_dir = QFileInfo(out_dir).absoluteFilePath();
    QString today = QDate::currentDate().toString("yyyyMMdd");

    auto NormalizeBase = [](QString base) -> QString {
        static const QStringList tails = {
            QStringLiteral("_运费结果_运费结果"),
            QStringLiteral("_结果_结果"),
            QStringLiteral("已结算已结算"),
            QStringLiteral("_运费结果"),
            QStringLiteral("_结果"),
            QStringLiteral("已结算"),
        };
        for (int iter = 0; iter < 3; ++iter) {
            bool stripped = false;
            // 剥 _YYYYMMDD 日期尾巴
            if (base.length() >= 9) {
                QString tail = base.right(9);
                if (tail.startsWith('_')) {
                    QString digits = tail.mid(1);
                    if (digits.length() == 8) {
                        bool ok = true;
                        for (int i = 0; i < 8; ++i) if (!digits[i].isDigit()) { ok = false; break; }
                        if (ok) { base.chop(9); stripped = true; }
                    }
                }
            }
            if (!stripped) {
                for (const auto &t : tails) {
                    if (base.endsWith(t, Qt::CaseInsensitive)) {
                        base.chop(t.length());
                        stripped = true;
                        break;
                    }
                }
            }
            if (!stripped) break;
        }
        return base;
    };

    qInfo() << "==== 批量计算开始 ====";
    qInfo() << "输出目录:" << out_dir;

    int total_fail = 0;
    int total_ok = 0;
    for (const auto &inp : inputs) {
        QFileInfo fi(inp);
        if (!fi.exists()) { qWarning() << "  [SKIP] 文件不存在:" << inp; continue; }
        qInfo() << "处理:" << fi.fileName();
        QString suf = fi.suffix().toLower();
        QString out_suf = (suf == "csv" ? "csv" : (suf == "parquet" ? "parquet" : "xlsx"));
        QString clean_base = NormalizeBase(fi.completeBaseName());
        QString out_name = clean_base + QStringLiteral("_") + today
                        + QStringLiteral("_已结算.") + out_suf;
        QString out = QDir(out_dir).filePath(out_name);
        if (ProcessOne(inp, out) == 0) total_ok++;
        else total_fail++;
    }

    qInfo() << "==== 批量计算结束 ====";
    qInfo().noquote() << QString("成功 %1 个，失败 %2 个，总输出目录: %3").arg(total_ok).arg(total_fail).arg(out_dir);
    return total_fail > 0 ? 10 : 0;
}
