#include "ui/dialogs/header_mapping_dialog.hpp"
#include "ui/dialogs/rule_setting_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "services/calc_service.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QCheckBox>
#include <cmath>

namespace freight::ui::dialogs {

// ============================================================
// MappingView — 自定义绘制红色箭头连线
// ============================================================
class MappingView : public QWidget {
public:
    MappingView(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setMouseTracking(true);
    }

    void SetData(const QStringList& imported, const QStringList& standard,
                 const QStringList& required, const QMap<QString, QString>& mapping) {
        imported_ = imported;
        standard_ = standard;
        required_ = required;
        mapping_ = mapping;
        selected_imported_ = -1;
        hovered_line_ = -1;
        UpdateGeometry();
        update();
    }

    QMap<QString, QString> GetMapping() const { return mapping_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 背景
        p.fillRect(rect(), QColor(250, 252, 255));

        int left_x = PADDING;
        int right_x = width() - PADDING - COL_WIDTH;
        int mid_x = (left_x + COL_WIDTH + right_x) / 2;

        // 标题
        p.setFont(QFont(QString::fromUtf8("PingFang SC"), 12, QFont::DemiBold));
        p.setPen(QColor(96, 98, 102));
        p.drawText(QRect(left_x, 6, COL_WIDTH, 24), Qt::AlignCenter, QString::fromUtf8("导入文件表头"));
        p.drawText(QRect(right_x, 6, COL_WIDTH, 24), Qt::AlignCenter, QString::fromUtf8("标准列（中文）"));

        // 绘制连线
        for (auto it = mapping_.begin(); it != mapping_.end(); ++it) {
            int s_idx = standard_.indexOf(it.key());
            int i_idx = imported_.indexOf(it.value());
            if (s_idx < 0 || i_idx < 0) continue;

            int y1 = ItemY(i_idx) + ITEM_HEIGHT / 2;
            int y2 = ItemY(s_idx) + ITEM_HEIGHT / 2;
            int x1 = left_x + COL_WIDTH;
            int x2 = right_x;

            bool is_hovered = (hovered_line_ == s_idx);
            QColor line_color = is_hovered ? QColor(245, 108, 108) : QColor(235, 80, 80);
            p.setPen(QPen(line_color, is_hovered ? 2.5 : 2.0, Qt::DashLine));

            // 贝塞尔曲线
            QPainterPath path;
            path.moveTo(x1, y1);
            int ctrl_x = (x1 + x2) / 2;
            path.cubicTo(ctrl_x, y1, ctrl_x, y2, x2, y2);
            p.drawPath(path);

            // 箭头
            double angle = std::atan2(y2 - y1, x2 - x1);
            double arrow_len = 10;
            double arrow_angle = M_PI / 6;
            p.setPen(Qt::NoPen);
            p.setBrush(line_color);
            QPolygonF arrow;
            arrow << QPointF(x2, y2)
                  << QPointF(x2 - arrow_len * std::cos(angle - arrow_angle),
                            y2 - arrow_len * std::sin(angle - arrow_angle))
                  << QPointF(x2 - arrow_len * std::cos(angle + arrow_angle),
                            y2 - arrow_len * std::sin(angle + arrow_angle));
            p.drawPolygon(arrow);
        }

        // 绘制左侧条目
        p.setFont(QFont(QString::fromUtf8("PingFang SC"), 11));
        for (int i = 0; i < imported_.size(); i++) {
            QRect r(left_x, ItemY(i), COL_WIDTH, ITEM_HEIGHT);
            bool selected = (selected_imported_ == i);
            bool mapped = mapping_.values().contains(imported_[i]);

            QColor bg = mapped ? QColor(236, 245, 255) : QColor(255, 255, 255);
            if (selected) bg = QColor(64, 158, 255);
            QColor border = selected ? QColor(64, 158, 255) :
                             mapped ? QColor(179, 216, 255) : QColor(220, 223, 230);
            int radius = 8;

            p.setPen(QPen(border, 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r, radius, radius);

            p.setPen(selected ? Qt::white : QColor(48, 49, 51));
            p.drawText(r.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, imported_[i]);
        }

        // 绘制右侧条目
        for (int i = 0; i < standard_.size(); i++) {
            QRect r(right_x, ItemY(i), COL_WIDTH, ITEM_HEIGHT);
            bool is_required = required_.contains(standard_[i]);
            bool mapped = mapping_.contains(standard_[i]);

            QColor bg = mapped ? QColor(236, 245, 255) : QColor(255, 255, 255);
            if (is_required && !mapped) bg = QColor(254, 240, 240);
            QColor border = (is_required && !mapped) ? QColor(245, 108, 108) :
                             mapped ? QColor(179, 216, 255) : QColor(220, 223, 230);
            int radius = 8;

            p.setPen(QPen(border, is_required && !mapped ? 2.0 : 1.5));
            p.setBrush(bg);
            p.drawRoundedRect(r, radius, radius);

            QString disp = core::AppConfig::StandardColumnToCn(standard_[i]);
            p.setPen(is_required && !mapped ? QColor(245, 108, 108) : QColor(48, 49, 51));
            QString text = disp;
            if (is_required) text.prepend("★ ");
            p.drawText(r.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        QPoint pos = event->pos();

        // 检测点击连线
        for (int s_idx = 0; s_idx < mapping_.size(); s_idx++) {
            QString std_name = (mapping_.begin() + s_idx).key();
            QString imp_name = mapping_[std_name];
            int i_idx = imported_.indexOf(imp_name);
            int s_row = standard_.indexOf(std_name);
            if (i_idx < 0 || s_row < 0) continue;

            int y1 = ItemY(i_idx) + ITEM_HEIGHT / 2;
            int y2 = ItemY(s_row) + ITEM_HEIGHT / 2;
            int mid_x = width() / 2;

            // 近似检测：点是否在连线附近
            if (std::abs(pos.x() - mid_x) < 25) {
                int min_y = std::min(y1, y2) - 15;
                int max_y = std::max(y1, y2) + 15;
                if (pos.y() >= min_y && pos.y() <= max_y) {
                    // 移除连线
                    mapping_.remove(std_name);
                    update();
                    return;
                }
            }
        }

        // 检测左侧条目点击
        int left_x = PADDING;
        int right_x = width() - PADDING - COL_WIDTH;
        for (int i = 0; i < imported_.size(); i++) {
            QRect r(left_x, ItemY(i), COL_WIDTH, ITEM_HEIGHT);
            if (r.contains(pos)) {
                selected_imported_ = (selected_imported_ == i) ? -1 : i;
                update();
                return;
            }
        }

        // 检测右侧条目点击
        for (int i = 0; i < standard_.size(); i++) {
            QRect r(right_x, ItemY(i), COL_WIDTH, ITEM_HEIGHT);
            if (r.contains(pos)) {
                if (selected_imported_ >= 0) {
                    QString imp = imported_[selected_imported_];
                    QString std_name = standard_[i];

                    // 如果这个导入表头已经映射到其他列，先移除
                    for (auto it = mapping_.begin(); it != mapping_.end(); ) {
                        if (it.value() == imp) {
                            it = mapping_.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    // 如果这个标准列已经映射，替换
                    if (mapping_.contains(std_name)) {
                        mapping_[std_name] = imp;
                    } else {
                        mapping_.insert(std_name, imp);
                    }

                    selected_imported_ = -1;
                } else {
                    // 点击已有映射的右侧条目，取消映射
                    if (mapping_.contains(standard_[i])) {
                        mapping_.remove(standard_[i]);
                    }
                }
                update();
                return;
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        QPoint pos = event->pos();
        int old_hover = hovered_line_;
        hovered_line_ = -1;

        for (auto it = mapping_.begin(); it != mapping_.end(); ++it) {
            int s_idx = standard_.indexOf(it.key());
            int i_idx = imported_.indexOf(it.value());
            if (s_idx < 0 || i_idx < 0) continue;

            int y1 = ItemY(i_idx) + ITEM_HEIGHT / 2;
            int y2 = ItemY(s_idx) + ITEM_HEIGHT / 2;
            int mid_x = width() / 2;

            if (std::abs(pos.x() - mid_x) < 25) {
                int min_y = std::min(y1, y2) - 15;
                int max_y = std::max(y1, y2) + 15;
                if (pos.y() >= min_y && pos.y() <= max_y) {
                    hovered_line_ = s_idx;
                    setCursor(Qt::PointingHandCursor);
                    break;
                }
            }
        }

        if (hovered_line_ == -1) {
            setCursor(Qt::ArrowCursor);
        }

        if (hovered_line_ != old_hover) {
            update();
        }
    }

private:
    static const int PADDING = 16;
    static const int COL_WIDTH = 170;
    static const int ITEM_HEIGHT = 32;
    static const int ITEM_SPACING = 6;
    static const int HEADER_OFFSET = 36;

    int ItemY(int index) const {
        return HEADER_OFFSET + index * (ITEM_HEIGHT + ITEM_SPACING);
    }

    void UpdateGeometry() {
        int max_rows = std::max(imported_.size(), standard_.size());
        int h = HEADER_OFFSET + max_rows * (ITEM_HEIGHT + ITEM_SPACING) + PADDING;
        setMinimumHeight(h);
        setMinimumWidth(PADDING * 2 + COL_WIDTH * 2 + 100);
    }

    QStringList imported_;
    QStringList standard_;
    QStringList required_;
    QMap<QString, QString> mapping_;
    int selected_imported_ = -1;
    int hovered_line_ = -1;
};

// ============================================================
// HeaderMappingDialog
// ============================================================
HeaderMappingDialog::HeaderMappingDialog(const QStringList& imported_headers,
                                          const QMap<QString, QString>& auto_mapping,
                                          const QStringList& preview_headers,
                                          const QList<QStringList>& preview_rows,
                                          QWidget* parent)
    : QDialog(parent)
    , imported_headers_(imported_headers)
    , mapping_(auto_mapping)
    , preview_headers_(preview_headers)
    , preview_rows_(preview_rows) {
    standard_names_ = core::AppConfig::StandardColumnOrder();
    required_.clear();
    for (const auto &s : core::AppConfig::RequiredStandardColumns()) required_ += s;
    SetupUI();
}

void HeaderMappingDialog::SetupUI() {
    setWindowTitle(QString::fromUtf8("表头映射"));
    setModal(true);
    resize(680, 640);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    // 标题
    auto* title = new QLabel(QString::fromUtf8("表头映射"));
    title->setStyleSheet("font-size: 18px; font-weight: 600; color: #303133;");
    layout->addWidget(title);

    // 提示
    hint_label_ = new QLabel(QString::fromUtf8("必填列（* 标记）未能自动匹配，请点击左侧表头再点击右侧标准列名进行手动映射"));
    hint_label_->setStyleSheet("font-size: 13px; color: #E64545; background: #FEF0F0; padding: 10px 14px; border-radius: 8px;");
    hint_label_->setWordWrap(true);
    layout->addWidget(hint_label_);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    auto *btn_reset_auto = new QPushButton(QString::fromUtf8(" 🔄 重置自动映射"));
    btn_reset_auto->setCursor(Qt::PointingHandCursor);
    toolbar->addWidget(btn_reset_auto);
    btn_settings_ = new QPushButton(QString::fromUtf8(" 🧭 自定义识别关键字…"));
    btn_settings_->setCursor(Qt::PointingHandCursor);
    toolbar->addWidget(btn_settings_);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    // 映射视图
    view_ = new MappingView();
    view_->SetData(imported_headers_, standard_names_, required_, mapping_);
    layout->addWidget(view_, 1);

    // 操作提示
    auto* tip = new QLabel(QString::fromUtf8("操作说明：点击左侧表头选中 → 点击右侧标准列名完成映射 | 点击连线可取消映射"));
    tip->setStyleSheet("font-size: 12px; color: #909399;");
    layout->addWidget(tip);

    // 数据预览
    auto* preview_title = new QLabel(QString::fromUtf8("数据预览（前5行）"));
    preview_title->setStyleSheet("font-size: 13px; font-weight: 500; color: #303133;");
    layout->addWidget(preview_title);

    preview_table_ = new QTableWidget();
    preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preview_table_->setStyleSheet(R"QSS(
QTableWidget {
    border: 1px solid #ebeef5;
    border-radius: 6px;
    gridline-color: #ebeef5;
    background: white;
    font-size: 12px;
}
QTableWidget::item { padding: 6px; }
QHeaderView::section {
    background: #f5f7fa;
    padding: 8px 6px;
    border: none;
    border-right: 1px solid #ebeef5;
    border-bottom: 1px solid #ebeef5;
    font-weight: 500;
    font-size: 12px;
}
    )QSS");
    preview_table_->verticalHeader()->setVisible(false);

    int preview_rows_count = std::min(5, (int)preview_rows_.size());
    preview_table_->setColumnCount(preview_headers_.size());
    preview_table_->setRowCount(preview_rows_count);
    preview_table_->setHorizontalHeaderLabels(preview_headers_);
    preview_table_->horizontalHeader()->setStretchLastSection(true);

    for (int r = 0; r < preview_rows_count; r++) {
        for (int c = 0; c < preview_headers_.size() && c < preview_rows_[r].size(); c++) {
            auto* item = new QTableWidgetItem(preview_rows_[r][c]);
            item->setTextAlignment(Qt::AlignCenter);
            preview_table_->setItem(r, c, item);
        }
    }
    layout->addWidget(preview_table_);

    // 按钮区
    auto* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    auto* btn_cancel = new QPushButton(QString::fromUtf8("取消"));
    btn_cancel->setCursor(Qt::PointingHandCursor);
    btn_cancel->setStyleSheet(R"QSS(
QPushButton {
    padding: 10px 28px;
    border-radius: 8px;
    border: 1px solid #dcdfe6;
    background: white;
    color: #606266;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover { border-color: #409eff; color: #409eff; }
    )QSS");
    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    btn_layout->addWidget(btn_cancel);

    auto* btn_ok = new QPushButton(QString::fromUtf8("确认映射"));
    btn_ok->setCursor(Qt::PointingHandCursor);
    btn_ok->setStyleSheet(R"QSS(
QPushButton {
    padding: 10px 32px;
    background: #409eff;
    color: white;
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 600;
}
QPushButton:hover { background: #66b1ff; }
    )QSS");
    connect(btn_ok, &QPushButton::clicked, this, &HeaderMappingDialog::OnConfirm);
    btn_layout->addWidget(btn_ok);
    connect(btn_settings_, &QPushButton::clicked, this, &HeaderMappingDialog::OnOpenMappingSettings);
    connect(btn_reset_auto, &QPushButton::clicked, this, [this]() {
        services::CalcService svc(this);
        auto re = svc.AutoMapColumns(imported_headers_);
        for (auto it = re.cbegin(); it != re.cend(); ++it) {
            if (!mapping_.contains(it.key())) mapping_.insert(it.key(), it.value());
        }
        view_->SetData(imported_headers_, standard_names_, required_, mapping_);
    });

    layout->addLayout(btn_layout);

    setStyleSheet(R"QSS(
QDialog { background: #f5f7fa; }
QWidget { font-family: -apple-system, BlinkMacSystemFont, 'PingFang SC', sans-serif; }
    )QSS");
}

void HeaderMappingDialog::OnConfirm() {
    mapping_ = view_->GetMapping();
    for (const auto& req : required_) {
        if (!mapping_.contains(req) || mapping_[req].isEmpty()) {
            QString cn = core::AppConfig::StandardColumnToCn(req);
            QMessageBox::warning(this, QString::fromUtf8("提示"),
                QString::fromUtf8("必填列「%1」尚未映射，请先完成映射").arg(cn));
            return;
        }
    }

    QStringList lines;
    for (auto it = mapping_.cbegin(); it != mapping_.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        lines += QString("「%1 → %2」")
            .arg(it.value()).arg(core::AppConfig::StandardColumnToCn(it.key()));
    }
    auto ret = QMessageBox::question(
        this, QString::fromUtf8("确认映射"),
        QString::fromUtf8("已完成以下映射：\n%1\n\n是否记住这些对应关系，下次自动识别？")
            .arg(lines.join("、\n")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    remember_ = (ret == QMessageBox::Yes);

    accept();
}

void HeaderMappingDialog::OnOpenMappingSettings() {
    RuleSettingDialog dlg(this);
    dlg.OpenMappingTab();
    int r = dlg.exec();
    Q_UNUSED(r);
    services::CalcService svc(this);
    auto re = svc.AutoMapColumns(imported_headers_);
    for (auto it = re.cbegin(); it != re.cend(); ++it) {
        if (!mapping_.contains(it.key())) mapping_.insert(it.key(), it.value());
    }
    view_->SetData(imported_headers_, standard_names_, required_, mapping_);
}

QMap<QString, QString> HeaderMappingDialog::GetMapping() const {
    return view_ ? view_->GetMapping() : mapping_;
}

} // namespace freight::ui::dialogs
