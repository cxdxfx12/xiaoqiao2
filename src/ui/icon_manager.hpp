#pragma once
#include <QIcon>
#include <QString>
#include <QMap>
#include <QPainter>
#include <QColor>

namespace freight::ui {

enum class IconSize {
    SIZE_16 = 16,
    SIZE_20 = 20,
    SIZE_24 = 24,
    SIZE_32 = 32,
    SIZE_48 = 48,
    SIZE_64 = 64,
};

enum class IconCategory {
    CARD,
    SETTING,
    ACTION,
    STATUS,
    LOGO,
};

class IconManager {
public:
    static IconManager& Instance();

    QIcon GetIcon(const QString &icon_name, IconCategory category, IconSize size);

    QIcon CardIcon(const QString &name) {
        return GetIcon(name, IconCategory::CARD, IconSize::SIZE_64);
    }
    QIcon SettingIcon(const QString &name) {
        return GetIcon(name, IconCategory::SETTING, IconSize::SIZE_24);
    }
    QIcon ActionIcon(const QString &name) {
        return GetIcon(name, IconCategory::ACTION, IconSize::SIZE_16);
    }
    QIcon StatusIcon(const QString &name) {
        return GetIcon(name, IconCategory::STATUS, IconSize::SIZE_24);
    }

private:
    IconManager();
    QIcon GenerateCardIcon(const QString &name, int size);
    QIcon GenerateSettingIcon(const QString &name, int size);
    QIcon GenerateActionIcon(const QString &name, int size);
    QIcon GenerateLogoIcon(int size);

    QMap<QString, QIcon> cache_;
};

} // namespace freight::ui
