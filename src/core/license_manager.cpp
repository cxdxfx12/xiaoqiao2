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
    for (const auto &iface : ifaces) {
        if (iface.isValid() && !iface.hardwareAddress().isEmpty()
            && (iface.type() == QNetworkInterface::Ethernet
                || iface.type() == QNetworkInterface::Wifi)) {
            raw += iface.hardwareAddress();
        }
    }

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
    if (!LoadLicense()) {
        qWarning() << "License load failed, using trial mode";
    }
    CheckTimeTampering();
    UpdateTimeRecord();
    return true;
}

LicenseInfo LicenseManager::GetLicenseInfo() {
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
        current_license_.type = LicenseType::Trial;
        current_license_.valid = true;
        current_license_.machine_code = GenerateMachineCode();
        current_license_.issue_date = QDateTime::currentDateTime();
        current_license_.expire_date = QDateTime::currentDateTime().addDays(30);
        current_license_.error_msg.clear();
        activated_ = false;
        return true;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        current_license_.error_msg = "无法读取授权文件";
        return false;
    }

    QString license_key = QString::fromUtf8(file.readAll()).trimmed();
    file.close();

    QString machine_code = GenerateMachineCode();
    LicenseInfo info = VerifyLicenseKey(license_key, machine_code, GetSecretKey());

    if (info.valid && !info.IsExpired()) {
        current_license_ = info;
        activated_ = true;
        return true;
    }

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

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    file.write(license_key.toUtf8());
    file.close();
    return true;
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
    if (!current_license_.valid) return true;
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

bool LicenseManager::CheckTimeTampering() {
    QString file_path = GetTimeRecordPath();
    QFile file(file_path);

    QDateTime current = QDateTime::currentDateTime();
    bool file_has_tamper_flag = false;
    bool stored_tampered = false;
    qint64 total_run_secs = 0;

    if (!file.exists()) {
        last_start_time_ = current;
        time_tampered_ = false;
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        time_tampered_ = false;
        return false;
    }

    QString data = QString::fromUtf8(file.readAll()).trimmed();
    file.close();

    QStringList parts = data.split("|");
    if (parts.size() < 2) {
        time_tampered_ = false;
        last_start_time_ = current;
        return false;
    }

    last_start_time_ = QDateTime::fromString(parts[0], "yyyyMMddHHmmss");
    total_run_secs = parts[1].toLongLong();
    if (parts.size() >= 3) {
        file_has_tamper_flag = true;
        stored_tampered = (parts[2] == "1");
    }

    if (!last_start_time_.isValid()) {
        time_tampered_ = false;
        last_start_time_ = current;
        return false;
    }

    if (current < last_start_time_) {
        // 永久版不触发时间篡改失效逻辑（机器码绑定的永久授权，无需靠时间限制）
        if (current_license_.type == LicenseType::Permanent) {
            last_start_time_ = current;
            time_tampered_ = false;
            return false;
        }

        time_tampered_ = true;
        qint64 back_seconds = last_start_time_.secsTo(current) * -1;
        int back_days = static_cast<int>(back_seconds / 86400) + 1;

        current_license_.expire_date = current_license_.expire_date.addDays(-back_days);
        if (current_license_.expire_date < current) {
            current_license_.expire_date = current.addDays(-1);
        }

        qWarning() << "Time tampering detected! Back by" << back_days << "days";
        qWarning() << "Adjusted expire date to:" << current_license_.expire_date.toString();

        // 已激活用户：把调整后的授权写回 license.dat（持久化），避免重启后恢复
        if (activated_) {
            SaveLicense(current_license_.license_key);
        }
        // 把 tampered 状态写入 time_record.dat，下次启动即使调回时间也继续保留篡改标记
        WriteTimeRecord(true);
        return true;
    }

    // 如果历史记录里已经被标记篡改，且非永久版，则持续保持时间篡改状态（防止用户"改回到之前时间"逃避）
    if (file_has_tamper_flag && stored_tampered && current_license_.type != LicenseType::Permanent) {
        time_tampered_ = true;
        return true;
    }

    time_tampered_ = false;
    return false;
}

void LicenseManager::WriteTimeRecord(bool tampered) {
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
    QString record = current.toString("yyyyMMddHHmmss") + "|0|" + QString(tampered ? "1" : "0");
    file.write(record.toUtf8());
    file.close();
}

void LicenseManager::UpdateTimeRecord() {
    WriteTimeRecord(time_tampered_);
}

} // namespace freight::core
