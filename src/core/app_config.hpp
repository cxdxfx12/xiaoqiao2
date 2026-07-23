#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <memory>

namespace freight::core {

struct TemplateFingerprint {
    QString template_id;
    QString display_name;
    QString courier_name;
    QStringList required_keywords;
    QMap<QString, QString> column_mapping;
    bool enabled = true;
    bool is_builtin = false;
};

inline bool operator==(const TemplateFingerprint &a, const TemplateFingerprint &b) {
    return a.template_id == b.template_id;
}

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

    // ========== S5 记住常用目录 ==========
    QString GetLastInputDir() const;
    void SetLastInputDir(const QString &dir);
    QString GetLastOutputDir() const;
    void SetLastOutputDir(const QString &dir);
    QStringList GetRecentFiles() const;
    void AddRecentFile(const QString &file);
    void ClearRecentFiles();

    // ========== S6 金额染色阈值 ==========
    double GetFeeLowThreshold() const;
    void SetFeeLowThreshold(double v);
    double GetFeeHighThreshold() const;
    void SetFeeHighThreshold(double v);

    // ========== 功能7 快递模板指纹库 ==========
    static const QList<TemplateFingerprint>& BuiltinTemplateFingerprints();
    QList<TemplateFingerprint> GetCustomTemplateFingerprints() const;
    void AddCustomTemplateFingerprint(const TemplateFingerprint &fp);
    void RemoveCustomTemplateFingerprint(const QString &template_id);
    void UpdateCustomTemplateFingerprint(const TemplateFingerprint &fp);
    QList<TemplateFingerprint> GetAllTemplateFingerprints(bool enabled_only = false) const;
    bool IsTemplateEnabled(const QString &template_id) const;
    void SetTemplateEnabled(const QString &template_id, bool enabled);
    bool GetTemplateAutoDetectGlobal() const;
    void SetTemplateAutoDetectGlobal(bool enabled);

private:
    AppConfig() = default;
    ~AppConfig() = default;
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    void LoadPerformanceSettings();
    void SavePerformanceSettings();
    void LoadMappingKeywords();
    void SaveMappingKeywords();
    void LoadRecentFiles();
    void SaveRecentFiles();
    void LoadFeeThresholds();
    void SaveFeeThresholds();
    void LoadCustomTemplates();
    void SaveCustomTemplates();

    QString data_dir_;
    std::unique_ptr<QSettings> settings_;
    int memory_limit_mb_ = 4096;
    int thread_count_ = 4;
    bool auto_performance_ = true;
    QMap<QString, QStringList> custom_keywords_;

    QString last_input_dir_;
    QString last_output_dir_;
    QStringList recent_files_;
    double fee_low_threshold_ = 5.0;
    double fee_high_threshold_ = 20.0;
    QList<TemplateFingerprint> custom_templates_;
    QSet<QString> disabled_template_ids_;
    bool template_auto_detect_global_ = true;
};

} // namespace freight::core
