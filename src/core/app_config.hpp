#pragma once
#include <QString>
#include <QStandardPaths>
#include <QDir>

namespace freight::core {

class AppConfig {
public:
    static AppConfig& Instance() {
        static AppConfig instance;
        return instance;
    }

    bool Init() {
        QString data_dir = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        QDir().mkpath(data_dir);
        QDir().mkpath(data_dir + "/results");
        QDir().mkpath(data_dir + "/cache");
        QDir().mkpath(data_dir + "/logs");
        data_dir_ = data_dir;
        return true;
    }

    QString GetDataDir() const { return data_dir_; }
    QString GetRulesDbPath() const { return data_dir_ + "/rules.db"; }
    QString GetHistoryDbPath() const { return data_dir_ + "/history.db"; }
    QString GetResultsDir() const { return data_dir_ + "/results"; }
    QString GetCacheDir() const { return data_dir_ + "/cache"; }
    QString GetLogsDir() const { return data_dir_ + "/logs"; }

    QString GetCompanyName() const { return "杭州喵喵至家网络有限公司"; }
    QString GetAppName() const { return "小乔运费结算"; }
    QString GetWebsite() const { return "www.hbdxm.com"; }
    QString GetServicePhone() const { return "17771300068 / 19171045360"; }
    QString GetVersion() const { return "1.0.0"; }

private:
    AppConfig() = default;
    QString data_dir_;
};

} // namespace freight::core
