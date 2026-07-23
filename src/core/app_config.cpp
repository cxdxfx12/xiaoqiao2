#include "app_config.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QThread>
#include <QSet>
#include <algorithm>

#ifdef Q_OS_MAC
#include <sys/sysctl.h>
#endif

namespace freight::core {

static QMap<QString, QString> BuildStandardColumnChinese() {
    QMap<QString, QString> m;
    m.insert("order_id", "订单号");
    m.insert("dest_province", "目的省份");
    m.insert("dest_city", "目的城市");
    m.insert("weight", "实际重量(KG)");
    m.insert("vol_weight", "体积重量(KG)");
    m.insert("customer_id", "客户编号");
    return m;
}

static QStringList BuildStandardColumnOrder() {
    return QStringList{
        "order_id", "dest_province", "dest_city",
        "weight", "vol_weight", "customer_id"
    };
}

static QSet<QString> BuildRequiredStandardColumns() {
    return QSet<QString>{"dest_province", "weight"};
}

static QMap<QString, QStringList> BuildDefaultMappingKeywords() {
    QMap<QString, QStringList> m;
    m.insert("order_id", {
        "order_id", "order_no", "waybill", "运单号", "订单号", "快递单号", "单号",
        "运单编号", "订单编号", "面单号", "trackingno", "tracking_no", "awbno"
    });
    m.insert("dest_province", {
        "dest_province", "province", "目的省份", "省份", "收件省份", "寄达省份",
        "到件省", "目的地省", "收货省", "省"
    });
    m.insert("dest_city", {
        "dest_city", "city", "目的城市", "城市", "收件城市", "寄达城市",
        "到件市", "目的地市", "收货市", "市"
    });
    m.insert("weight", {
        "weight", "actual_weight", "实重", "实际重量", "结算重量", "重量",
        "毛重", "计费重", "charge_weight", "计费重量", "货物重量", "kg"
    });
    m.insert("vol_weight", {
        "vol_weight", "volume_weight", "volumetric_weight", "体积重量", "体积重",
        "材积重", "材积", "抛重"
    });
    m.insert("customer_id", {
        "customer_id", "customer", "客户", "客户编号", "客户代码", "客户名称",
        "商家编号", "商家", "客户ID", "网点", "网点编号"
    });
    return m;
}

const QMap<QString, QString>& AppConfig::StandardColumnChinese() {
    static const auto m = BuildStandardColumnChinese();
    return m;
}

const QStringList& AppConfig::StandardColumnOrder() {
    static const auto list = BuildStandardColumnOrder();
    return list;
}

const QSet<QString>& AppConfig::RequiredStandardColumns() {
    static const auto s = BuildRequiredStandardColumns();
    return s;
}

QString AppConfig::StandardColumnToCn(const QString &en_name) {
    const auto &m = StandardColumnChinese();
    auto it = m.find(en_name);
    return it != m.end() ? it.value() : en_name;
}

QString AppConfig::CnToStandardColumn(const QString &cn_name) {
    const auto &m = StandardColumnChinese();
    for (auto it = m.begin(); it != m.end(); ++it) {
        if (it.value() == cn_name) return it.key();
    }
    return cn_name;
}

const QMap<QString, QStringList>& AppConfig::DefaultMappingKeywords() {
    static const auto m = BuildDefaultMappingKeywords();
    return m;
}

bool AppConfig::Init() {
    QString data_dir = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(data_dir);
    QDir().mkpath(data_dir + "/results");
    QDir().mkpath(data_dir + "/cache");
    QDir().mkpath(data_dir + "/logs");
    data_dir_ = data_dir;

    settings_ = std::make_unique<QSettings>(data_dir_ + "/config.ini", QSettings::IniFormat);
    LoadPerformanceSettings();
    LoadMappingKeywords();
    return true;
}

QString AppConfig::GetDataDir() const { return data_dir_; }
QString AppConfig::GetRulesDbPath() const { return data_dir_ + "/rules.db"; }
QString AppConfig::GetHistoryDbPath() const { return data_dir_ + "/history.db"; }
QString AppConfig::GetResultsDir() const { return data_dir_ + "/results"; }
QString AppConfig::GetCacheDir() const { return data_dir_ + "/cache"; }
QString AppConfig::GetLogsDir() const { return data_dir_ + "/logs"; }

QString AppConfig::GetCompanyName() const { return QStringLiteral("杭州喵喵至家网络有限公司"); }
QString AppConfig::GetAppName() const { return QStringLiteral("小乔运费结算"); }
QString AppConfig::GetWebsite() const { return QStringLiteral("www.hbdxm.com"); }
QString AppConfig::GetServicePhone() const { return QStringLiteral("17771300068 / 19171045360"); }
QString AppConfig::GetVersion() const { return QStringLiteral("1.0.0"); }

int AppConfig::GetTotalMemoryMB() {
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

int AppConfig::GetCpuCoreCount() {
    return QThread::idealThreadCount() > 0 ? QThread::idealThreadCount() : 4;
}

int AppConfig::GetMemoryLimitMB() const { return memory_limit_mb_; }
int AppConfig::GetThreadCount() const { return thread_count_; }
bool AppConfig::GetAutoPerformance() const { return auto_performance_; }

void AppConfig::SetMemoryLimitMB(int mb) {
    memory_limit_mb_ = mb;
    auto_performance_ = false;
    SavePerformanceSettings();
}

void AppConfig::SetThreadCount(int count) {
    thread_count_ = count;
    auto_performance_ = false;
    SavePerformanceSettings();
}

void AppConfig::SetAutoPerformance(bool auto_perf) {
    auto_performance_ = auto_perf;
    if (auto_perf) {
        ApplyAutoPerformance();
    }
    SavePerformanceSettings();
}

void AppConfig::ApplyAutoPerformance() {
    int total_mem = GetTotalMemoryMB();
    memory_limit_mb_ = static_cast<int>(total_mem * 0.9);
    int cores = GetCpuCoreCount();
    thread_count_ = std::max(1, static_cast<int>(cores * 0.9));
}

void AppConfig::LoadPerformanceSettings() {
    auto_performance_ = settings_->value("performance/auto", true).toBool();
    if (auto_performance_) {
        ApplyAutoPerformance();
    } else {
        memory_limit_mb_ = settings_->value("performance/memory_limit_mb", 4096).toInt();
        thread_count_ = settings_->value("performance/thread_count", 4).toInt();
    }
}

void AppConfig::SavePerformanceSettings() {
    if (!settings_) return;
    settings_->setValue("performance/auto", auto_performance_);
    settings_->setValue("performance/memory_limit_mb", memory_limit_mb_);
    settings_->setValue("performance/thread_count", thread_count_);
    settings_->sync();
}

QMap<QString, QStringList> AppConfig::GetMappingKeywords() const {
    return custom_keywords_;
}

QMap<QString, QStringList> AppConfig::GetEffectiveMappingKeywords() const {
    auto result = DefaultMappingKeywords();
    for (auto it = custom_keywords_.begin(); it != custom_keywords_.end(); ++it) {
        result[it.key()].append(it.value());
        result[it.key()].removeDuplicates();
    }
    return result;
}

void AppConfig::SetMappingKeywords(const QMap<QString, QStringList>& map) {
    custom_keywords_ = map;
    SaveMappingKeywords();
}

void AppConfig::AddMappingKeyword(const QString& standard_col, const QString& keyword) {
    if (keyword.trimmed().isEmpty()) return;
    if (!custom_keywords_[standard_col].contains(keyword, Qt::CaseInsensitive)) {
        custom_keywords_[standard_col].append(keyword.trimmed());
        SaveMappingKeywords();
    }
}

void AppConfig::RemoveMappingKeyword(const QString& standard_col, const QString& keyword) {
    QStringList &list = custom_keywords_[standard_col];
    list.removeAll(keyword);
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).compare(keyword, Qt::CaseInsensitive) == 0) {
            list.removeAt(i);
        }
    }
    if (list.isEmpty()) custom_keywords_.remove(standard_col);
    SaveMappingKeywords();
}

void AppConfig::ResetMappingKeywords() {
    custom_keywords_.clear();
    SaveMappingKeywords();
}

void AppConfig::LoadMappingKeywords() {
    custom_keywords_.clear();
    if (!settings_) return;
    int size = settings_->beginReadArray("mapping_keywords");
    for (int i = 0; i < size; ++i) {
        settings_->setArrayIndex(i);
        QString std = settings_->value("standard").toString();
        QString kw = settings_->value("keyword").toString();
        if (!std.isEmpty() && !kw.isEmpty()) {
            custom_keywords_[std].append(kw);
        }
    }
    settings_->endArray();
}

void AppConfig::SaveMappingKeywords() {
    if (!settings_) return;
    settings_->beginWriteArray("mapping_keywords");
    int idx = 0;
    for (auto it = custom_keywords_.cbegin(); it != custom_keywords_.cend(); ++it) {
        const QStringList &kws = it.value();
        for (const QString &kw : kws) {
            settings_->setArrayIndex(idx++);
            settings_->setValue("standard", it.key());
            settings_->setValue("keyword", kw);
        }
    }
    settings_->endArray();
    settings_->sync();
}

} // namespace freight::core
