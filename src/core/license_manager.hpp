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
    QString license_key;
    QString error_msg;

    bool IsExpired() const {
        if (!valid) return true;
        if (type == LicenseType::Permanent) return false;
        return QDateTime::currentDateTime() > expire_date;
    }

    int DaysRemaining() const {
        if (!valid) return 0;
        if (type == LicenseType::Permanent) return 9999;
        return QDateTime::currentDateTime().daysTo(expire_date);
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
    void UpdateTimeRecord();

    static QByteArray GetSecretKey();
    bool activated_ = false;
    LicenseInfo current_license_;
    QDateTime last_start_time_;
    bool time_tampered_ = false;

    static const QByteArray SECRET_KEY;
};

} // namespace freight::core
