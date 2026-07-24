#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QByteArray>

namespace freight::core {

enum class LicenseType {
    Trial = 0,
    Personal = 1,
    Enterprise = 2,
    Permanent = 3
};

struct LicenseInfo {
    bool valid = false;
    LicenseType type = LicenseType::Trial;
    QString machine_code;
    QDateTime issue_date;
    QDateTime expire_date;
    QDateTime adjusted_expire_override;  // 如果 isValid，优先用这个作为"真实过期时间"（时间篡改扣减后）
    QString license_key;
    QString error_msg;

    QDateTime EffectiveExpireDate() const {
        return adjusted_expire_override.isValid() ? adjusted_expire_override : expire_date;
    }

    bool IsExpired() const {
        if (!valid) return true;
        if (type == LicenseType::Permanent) return false;
        return QDateTime::currentDateTime() > EffectiveExpireDate();
    }

    int DaysRemaining() const {
        if (!valid) return -1;  // 无效 = -1（不是 0，防止 IsNearExpiry 误报"0 天到期"）
        if (type == LicenseType::Permanent) return 9999;
        return QDateTime::currentDateTime().daysTo(EffectiveExpireDate());
    }

    QString TypeString() const {
        switch (type) {
            case LicenseType::Trial: return "试用版";
            case LicenseType::Personal: return "个人版";
            case LicenseType::Enterprise: return "企业版";
            case LicenseType::Permanent: return "永久版";
            default: return "未知";
        }
    }
};

class LicenseManager : public QObject {
    Q_OBJECT

public:
    static LicenseManager& Instance();

    bool Init();
    LicenseInfo GetLicenseInfo();
    bool ActivateLicense(const QString &license_key);
    bool IsActivated() const { return activated_; }

    bool IsExpired() const;
    bool IsNearExpiry(int days = 7) const;
    bool IsFunctionAvailable() const;

    void CheckStartupReminder(bool &show_expired, bool &show_near_expiry,
                              int &remaining_days, QString &message);

    static QString GenerateMachineCode();
    static QString GenerateLicenseKey(const QString &machine_code,
                                      LicenseType type,
                                      const QDateTime &expire_date,
                                      const QByteArray &secret_key);

    static LicenseInfo VerifyLicenseKey(const QString &license_key,
                                        const QString &machine_code,
                                        const QByteArray &secret_key);

    static QByteArray DefaultSecretKey();

private:
    explicit LicenseManager(QObject *parent = nullptr);
    ~LicenseManager() override;

    bool LoadLicense();
    bool SaveLicense(const QString &license_key);
    QString GetLicenseFilePath() const;
    QString GetTimeRecordPath() const;

    bool CheckTimeTampering();
    void WriteTimeRecord(bool tampered);
    void WriteTimeRecord(bool tampered, qint64 total_run_secs, const QDateTime &adjusted_expire);
    void UpdateTimeRecord();
    bool LoadTimeRecord(QDateTime &out_last_start, qint64 &out_total_secs,
                        bool &out_tampered, QDateTime &out_adjusted_expire);
    bool IsLicenseTamperFlagPersisted();
    void PersistLicenseTamperFlag();
    void ClearPersistedLicenseTamperFlagForPerm();

    static QByteArray GetSecretKey();
    bool activated_ = false;
    LicenseInfo current_license_;
    QDateTime last_start_time_;
    bool time_tampered_ = false;
    qint64 total_run_secs_ = 0;
    // 永久版以外：如果时间被篡改，用这个『扣减后的过期时间』覆盖 license_key 内部的过期时间
    QDateTime adjusted_expire_override_;

    static const QByteArray SECRET_KEY;
};

} // namespace freight::core
