#include "app_config.hpp"
#include <QStandardPaths>
#include <QDir>
#include <QThread>
#include <QSet>
#include <QFileInfo>
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

static QList<TemplateFingerprint> BuildBuiltinTemplates() {
    QList<TemplateFingerprint> list;
    auto markBuiltin = [](TemplateFingerprint &t) { t.is_builtin = true; t.enabled = true; return t; };

    {
        TemplateFingerprint t;
        t.template_id = "zto_standard";
        t.display_name = "中通快递-标准模板";
        t.courier_name = "中通";
        t.required_keywords = {"中通", "ZTO", "网点名称", "派件员"};
        t.column_mapping = {
            {"运单号", "order_id"}, {"订单号", "order_id"},
            {"目的省份", "dest_province"}, {"省份", "dest_province"},
            {"目的城市", "dest_city"}, {"城市", "dest_city"},
            {"实际重量", "weight"}, {"重量", "weight"}, {"实重", "weight"},
            {"体积重量", "vol_weight"}, {"体积重", "vol_weight"},
            {"客户编号", "customer_id"}, {"客户代码", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "yto_standard";
        t.display_name = "圆通速递-标准模板";
        t.courier_name = "圆通";
        t.required_keywords = {"圆通", "YTO", "业务员"};
        t.column_mapping = {
            {"运单号", "order_id"}, {"快递单号", "order_id"},
            {"收件省", "dest_province"}, {"目的地省", "dest_province"},
            {"收件市", "dest_city"}, {"目的地市", "dest_city"},
            {"重量", "weight"}, {"实际重量", "weight"},
            {"体积重", "vol_weight"}, {"材积", "vol_weight"},
            {"客户编号", "customer_id"}, {"客户", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "yd_standard";
        t.display_name = "韵达速递-标准模板";
        t.courier_name = "韵达";
        t.required_keywords = {"韵达", "YUNDA", "服务站"};
        t.column_mapping = {
            {"单号", "order_id"}, {"运单号", "order_id"},
            {"到达省份", "dest_province"}, {"省", "dest_province"},
            {"到达城市", "dest_city"}, {"市", "dest_city"},
            {"货物重量", "weight"}, {"重", "weight"},
            {"材积重", "vol_weight"}, {"抛重", "vol_weight"},
            {"网点编号", "customer_id"}, {"网点", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "sto_standard";
        t.display_name = "申通快递-标准模板";
        t.courier_name = "申通";
        t.required_keywords = {"申通", "STO", "派件网点"};
        t.column_mapping = {
            {"运单编号", "order_id"}, {"申通单号", "order_id"},
            {"寄达省份", "dest_province"}, {"收件省份", "dest_province"},
            {"寄达城市", "dest_city"}, {"收件城市", "dest_city"},
            {"结算重量", "weight"}, {"计费重量", "weight"},
            {"体积重量", "vol_weight"},
            {"客户名称", "customer_id"}, {"客户ID", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "jitu_standard";
        t.display_name = "极兔速递-标准模板";
        t.courier_name = "极兔";
        t.required_keywords = {"极兔", "J&T", "JT", "派件员编码"};
        t.column_mapping = {
            {"单号", "order_id"}, {"面单号", "order_id"},
            {"收货省", "dest_province"}, {"到件省", "dest_province"},
            {"收货市", "dest_city"}, {"到件市", "dest_city"},
            {"KG", "weight"}, {"kg", "weight"}, {"重量", "weight"},
            {"体积重", "vol_weight"},
            {"商家编号", "customer_id"}, {"商家", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "ems_standard";
        t.display_name = "邮政EMS-标准模板";
        t.courier_name = "邮政";
        t.required_keywords = {"邮政", "EMS", "邮区", "邮件号码"};
        t.column_mapping = {
            {"邮件号码", "order_id"}, {"邮件编号", "order_id"},
            {"省名", "dest_province"}, {"寄达省", "dest_province"},
            {"市名", "dest_city"}, {"寄达市", "dest_city"},
            {"重量", "weight"}, {"实际重量", "weight"},
            {"体积重量", "vol_weight"},
            {"客户编号", "customer_id"}, {"客户代码", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "sf_standard";
        t.display_name = "顺丰速运-标准模板";
        t.courier_name = "顺丰";
        t.required_keywords = {"顺丰", "SF", "顺丰速运", "收派员"};
        t.column_mapping = {
            {"顺丰单号", "order_id"}, {"运单号", "order_id"},
            {"目的地省", "dest_province"}, {"目的地省份", "dest_province"},
            {"目的地市", "dest_city"}, {"目的地城市", "dest_city"},
            {"计费重量", "weight"}, {"charge_weight", "weight"},
            {"体积重量", "vol_weight"}, {"volumetric", "vol_weight"},
            {"月结卡号", "customer_id"}, {"月结账号", "customer_id"}
        };
        list << markBuiltin(t);
    }
    {
        TemplateFingerprint t;
        t.template_id = "deppon_standard";
        t.display_name = "德邦快递-标准模板";
        t.courier_name = "德邦";
        t.required_keywords = {"德邦", "DEPPON", "接货仓", "外场"};
        t.column_mapping = {
            {"运单号", "order_id"}, {"德邦单号", "order_id"},
            {"目的省份", "dest_province"}, {"目的站省", "dest_province"},
            {"目的城市", "dest_city"}, {"目的站市", "dest_city"},
            {"毛重", "weight"}, {"实际重量", "weight"},
            {"材积", "vol_weight"}, {"材积重", "vol_weight"},
            {"客户代码", "customer_id"}, {"客户编号", "customer_id"}
        };
        list << markBuiltin(t);
    }
    return list;
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

const QList<TemplateFingerprint>& AppConfig::BuiltinTemplateFingerprints() {
    static const auto list = BuildBuiltinTemplates();
    return list;
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
    LoadRecentFiles();
    LoadFeeThresholds();
    LoadCustomTemplates();
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

// ========== S5 记住常用目录 ==========
QString AppConfig::GetLastInputDir() const {
    if (!last_input_dir_.isEmpty() && QDir(last_input_dir_).exists()) return last_input_dir_;
    return QDir::homePath();
}
void AppConfig::SetLastInputDir(const QString &dir) {
    last_input_dir_ = dir;
    if (settings_) {
        settings_->setValue("paths/last_input_dir", dir);
        settings_->sync();
    }
}
QString AppConfig::GetLastOutputDir() const {
    if (!last_output_dir_.isEmpty() && QDir(last_output_dir_).exists()) return last_output_dir_;
    return GetResultsDir();
}
void AppConfig::SetLastOutputDir(const QString &dir) {
    last_output_dir_ = dir;
    if (settings_) {
        settings_->setValue("paths/last_output_dir", dir);
        settings_->sync();
    }
}
QStringList AppConfig::GetRecentFiles() const {
    return recent_files_;
}
void AppConfig::AddRecentFile(const QString &file) {
    if (file.isEmpty()) return;
    recent_files_.removeAll(file);
    recent_files_.prepend(file);
    while (recent_files_.size() > 10) recent_files_.removeLast();
    SaveRecentFiles();
}
void AppConfig::ClearRecentFiles() {
    recent_files_.clear();
    SaveRecentFiles();
}
void AppConfig::LoadRecentFiles() {
    if (!settings_) return;
    last_input_dir_ = settings_->value("paths/last_input_dir", "").toString();
    last_output_dir_ = settings_->value("paths/last_output_dir", "").toString();
    int size = settings_->beginReadArray("recent_files");
    for (int i = 0; i < size; ++i) {
        settings_->setArrayIndex(i);
        QString f = settings_->value("file").toString();
        if (QFileInfo::exists(f)) recent_files_ << f;
    }
    settings_->endArray();
}
void AppConfig::SaveRecentFiles() {
    if (!settings_) return;
    settings_->beginWriteArray("recent_files");
    for (int i = 0; i < recent_files_.size(); ++i) {
        settings_->setArrayIndex(i);
        settings_->setValue("file", recent_files_.at(i));
    }
    settings_->endArray();
    settings_->sync();
}

// ========== S6 金额染色阈值 ==========
double AppConfig::GetFeeLowThreshold() const { return fee_low_threshold_; }
void AppConfig::SetFeeLowThreshold(double v) {
    fee_low_threshold_ = v;
    SaveFeeThresholds();
}
double AppConfig::GetFeeHighThreshold() const { return fee_high_threshold_; }
void AppConfig::SetFeeHighThreshold(double v) {
    fee_high_threshold_ = v;
    SaveFeeThresholds();
}
void AppConfig::LoadFeeThresholds() {
    if (!settings_) return;
    fee_low_threshold_ = settings_->value("fee_thresholds/low", 5.0).toDouble();
    fee_high_threshold_ = settings_->value("fee_thresholds/high", 20.0).toDouble();
}
void AppConfig::SaveFeeThresholds() {
    if (!settings_) return;
    settings_->setValue("fee_thresholds/low", fee_low_threshold_);
    settings_->setValue("fee_thresholds/high", fee_high_threshold_);
    settings_->sync();
}

// ========== 功能7 模板指纹库 ==========
QList<TemplateFingerprint> AppConfig::GetCustomTemplateFingerprints() const {
    return custom_templates_;
}
void AppConfig::AddCustomTemplateFingerprint(const TemplateFingerprint &fp) {
    for (int i = custom_templates_.size() - 1; i >= 0; --i) {
        if (custom_templates_[i].template_id == fp.template_id) {
            custom_templates_.removeAt(i);
        }
    }
    custom_templates_ << fp;
    SaveCustomTemplates();
}
void AppConfig::RemoveCustomTemplateFingerprint(const QString &template_id) {
    for (int i = custom_templates_.size() - 1; i >= 0; --i) {
        if (custom_templates_[i].template_id == template_id) {
            custom_templates_.removeAt(i);
        }
    }
    disabled_template_ids_.remove(template_id);
    SaveCustomTemplates();
}
void AppConfig::UpdateCustomTemplateFingerprint(const TemplateFingerprint &fp) {
    for (int i = 0; i < custom_templates_.size(); ++i) {
        if (custom_templates_[i].template_id == fp.template_id) {
            custom_templates_[i] = fp;
            SaveCustomTemplates();
            return;
        }
    }
    AddCustomTemplateFingerprint(fp);
}
QList<TemplateFingerprint> AppConfig::GetAllTemplateFingerprints(bool enabled_only) const {
    QList<TemplateFingerprint> result = BuiltinTemplateFingerprints();
    for (auto &t : result) {
        t.enabled = !disabled_template_ids_.contains(t.template_id);
    }
    for (auto t : custom_templates_) {
        t.enabled = !disabled_template_ids_.contains(t.template_id);
        result.append(t);
    }
    if (enabled_only) {
        QList<TemplateFingerprint> filtered;
        for (const auto &t : result) if (t.enabled) filtered << t;
        return filtered;
    }
    return result;
}
bool AppConfig::IsTemplateEnabled(const QString &template_id) const {
    return !disabled_template_ids_.contains(template_id);
}
void AppConfig::SetTemplateEnabled(const QString &template_id, bool enabled) {
    if (enabled) disabled_template_ids_.remove(template_id);
    else disabled_template_ids_.insert(template_id);
    SaveCustomTemplates();
}
bool AppConfig::GetTemplateAutoDetectGlobal() const {
    return template_auto_detect_global_;
}
void AppConfig::SetTemplateAutoDetectGlobal(bool enabled) {
    template_auto_detect_global_ = enabled;
    SaveCustomTemplates();
}
void AppConfig::LoadCustomTemplates() {
    if (!settings_) return;
    custom_templates_.clear();
    disabled_template_ids_.clear();
    template_auto_detect_global_ = settings_->value("templates/auto_detect_global", true).toBool();
    QStringList disabled = settings_->value("templates/disabled_ids", QStringList()).toStringList();
    for (const auto &id : disabled) disabled_template_ids_.insert(id);

    int size = settings_->beginReadArray("custom_templates");
    for (int i = 0; i < size; ++i) {
        settings_->setArrayIndex(i);
        TemplateFingerprint fp;
        fp.template_id = settings_->value("template_id").toString();
        fp.display_name = settings_->value("display_name").toString();
        fp.courier_name = settings_->value("courier_name").toString();
        fp.required_keywords = settings_->value("keywords").toStringList();
        fp.enabled = settings_->value("enabled", true).toBool();
        fp.is_builtin = false;
        QString mapping_str = settings_->value("mapping").toString();
        QStringList pairs = mapping_str.split("||", Qt::SkipEmptyParts);
        for (const auto &p : pairs) {
            QStringList kv = p.split("=>", Qt::SkipEmptyParts);
            if (kv.size() == 2) fp.column_mapping[kv[0]] = kv[1];
        }
        if (!fp.template_id.isEmpty()) custom_templates_ << fp;
    }
    settings_->endArray();
}
void AppConfig::SaveCustomTemplates() {
    if (!settings_) return;
    QStringList disabled;
    for (const auto &id : disabled_template_ids_) disabled << id;
    settings_->setValue("templates/auto_detect_global", template_auto_detect_global_);
    settings_->setValue("templates/disabled_ids", disabled);

    settings_->beginWriteArray("custom_templates");
    for (int i = 0; i < custom_templates_.size(); ++i) {
        settings_->setArrayIndex(i);
        const auto &fp = custom_templates_[i];
        settings_->setValue("template_id", fp.template_id);
        settings_->setValue("display_name", fp.display_name);
        settings_->setValue("courier_name", fp.courier_name);
        settings_->setValue("keywords", fp.required_keywords);
        settings_->setValue("enabled", fp.enabled);
        QStringList pairs;
        for (auto it = fp.column_mapping.begin(); it != fp.column_mapping.end(); ++it) {
            pairs << it.key() + "=>" + it.value();
        }
        settings_->setValue("mapping", pairs.join("||"));
    }
    settings_->endArray();
    settings_->sync();
}

} // namespace freight::core
