#pragma once
#include <QString>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QThread>
#include <memory>

#ifdef Q_OS_MAC
#include <sys/sysctl.h>
#endif

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

        settings_ = std::make_unique<QSettings>(data_dir_ + "/config.ini", QSettings::IniFormat);
        LoadPerformanceSettings();
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

    static int GetTotalMemoryMB() {
#ifdef Q_OS_MAC
        int64_t mem = 0;
        size_t len = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) {
            return static_cast<int>(mem / (1024 * 1024));
        }
        return 8192;
#else
        return 8192;
#endif
    }

    static int GetCpuCoreCount() {
        return QThread::idealThreadCount() > 0 ? QThread::idealThreadCount() : 4;
    }

    int GetMemoryLimitMB() const { return memory_limit_mb_; }
    int GetThreadCount() const { return thread_count_; }
    bool GetAutoPerformance() const { return auto_performance_; }

    void SetMemoryLimitMB(int mb) {
        memory_limit_mb_ = mb;
        auto_performance_ = false;
        SavePerformanceSettings();
    }

    void SetThreadCount(int count) {
        thread_count_ = count;
        auto_performance_ = false;
        SavePerformanceSettings();
    }

    void SetAutoPerformance(bool auto_perf) {
        auto_performance_ = auto_perf;
        if (auto_perf) {
            ApplyAutoPerformance();
        }
        SavePerformanceSettings();
    }

    void ApplyAutoPerformance() {
        int total_mem = GetTotalMemoryMB();
        memory_limit_mb_ = static_cast<int>(total_mem * 0.9);
        int cores = GetCpuCoreCount();
        thread_count_ = std::max(1, static_cast<int>(cores * 0.9));
    }

private:
    AppConfig() = default;

    void LoadPerformanceSettings() {
        auto_performance_ = settings_->value("performance/auto", true).toBool();
        if (auto_performance_) {
            ApplyAutoPerformance();
        } else {
            memory_limit_mb_ = settings_->value("performance/memory_limit_mb", 4096).toInt();
            thread_count_ = settings_->value("performance/thread_count", 4).toInt();
        }
    }

    void SavePerformanceSettings() {
        settings_->setValue("performance/auto", auto_performance_);
        settings_->setValue("performance/memory_limit_mb", memory_limit_mb_);
        settings_->setValue("performance/thread_count", thread_count_);
        settings_->sync();
    }

    QString data_dir_;
    std::unique_ptr<QSettings> settings_;
    int memory_limit_mb_ = 4096;
    int thread_count_ = 4;
    bool auto_performance_ = true;
};

} // namespace freight::core
