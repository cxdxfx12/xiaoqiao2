#include "ui/icon_manager.hpp"
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QLinearGradient>

namespace freight::ui {

IconManager& IconManager::Instance() {
    static IconManager instance;
    return instance;
}

IconManager::IconManager() = default;

QIcon IconManager::GetIcon(const QString &icon_name, IconCategory category, IconSize size) {
    QString key = QString("%1_%2_%3")
        .arg(static_cast<int>(category))
        .arg(icon_name)
        .arg(static_cast<int>(size));

    if (cache_.contains(key)) {
        return cache_[key];
    }

    QIcon icon;
    int px = static_cast<int>(size);

    switch (category) {
        case IconCategory::CARD:
            icon = GenerateCardIcon(icon_name, px);
            break;
        case IconCategory::SETTING:
            icon = GenerateSettingIcon(icon_name, px);
            break;
        case IconCategory::ACTION:
            icon = GenerateActionIcon(icon_name, px);
            break;
        case IconCategory::STATUS:
        case IconCategory::LOGO:
            icon = GenerateLogoIcon(px);
            break;
    }

    if (!icon.isNull()) {
        cache_[key] = icon;
    }
    return icon;
}

static QPixmap drawCardBg(int size, const QColor &c1, const QColor &c2) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0, c1);
    grad.setColorAt(1, c2);

    QPainterPath path;
    path.addRoundedRect(2, 2, size-4, size-4, size*0.18, size*0.18);
    p.fillPath(path, grad);

    p.end();
    return pix;
}

QIcon IconManager::GenerateCardIcon(const QString &name, int size) {
    QColor c1, c2;
    if (name == "calc_single") {
        c1 = QColor("#409eff"); c2 = QColor("#67c23a");
    } else if (name == "calc_batch") {
        c1 = QColor("#e6a23c"); c2 = QColor("#f56c6c");
    } else if (name == "compare") {
        c1 = QColor("#909399"); c2 = QColor("#6d63c4");
    } else if (name == "history") {
        c1 = QColor("#67c23a"); c2 = QColor("#409eff");
    } else if (name == "calc_detail" || name == "dashboard") {
        c1 = QColor("#8e44ad"); c2 = QColor("#f39c12");
    } else {
        c1 = QColor("#409eff"); c2 = QColor("#67c23a");
    }

    QPixmap pix = drawCardBg(size, c1, c2);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::white, size*0.05));
    p.setBrush(Qt::NoBrush);

    int cx = size / 2;
    int cy = size / 2;
    int s = size * 0.45;

    if (name == "calc_single") {
        p.setBrush(QColor(255,255,255,240));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(cx-s*0.7, cy-s*0.8, s*1.4, s*1.5, size*0.06, size*0.06);
        p.setPen(QPen(QColor("#409eff"), size*0.04));
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 3; i++) {
            int y = cy - s*0.5 + i*s*0.35;
            p.drawLine(cx-s*0.4, y, cx+s*0.4, y);
        }
        p.setPen(QPen(Qt::white, size*0.06));
        p.setBrush(QColor("#67c23a"));
        p.drawEllipse(cx+s*0.4, cy+s*0.4, s*0.4, s*0.4);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", size*0.25, QFont::Bold));
        p.drawText(cx+s*0.4, cy+s*0.4, s*0.4, s*0.4, Qt::AlignCenter, "¥");
    } else if (name == "calc_batch") {
        p.setBrush(QColor(255,255,255,230));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(cx-s*0.7, cy-s*0.7, s*1.4, s*1.2, size*0.05, size*0.05);
        p.setPen(QPen(QColor("#e6a23c"), size*0.03));
        for (int i = 0; i < 4; i++) {
            int y = cy - s*0.5 + i*s*0.3;
            p.drawLine(cx-s*0.5, y, cx+s*0.5, y);
        }
        p.setBrush(QColor("#f56c6c"));
        p.setPen(Qt::NoPen);
        QPolygonF bolt;
        bolt << QPointF(cx+s*0.1, cy-s*0.6)
             << QPointF(cx-s*0.1, cy)
             << QPointF(cx+s*0.05, cy)
             << QPointF(cx-s*0.1, cy+s*0.6)
             << QPointF(cx+s*0.2, cy+s*0.05)
             << QPointF(cx+s*0.05, cy+s*0.05)
             << QPointF(cx+s*0.1, cy-s*0.6);
        p.drawPolygon(bolt);
    } else if (name == "compare") {
        p.setBrush(QColor(255,255,255,230));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(cx-s*0.65, cy-s*0.7, s*0.55, s*1.3, size*0.04, size*0.04);
        p.drawRoundedRect(cx+s*0.1, cy-s*0.5, s*0.55, s*0.9, size*0.04, size*0.04);
        p.setBrush(QColor("#6d63c4"));
        p.drawRect(cx-s*0.55, cy+s*0.2, s*0.35, s*0.3);
        p.setBrush(QColor("#909399"));
        p.drawRect(cx-s*0.1, cy-s*0.1, s*0.35, s*0.35);
        p.setBrush(QColor("#409eff"));
        p.drawRect(cx+s*0.2, cy+s*0.1, s*0.35, s*0.4);
    } else if (name == "history") {
        p.setBrush(QColor(255,255,255,230));
        p.setPen(Qt::NoPen);
        p.drawEllipse(cx-s*0.6, cy-s*0.6, s*1.2, s*1.2);
        p.setPen(QPen(QColor("#67c23a"), size*0.05));
        p.setBrush(Qt::NoBrush);
        int r = s*0.5;
        p.drawArc(cx-r, cy-r, r*2, r*2, 90*16, -270*16);
        p.setPen(QPen(QColor("#67c23a"), size*0.05));
        p.drawLine(cx, cy, cx, cy-s*0.5);
        p.drawLine(cx, cy, cx+s*0.4, cy-s*0.1);
    } else if (name == "calc_detail" || name == "dashboard") {
        p.setBrush(QColor(255,255,255,240));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(cx-s*0.7, cy-s*0.7, s*1.4, s*1.3, size*0.05, size*0.05);
        p.setBrush(QColor("#8e44ad"));
        p.drawRoundedRect(cx-s*0.5, cy+s*0.1, s*0.22, s*0.45, size*0.03, size*0.03);
        p.setBrush(QColor("#409eff"));
        p.drawRoundedRect(cx-s*0.18, cy-s*0.2, s*0.22, s*0.75, size*0.03, size*0.03);
        p.setBrush(QColor("#f39c12"));
        p.drawRoundedRect(cx+s*0.14, cy-s*0.45, s*0.22, s*1.0, size*0.03, size*0.03);
        p.setPen(QPen(QColor("#e74c3c"), size*0.06));
        p.setBrush(Qt::NoBrush);
        QPolygonF line;
        line << QPointF(cx-s*0.55, cy-s*0.35)
             << QPointF(cx-s*0.25, cy-s*0.55)
             << QPointF(cx+s*0.05, cy-s*0.1)
             << QPointF(cx+s*0.35, cy-s*0.45)
             << QPointF(cx+s*0.55, cy-s*0.6);
        p.drawPolyline(line);
        p.setBrush(QColor("#e74c3c"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(cx+s*0.55-size*0.04, cy-s*0.6-size*0.04, size*0.08, size*0.08);
    }

    p.end();
    return QIcon(pix);
}

QIcon IconManager::GenerateSettingIcon(const QString &name, int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#606266"), size*0.06));
    p.setBrush(QColor("#606266"));

    int cx = size/2, cy = size/2;
    int s = size*0.4;

    if (name == "rule_setting") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#606266"));
        for (int i = 0; i < 3; i++) {
            int y = cy - s*0.5 + i*s*0.5;
            p.drawRoundedRect(cx-s*0.7, y, s*1.4, s*0.3, size*0.05, size*0.05);
        }
        p.setBrush(QColor("#409eff"));
        p.drawEllipse(cx+s*0.4, cy-s*0.35, s*0.3, s*0.3);
    } else if (name == "customer") {
        p.setBrush(QColor("#606266"));
        p.drawEllipse(cx, cy-s*0.3, s*0.4, s*0.4);
        p.drawChord(cx-s*0.6, cy, s*1.2, s*0.8, 0, -180*16);
        p.setBrush(QColor("#409eff"));
        p.drawEllipse(cx+s*0.3, cy+s*0.1, s*0.3, s*0.3);
    } else if (name == "system_setting") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#606266"));
        p.drawEllipse(cx-s*0.1, cy-s*0.7, s*0.2, s*0.2);
        p.drawEllipse(cx+s*0.6, cy-s*0.2, s*0.2, s*0.2);
        p.drawEllipse(cx-s*0.7, cy-s*0.2, s*0.2, s*0.2);
        p.drawEllipse(cx-s*0.5, cy+s*0.5, s*0.2, s*0.2);
        p.drawEllipse(cx+s*0.5, cy+s*0.5, s*0.2, s*0.2);
        p.drawEllipse(cx-s*0.25, cy-s*0.25, s*0.5, s*0.5);
        p.setBrush(QColor("#ffffff"));
        p.drawEllipse(cx-s*0.15, cy-s*0.15, s*0.3, s*0.3);
    } else if (name == "about") {
        p.setBrush(QColor("#606266"));
        p.drawEllipse(cx-s*0.5, cy-s*0.5, s, s);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", size*0.5, QFont::Bold));
        p.drawText(cx-s*0.5, cy-s*0.5, s, s, Qt::AlignCenter, "i");
    } else {
        p.setBrush(QColor("#606266"));
        p.drawRoundedRect(cx-s*0.5, cy-s*0.5, s, s, size*0.08, size*0.08);
    }

    p.end();
    return QIcon(pix);
}

QIcon IconManager::GenerateActionIcon(const QString &name, int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#606266"), size*0.08));
    p.setBrush(Qt::NoBrush);

    int cx = size/2, cy = size/2;
    int s = size*0.35;

    if (name == "import") {
        p.drawLine(cx, cy-s, cx, cy+s*0.5);
        p.drawLine(cx-s*0.6, cy, cx, cy-s);
        p.drawLine(cx+s*0.6, cy, cx, cy-s);
        p.drawLine(cx-s*0.8, cy+s*0.5, cx+s*0.8, cy+s*0.5);
    } else if (name == "export") {
        p.drawLine(cx, cy+s, cx, cy-s*0.5);
        p.drawLine(cx-s*0.6, cy, cx, cy+s);
        p.drawLine(cx+s*0.6, cy, cx, cy+s);
        p.drawLine(cx-s*0.8, cy-s*0.5, cx+s*0.8, cy-s*0.5);
    } else if (name == "calculate") {
        p.setBrush(QColor("#67c23a"));
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(cx-s*0.6, cy-s*0.7)
            << QPointF(cx+s*0.7, cy)
            << QPointF(cx-s*0.6, cy+s*0.7);
        p.drawPolygon(tri);
    } else if (name == "cancel") {
        p.setPen(QPen(QColor("#f56c6c"), size*0.08));
        p.drawLine(cx-s*0.5, cy-s*0.5, cx+s*0.5, cy+s*0.5);
        p.drawLine(cx+s*0.5, cy-s*0.5, cx-s*0.5, cy+s*0.5);
    } else if (name == "save") {
        p.setBrush(QColor("#606266"));
        p.drawRoundedRect(cx-s*0.6, cy-s*0.7, s*1.2, s*1.4, size*0.05, size*0.05);
        p.setBrush(QColor("#ffffff"));
        p.drawRoundedRect(cx-s*0.5, cy-s*0.5, s, s*0.8, size*0.03, size*0.03);
        p.setBrush(QColor("#606266"));
        p.drawRect(cx-s*0.2, cy-s*0.6, s*0.4, s*0.25);
    } else if (name == "add") {
        p.setPen(QPen(QColor("#67c23a"), size*0.1));
        p.drawLine(cx, cy-s, cx, cy+s);
        p.drawLine(cx-s, cy, cx+s, cy);
    } else if (name == "delete") {
        p.setPen(QPen(QColor("#f56c6c"), size*0.08));
        p.drawLine(cx-s*0.5, cy-s*0.3, cx+s*0.5, cy+s*0.5);
        p.drawLine(cx+s*0.5, cy-s*0.3, cx-s*0.5, cy+s*0.5);
        p.drawLine(cx-s*0.6, cy-s*0.3, cx+s*0.6, cy-s*0.3);
    } else if (name == "search") {
        p.setPen(QPen(QColor("#606266"), size*0.08));
        p.drawEllipse(cx-s*0.3, cy-s*0.5, s*0.6, s*0.6);
        p.drawLine(cx+s*0.2, cy+s*0.2, cx+s*0.6, cy+s*0.6);
    } else {
        p.drawRoundedRect(cx-s*0.5, cy-s*0.3, s, s*0.6, size*0.05, size*0.05);
    }

    p.end();
    return QIcon(pix);
}

QIcon IconManager::GenerateLogoIcon(int size) {
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0, QColor("#409eff"));
    grad.setColorAt(1, QColor("#67c23a"));

    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(2, 2, size-4, size-4);

    p.setPen(QPen(Qt::white, size*0.06));
    p.setFont(QFont("Arial", size*0.5, QFont::Bold));
    p.drawText(0, 0, size, size, Qt::AlignCenter, "小");

    p.end();
    return QIcon(pix);
}

} // namespace freight::ui
