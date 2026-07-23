#include "ui/dialogs/fee_breakdown_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QGroupBox>
#include <QProgressBar>
#include <QGuiApplication>
#include <QScreen>

namespace freight::ui::dialogs {

FeeBreakdownDialog::FeeBreakdownDialog(const FeeBreakdownData &data, QWidget *parent)
    : QDialog(parent), data_(data) {
    SetupUI();
}

void FeeBreakdownDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("运费明细分解");
    setWindowIcon(icons.ActionIcon("info"));
    resize(460, 580);
    setModal(true);

    main_layout_ = new QVBoxLayout(this);
    main_layout_->setContentsMargins(20, 20, 20, 20);
    main_layout_->setSpacing(14);

    auto *title_layout = new QHBoxLayout();
    auto *icon_label = new QLabel();
    icon_label->setPixmap(icons.ActionIcon("calc_detail").pixmap(28, 28));
    auto *tbox = new QVBoxLayout();
    lbl_title_ = new QLabel("运费明细分解");
    lbl_title_->setStyleSheet("font-size: 18px; font-weight: 600; color: #303133;");
    lbl_subtitle_ = new QLabel(QString("运单号: %1").arg(
        data_.order_id.isEmpty() ? "(未提供)" : data_.order_id));
    lbl_subtitle_->setStyleSheet("color: #909399; font-size: 12px;");
    tbox->addWidget(lbl_title_);
    tbox->addWidget(lbl_subtitle_);
    title_layout->addWidget(icon_label);
    title_layout->addSpacing(10);
    title_layout->addLayout(tbox, 1);
    main_layout_->addLayout(title_layout);

    // ==== 基础信息卡 ====
    card_info_ = new QFrame();
    card_info_->setStyleSheet(R"QSS(
QFrame { background: white; border-radius: 10px; border: 1px solid #ebeef5; }
    )QSS");
    auto *info_layout = new QGridLayout(card_info_);
    info_layout->setContentsMargins(16, 14, 16, 14);
    info_layout->setHorizontalSpacing(20);
    info_layout->setVerticalSpacing(10);

    auto add_info = [&](int row, const QString &lbl, const QString &val, const QString &color = "#606266") {
        auto *l = new QLabel(lbl);
        l->setStyleSheet("color: #909399; font-size: 12px;");
        auto *v = new QLabel(val.isEmpty() ? "—" : val);
        v->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 500;").arg(color));
        info_layout->addWidget(l, row, 0);
        info_layout->addWidget(v, row, 1);
    };

    add_info(0, "目的省份:", data_.dest_province);
    add_info(1, "目的城市:", data_.dest_city);
    add_info(2, "使用模板:", data_.template_name, "#409eff");
    add_info(3, "客户编号:", data_.customer_id.isEmpty() ? "（全局规则）" : data_.customer_id);

    main_layout_->addWidget(card_info_);

    // ==== 计费重量进度条 ====
    auto *wbox = new QFrame();
    wbox->setStyleSheet(card_info_->styleSheet());
    auto *wbox_layout = new QVBoxLayout(wbox);
    wbox_layout->setContentsMargins(16, 12, 16, 12);
    wbox_layout->setSpacing(8);

    auto *wheader = new QHBoxLayout();
    auto *wl = new QLabel("计费重量分析");
    wl->setStyleSheet("font-size: 13px; font-weight: 600; color: #303133;");
    wheader->addWidget(wl);
    wheader->addStretch();
    auto *wval = new QLabel(QString("%1 KG").arg(data_.charge_weight, 0, 'f', 3));
    wval->setStyleSheet("font-size: 14px; font-weight: 700; color: #409eff;");
    wheader->addWidget(wval);
    wbox_layout->addLayout(wheader);

    weight_bar_ = new QProgressBar();
    weight_bar_->setRange(0, 100);
    double maxw = qMax(qMax(data_.weight, data_.vol_weight), 0.1);
    int pct_act = qMin(100, static_cast<int>(data_.weight / maxw * 100));
    int pct_vol = qMin(100, static_cast<int>(data_.vol_weight / maxw * 100));
    int pct_charge = qMin(100, static_cast<int>(data_.charge_weight / maxw * 100));
    weight_bar_->setValue(pct_charge);
    weight_bar_->setStyleSheet(R"QSS(
QProgressBar {
    border: 1px solid #e4e7ed;
    border-radius: 6px;
    background: #f5f7fa;
    height: 18px;
    text-align: center;
    font-size: 10px;
    color: #606266;
}
QProgressBar::chunk { background-color: #409eff; border-radius: 6px; }
    )QSS");
    weight_bar_->setFormat(QString("实重%1% · 体积重%2%").arg(pct_act).arg(pct_vol));
    wbox_layout->addWidget(weight_bar_);

    auto *wfoot = new QHBoxLayout();
    auto *wa = new QLabel(QString("实重: %1 KG").arg(data_.weight, 0, 'f', 3));
    wa->setStyleSheet("font-size: 11px; color: #909399;");
    auto *wv = new QLabel(QString("体积重: %1 KG").arg(data_.vol_weight, 0, 'f', 3));
    wv->setStyleSheet("font-size: 11px; color: #909399;");
    auto *wt = new QLabel(QString("阶梯: %1").arg(data_.weight_tier.isEmpty() ? "—" : data_.weight_tier));
    wt->setStyleSheet("font-size: 11px; color: #e6a23c; font-weight: 500;");
    wfoot->addWidget(wa);
    wfoot->addSpacing(14);
    wfoot->addWidget(wv);
    wfoot->addStretch();
    wfoot->addWidget(wt);
    wbox_layout->addLayout(wfoot);

    main_layout_->addWidget(wbox);

    // ==== 费用分解卡 ====
    card_breakdown_ = new QFrame();
    card_breakdown_->setStyleSheet(card_info_->styleSheet());
    auto *bk_layout = new QVBoxLayout(card_breakdown_);
    bk_layout->setContentsMargins(16, 14, 16, 14);
    bk_layout->setSpacing(8);

    auto *bk_title = new QLabel("费用构成");
    bk_title->setStyleSheet("font-size: 13px; font-weight: 600; color: #303133; margin-bottom: 4px;");
    bk_layout->addWidget(bk_title);

    bk_layout->addWidget(CreateFeeItem("基础运费", data_.base_freight, true, false));
    if (data_.fuel_surcharge != 0)
        bk_layout->addWidget(CreateFeeItem("燃油附加费", data_.fuel_surcharge, data_.fuel_surcharge >= 0, false));
    if (data_.remote_surcharge != 0)
        bk_layout->addWidget(CreateFeeItem("偏远地区加价", data_.remote_surcharge, true, false));
    if (data_.customer_discount != 0)
        bk_layout->addWidget(CreateFeeItem("客户优惠/折扣", -qAbs(data_.customer_discount), false, false));
    if (data_.other_fee != 0)
        bk_layout->addWidget(CreateFeeItem("其他附加费", data_.other_fee, data_.other_fee >= 0, false));

    main_layout_->addWidget(card_breakdown_);

    // ==== 合计 ====
    card_total_ = new QFrame();
    card_total_->setStyleSheet(R"QSS(
QFrame {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #667eea, stop:1 #764ba2);
    border-radius: 10px;
}
QLabel { background: transparent; }
    )QSS");
    auto *total_layout = new QHBoxLayout(card_total_);
    total_layout->setContentsMargins(20, 16, 20, 16);

    auto *tl = new QLabel("合计总运费");
    tl->setStyleSheet("color: white; font-size: 14px; font-weight: 500;");
    auto *tv = new QLabel(QString("¥ %1").arg(data_.total_fee, 0, 'f', 2));
    tv->setStyleSheet("color: white; font-size: 24px; font-weight: 700;");
    total_layout->addWidget(tl);
    total_layout->addStretch();
    total_layout->addWidget(tv);

    main_layout_->addWidget(card_total_);

    main_layout_->addStretch();

    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    btn_close_ = new QPushButton("我知道了");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(R"QSS(
QPushButton {
    padding: 10px 36px;
    background: #409eff;
    color: white;
    border: none;
    border-radius: 8px;
    font-size: 14px;
    font-weight: 500;
}
QPushButton:hover { background: #66b1ff; }
    )QSS");
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    btn_layout->addWidget(btn_close_);
    main_layout_->addLayout(btn_layout);

    setStyleSheet(R"QSS(
QDialog {
    background: linear-gradient(180deg, #f0f5ff 0%, #f5f7fa 100%);
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}
    )QSS");
}

QFrame* FeeBreakdownDialog::CreateFeeItem(const QString &label, double value, bool positive, bool highlight) {
    auto *frame = new QFrame();
    frame->setStyleSheet(QString("background: %1; border-radius: 6px;")
        .arg(highlight ? "#ecf5ff" : "transparent"));
    auto *l = new QHBoxLayout(frame);
    l->setContentsMargins(10, 8, 10, 8);
    l->setSpacing(8);

    auto *dot = new QLabel();
    QColor dotColor = positive ? QColor("#67c23a") : QColor("#f56c6c");
    dot->setStyleSheet(QString(
        "background: %1; border-radius: 3px; min-width: 6px; max-width: 6px; min-height: 6px; max-height: 6px;"
    ).arg(dotColor.name()));

    auto *lbl = new QLabel(label);
    lbl->setStyleSheet("color: #606266; font-size: 13px;");

    auto prefix = value >= 0 ? "+ " : "- ";
    auto valueStr = QString("%1 ¥ %2").arg(prefix).arg(qAbs(value), 0, 'f', 2);
    auto *val = new QLabel(valueStr);
    val->setStyleSheet(QString(
        "font-size: 13px; font-weight: 600; color: %1;"
    ).arg(positive ? "#303133" : "#f56c6c"));

    l->addWidget(dot);
    l->addWidget(lbl, 1);
    l->addWidget(val);
    return frame;
}

} // namespace freight::ui::dialogs
