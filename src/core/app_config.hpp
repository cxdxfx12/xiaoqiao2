#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSettings>
#include <memory>

namespace freight::core {

class AppConfig {
public:
    static AppConfig& Instance() {
        static AppConfig instance;
        return instance;
    }

    bool Init();

    QString GetDataDir() const;
    QString GetRulesDbPath() const;
    QString GetHistoryDbPath() const;
    QString GetResultsDir() const;
    QString GetCacheDir() const;
    QString GetLogsDir() const;

    QString GetCompanyName() const;
    QString GetAppName() const;
    QString GetWebsite() const;
    QString GetServicePhone() const;
    QString GetVersion() const;

    static int GetTotalMemoryMB();
    static int GetCpuCoreCount();

    int GetMemoryLimitMB() const;
    int GetThreadCount() const;
    bool GetAutoPerformance() const;

    void SetMemoryLimitMB(int mb);
    void SetThreadCount(int count);
    void SetAutoPerformance(bool auto_perf);

    void ApplyAutoPerformance();

    static const QMap<QString, QString>& StandardColumnChinese();
    static const QStringList& StandardColumnOrder();
    static QString StandardColumnToCn(const QString &en_name);
    static QString CnToStandardColumn(const QString &cn_name);
    static const QMap<QString, QStringList>& DefaultMappingKeywords();
    static const QSet<QString>& RequiredStandardColumns();

    QMap<QString, QStringList> GetMappingKeywords() const;
    void SetMappingKeywords(const QMap<QString, QStringList>& map);
    void AddMappingKeyword(const QString& standard_col, const QString& keyword);
    void RemoveMappingKeyword(const QString& standard_col, const QString& keyword);
    void ResetMappingKeywords();
    QMap<QString, QStringList> GetEffectiveMappingKeywords() const;

private:
    AppConfig() = default;
    ~AppConfig() = default;
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    void LoadPerformanceSettings();
    void SavePerformanceSettings();
    void LoadMappingKeywords();
    void SaveMappingKeywords();

    QString data_dir_;
    std::unique_ptr<QSettings> settings_;
    int memory_limit_mb_ = 4096;
    int thread_count_ = 4;
    bool auto_performance_ = true;
    QMap<QString, QStringList> custom_keywords_;
};

} // namespace freight::core
