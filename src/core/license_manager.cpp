#include "core/license_manager.hpp"
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QRandomGenerator>
#include <algorithm>

namespace freight::core {

namespace {
// 密钥拆 5 段拼接（原始：XiaoQiaoFreight2026@#$SecretKey888）
// 这样二进制 .rodata 里不会出现完整连续的密钥，增加逆向难度。
constexpr const char kSeg1[] = "XiaoQiao";
constexpr const char kSeg2[] = "Freight2026";
constexpr const char kSeg3[] = "@#$";
constexpr const char kSeg4[] = "SecretKey";
constexpr const char kSeg5[] = "888";
} // namespace

const QByteArray LicenseManager::SECRET_KEY = "";  // 占位，实际通过 GetSecretKey() 还原

LicenseManager& LicenseManager::Instance() {
    static LicenseManager instance;
    return instance;
}

LicenseManager::LicenseManager(QObject *parent) : QObject(parent) {}

LicenseManager::~LicenseManager() = default;

QByteArray LicenseManager::GetSecretKey() {
    QByteArray out;
    out.reserve(int(sizeof(kSeg1) + sizeof(kSeg2) + sizeof(kSeg3) + sizeof(kSeg4) + sizeof(kSeg5) - 5));
    out.append(kSeg1, int(sizeof(kSeg1) - 1));
    out.append(kSeg2, int(sizeof(kSeg2) - 1));
    out.append(kSeg3, int(sizeof(kSeg3) - 1));
    out.append(kSeg4, int(sizeof(kSeg4) - 1));
    out.append(kSeg5, int(sizeof(kSeg5) - 1));
    return out;
}

QByteArray LicenseManager::DefaultSecretKey() {
    return GetSecretKey();
}

QString LicenseManager::GenerateMachineCode() {
    QString raw;

    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    // Ethernet 优先，Wifi 次之。稳定排序，避免 OS 枚举顺序随机导致机器码跳动
    QList<QNetworkInterface> eth_list, wifi_list;
    for (const auto &iface : ifaces) {
        if (!iface.isValid() || iface.hardwareAddress().isEmpty()) continue;
        // 跳过明显的虚拟/loopback/点对点拨号
        if (iface.type() == QNetworkInterface::Loopback
            || iface.type() == QNetworkInterface::Virtual
            || iface.type() == QNetworkInterface::Ppp
            || iface.type() == QNetworkInterface::Slip) continue;
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        QString hw = iface.hardwareAddress();
        if (hw.isEmpty() || hw == "00:00:00:00:00:00") continue;
        if (iface.type() == QNetworkInterface::Ethernet) eth_list << iface;
        else if (iface.type() == QNetworkInterface::Wifi) wifi_list << iface;
    }
    // 先按名称排序，取第一个 Ethernet；没有就取第一个 Wifi；再没有就回退兜底串
    auto pickFirstStable = [](QList<QNetworkInterface> &lst) -> QString {
        if (lst.isEmpty()) return QString();
        std::sort(lst.begin(), lst.end(), [](const QNetworkInterface &a, const QNetworkInterface &b) {
            return a.humanReadableName() < b.humanReadableName();
        });
        return lst.first().hardwareAddress();
    };
    raw = pickFirstStable(eth_list);
    if (raw.isEmpty()) raw = pickFirstStable(wifi_list);

    if (raw.isEmpty()) {
        raw = "XIAOQIAO_DEFAULT_MACHINE";
    }

    QString machine_id = QString::fromStdString(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex().toStdString()
    ).toUpper();

    QString formatted;
    for (int i = 0; i < machine_id.length(); i += 4) {
        if (i > 0) formatted += "-";
        formatted += machine_id.mid(i, 4);
    }
    return formatted;
}

QString LicenseManager::GenerateLicenseKey(const QString &machine_code,
                                             LicenseType type,
                                             const QDateTime &expire_date,
                                             const QByteArray &secret_key) {
    QString clean_machine = machine_code;
    clean_machine.remove("-");

    QJsonObject payload;
    payload["mc"] = clean_machine;
    payload["type"] = static_cast<int>(type);
    payload["exp"] = expire_date.toString("yyyyMMddHHmmss");

    QJsonDocument doc(payload);
    QByteArray payload_bytes = doc.toJson(QJsonDocument::Compact);

    QByteArray hmac = QMessageAuthenticationCode::hash(
        payload_bytes, secret_key, QCryptographicHash::Sha256
    ).toHex().toUpper();

    QByteArray combined = payload_bytes.toBase64() + "." + hmac;

    QString license = QString::fromLatin1(combined);

    QString formatted;
    for (int i = 0; i < license.length(); i += 6) {
        if (i > 0) formatted += "-";
        formatted += license.mid(i, 6);
    }

    return formatted;
}

LicenseInfo LicenseManager::VerifyLicenseKey(const QString &license_key,
                                            const QString &machine_code,
                                            const QByteArray &secret_key) {
    LicenseInfo result;
    result.license_key = license_key;

    QString clean_key = license_key;
    clean_key.remove("-");

    QStringList parts = clean_key.split(".");
    if (parts.size() != 2) {
        result.error_msg = "授权码格式错误";
        return result;
    }

    QByteArray payload_bytes = QByteArray::fromBase64(parts[0].toLatin1());
    QByteArray received_hmac = parts[1].toLatin1();

    QByteArray expected_hmac = QMessageAuthenticationCode::hash(
        payload_bytes, secret_key, QCryptographicHash::Sha256
    ).toHex().toUpper();

    if (received_hmac != expected_hmac) {
        result.error_msg = "授权码验证失败";
        return result;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload_bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error_msg = "授权码解析失败";
        return result;
    }

    QJsonObject obj = doc.object();

    QString clean_machine = machine_code;
    clean_machine.remove("-");

    if (obj["mc"].toString() != clean_machine) {
        result.error_msg = "机器码不匹配";
        return result;
    }

    result.machine_code = machine_code;
    result.type = static_cast<LicenseType>(obj["type"].toInt(0));
    result.expire_date = QDateTime::fromString(obj["exp"].toString(), "yyyyMMddHHmmss");
    result.valid = true;
    result.error_msg.clear();

    return result;
}

bool LicenseManager::Init() {
    // 1. 先加载/创建试用授权（防止试用无限续期）
    if (!LoadLicense()) {
        qWarning() << "License load failed, using trial mode";
    }
    // 2. 读取 time_record.dat（含 tamper 标记 + adjusted_expire_override）
    QDateTime out_last;
    qint64 out_secs = 0;
    bool out_tamper = false;
    QDateTime out_adj_exp;
    if (LoadTimeRecord(out_last, out_secs, out_tamper, out_adj_exp)) {
        last_start_time_ = out_last;
        total_run_secs_ = out_secs;
        if (out_adj_exp.isValid()) adjusted_expire_override_ = out_adj_exp;
    }
    // 3. 把 adjusted_expire_override 应用到 current_license_
    if (adjusted_expire_override_.isValid() && current_license_.type != LicenseType::Permanent) {
        current_license_.adjusted_expire_override = adjusted_expire_override_;
    }
    // 4. 检查 license.dat 尾部的 TAMPER 持久化标记
    if (IsLicenseTamperFlagPersisted() && current_license_.type != LicenseType::Permanent) {
        out_tamper = true;
        time_tampered_ = true;
    }
    // 5. 时间篡改检测（会更新 adjusted_expire_override_ / time_tampered_）
    CheckTimeTampering();
    // 6. 更新时间记录文件
    UpdateTimeRecord();
    return true;
}

LicenseInfo LicenseManager::GetLicenseInfo() {
    if (adjusted_expire_override_.isValid()
        && current_license_.valid
        && current_license_.type != LicenseType::Permanent) {
        current_license_.adjusted_expire_override = adjusted_expire_override_;
    }
    return current_license_;
}

bool LicenseManager::ActivateLicense(const QString &license_key) {
    QString machine_code = GenerateMachineCode();
    LicenseInfo info = VerifyLicenseKey(license_key, machine_code, GetSecretKey());

    if (!info.valid) {
        current_license_ = info;
        activated_ = false;
        return false;
    }

    // 激活：重置时间篡改记录（之前的试用版篡改痕迹如果是永久版就清掉）
    if (info.type == LicenseType::Permanent) {
        time_tampered_ = false;
        adjusted_expire_override_ = QDateTime();
        current_license_.adjusted_expire_override = QDateTime();
        ClearPersistedLicenseTamperFlagForPerm();
    } else {
        // 付费版有过期日：如果 time_rec 里之前有篡改扣减的 adjusted_expire 且比新授权 expire 更早，保留扣减
        if (adjusted_expire_override_.isValid() && adjusted_expire_override_ < info.expire_date) {
            info.adjusted_expire_override = adjusted_expire_override_;
        }
    }

    if (info.IsExpired()) {
        info.valid = false;
        info.error_msg = "授权已过期";
        current_license_ = info;
        activated_ = false;
        return false;
    }

    current_license_ = info;
    activated_ = true;

    SaveLicense(license_key);
    return true;
}

bool LicenseManager::LoadLicense() {
    QString file_path = GetLicenseFilePath();
    QFile file(file_path);

    if (!file.exists()) {
        // ========= Bug1 Fix: 试用版也要落盘，避免每次启动都是新的 30 天 =========
        QString mc = GenerateMachineCode();
        QDateTime issue = QDateTime::currentDateTime();
        QDateTime expire = issue.addDays(30);
        QString trial_license_key = GenerateLicenseKey(mc, LicenseType::Trial, expire, GetSecretKey());

        LicenseInfo info;
        info.valid = true;
        info.type = LicenseType::Trial;
        info.machine_code = mc;
        info.issue_date = issue;
        info.expire_date = expire;
        info.license_key = trial_license_key;
        info.error_msg.clear();
        current_license_ = info;
        activated_ = false;

        SaveLicense(trial_license_key);
        qDebug() << "Trial license created and persisted, expire:" << expire.toString();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        current_license_.error_msg = "无法读取授权文件";
        return false;
    }

    QString raw = QString::fromUtf8(file.readAll()).trimmed();
    file.close();

    // license.dat 可能附加多行：第 1 行 = 授权码；后续行是持久化标记（如 TAMPER=1）
    QStringList lines = raw.split('\n', Qt::SkipEmptyParts);
    QString license_key;
    if (!lines.isEmpty()) license_key = lines.first().trimmed();

    QString machine_code = GenerateMachineCode();
    LicenseInfo info = VerifyLicenseKey(license_key, machine_code, GetSecretKey());

    if (info.valid && !info.IsExpired()) {
        current_license_ = info;
        activated_ = (info.type != LicenseType::Trial);  // Trial = 未购买激活
        return true;
    }

    // 即使 key 无效/过期，也写入 current_license_（方便显示错误）
    current_license_ = info;
    activated_ = false;
    return false;
}

bool LicenseManager::SaveLicense(const QString &license_key) {
    QString file_path = GetLicenseFilePath();
    QFileInfo fi(file_path);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 保留原来的 TAMPER 标记行（除非是永久版 ClearPersistedLicenseTamperFlagForPerm 已经覆盖）
    QFile file(file_path);
    QString tamper_line;
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString raw = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        QStringList lines = raw.split('\n', Qt::SkipEmptyParts);
        for (int i = 1; i < lines.size(); i++) {
            if (lines[i].startsWith("TAMPER=")) {
                tamper_line = lines[i];
                break;
            }
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(license_key.toUtf8());
    file.write("\n", 1);
    if (!tamper_line.isEmpty()) {
        file.write(tamper_line.toUtf8());
        file.write("\n", 1);
    }
    file.close();
    return true;
}

bool LicenseManager::IsLicenseTamperFlagPersisted() {
    QString file_path = GetLicenseFilePath();
    QFile file(file_path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString raw = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    return raw.contains("\nTAMPER=1") || raw.endsWith("TAMPER=1");
}

void LicenseManager::PersistLicenseTamperFlag() {
    QString file_path = GetLicenseFilePath();
    if (IsLicenseTamperFlagPersisted()) return;
    QFile file(file_path);
    if (!file.open(QIODevice::Append | QIODevice::Text)) return;
    file.write("\nTAMPER=1", 9);
    file.close();
}

void LicenseManager::ClearPersistedLicenseTamperFlagForPerm() {
    QString file_path = GetLicenseFilePath();
    QFile file(file_path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString raw = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    QStringList lines = raw.split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) return;
    QString cleaned = lines.first();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    file.write(cleaned.toUtf8());
    file.write("\n", 1);
    file.close();
}

QString LicenseManager::GetLicenseFilePath() const {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_dir);
    return data_dir + "/license.dat";
}

QString LicenseManager::GetTimeRecordPath() const {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_dir);
    return data_dir + "/time_rec.dat";
}

bool LicenseManager::IsExpired() const {
    return current_license_.IsExpired();
}

bool LicenseManager::IsNearExpiry(int days) const {
    if (current_license_.type == LicenseType::Permanent) return false;
    if (!current_license_.valid) return false;
    int remaining = current_license_.DaysRemaining();
    return remaining >= 0 && remaining <= days;
}

bool LicenseManager::IsFunctionAvailable() const {
    if (current_license_.type == LicenseType::Permanent) return true;
    return !current_license_.IsExpired();
}

void LicenseManager::CheckStartupReminder(bool &show_expired, bool &show_near_expiry,
                                          int &remaining_days, QString &message) {
    show_expired = false;
    show_near_expiry = false;
    remaining_days = current_license_.DaysRemaining();
    if (remaining_days < 0) remaining_days = 0;

    if (time_tampered_) {
        show_expired = true;
        message = "检测到系统时间被篡改，试用已过期。\n请购买正版授权后继续使用。";
        return;
    }

    if (current_license_.IsExpired()) {
        show_expired = true;
        if (activated_) {
            message = "您的授权已过期！\n请联系客服续费后继续使用。\n\n客服热线：17771300068";
        } else {
            message = "试用期已结束！\n请购买正版授权后继续使用。\n\n客服热线：17771300068";
        }
        return;
    }

    if (IsNearExpiry(7)) {
        show_near_expiry = true;
        if (activated_) {
            message = QString("您的授权将于 %1 天后到期。\n请及时联系客服续费，避免影响使用。\n\n客服热线：17771300068")
                .arg(remaining_days);
        } else {
            message = QString("试用期还剩 %1 天。\n试用期结束后将无法使用批量计算功能。\n\n购买正版授权请联系：17771300068")
                .arg(remaining_days);
        }
    }
}

bool LicenseManager::LoadTimeRecord(QDateTime &out_last_start, qint64 &out_total_secs,
                                    bool &out_tampered, QDateTime &out_adjusted_expire) {
    QString file_path = GetTimeRecordPath();
    QFile file(file_path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    QString data = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    QStringList parts = data.split("|");
    if (parts.size() < 2) return false;

    out_last_start = QDateTime::fromString(parts[0], "yyyyMMddHHmmss");
    out_total_secs = parts[1].toLongLong();
    out_tampered = false;
    out_adjusted_expire = QDateTime();
    if (parts.size() >= 3) {
        out_tampered = (parts[2] == "1");
    }
    if (parts.size() >= 4 && !parts[3].trimmed().isEmpty()) {
        out_adjusted_expire = QDateTime::fromString(parts[3], "yyyyMMddHHmmss");
    }
    return out_last_start.isValid();
}

bool LicenseManager::CheckTimeTampering() {
    QDateTime current = QDateTime::currentDateTime();
    bool stored_tampered = false;
    QDateTime stored_adj_exp;
    qint64 stored_secs = 0;
    QDateTime stored_last_start;

    if (last_start_time_.isNull()) {
        // 没有历史记录：设为现在
        last_start_time_ = current;
        time_tampered_ = false;
        return false;
    }
    stored_last_start = last_start_time_;

    if (current < stored_last_start) {
        // 永久版不触发时间篡改失效
        if (current_license_.type == LicenseType::Permanent) {
            last_start_time_ = current;
            time_tampered_ = false;
            return false;
        }

        time_tampered_ = true;
        qint64 back_seconds = stored_last_start.secsTo(current) * -1;
        int back_days = static_cast<int>(back_seconds / 86400) + 1;

        QDateTime base_expire = current_license_.expire_date;
        if (adjusted_expire_override_.isValid()) base_expire = adjusted_expire_override_;
        QDateTime new_expire = base_expire.addDays(-back_days);
        if (new_expire < current) {
            new_expire = current.addDays(-1);
        }
        adjusted_expire_override_ = new_expire;
        current_license_.adjusted_expire_override = new_expire;

        qWarning() << "Time tampering detected! Back by" << back_days << "days";
        qWarning() << "Adjusted expire date to:" << new_expire.toString();

        // 把 tampered=1 同时写入 license.dat（TAMPER=1 标记行）和 time_record.dat（双保险）
        PersistLicenseTamperFlag();
        WriteTimeRecord(true, total_run_secs_, new_expire);
        return true;
    }

    // 如果历史任一文件有篡改标记且非永久版，持续保留篡改状态
    bool license_tamper_flag = (current_license_.type != LicenseType::Permanent) && IsLicenseTamperFlagPersisted();
    if (license_tamper_flag) {
        time_tampered_ = true;
        // 如果之前已经有 adjusted_expire 记录但这次 current>=last_start，也要继续用覆盖
        return true;
    }
    // 读取到的 out_tamper=true 且非永久版
    if (time_tampered_ && current_license_.type != LicenseType::Permanent) {
        return true;
    }

    // 正常：累加累计运行秒（粗略：本次启动到 now 的秒数，这里简化处理）
    qint64 delta = stored_last_start.secsTo(current);
    if (delta > 0) total_run_secs_ = stored_secs + delta;
    time_tampered_ = false;
    return false;
}

void LicenseManager::WriteTimeRecord(bool tampered) {
    WriteTimeRecord(tampered, total_run_secs_, adjusted_expire_override_);
}

void LicenseManager::WriteTimeRecord(bool tampered, qint64 total_run_secs, const QDateTime &adjusted_expire) {
    QString file_path = GetTimeRecordPath();
    QFileInfo fi(file_path);
    QDir dir = fi.dir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    QDateTime current = QDateTime::currentDateTime();
    QString record = current.toString("yyyyMMddHHmmss") + "|"
                   + QString::number(total_run_secs) + "|"
                   + QString(tampered ? "1" : "0") + "|"
                   + (adjusted_expire.isValid() ? adjusted_expire.toString("yyyyMMddHHmmss") : QString());
    file.write(record.toUtf8());
    file.close();
}

void LicenseManager::UpdateTimeRecord() {
    WriteTimeRecord(time_tampered_);
}

} // namespace freight::core
