#include "ui/main_window.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include "ui/dialogs/single_calc_dialog.hpp"
#include "ui/dialogs/batch_calc_dialog.hpp"
#include "ui/dialogs/rule_setting_dialog.hpp"
#include "ui/dialogs/customer_setting_dialog.hpp"
#include "ui/dialogs/system_setting_dialog.hpp"
#include "ui/dialogs/about_dialog.hpp"
#include "ui/dialogs/history_dialog.hpp"
#include "ui/dialogs/compare_dialog.hpp"
#include <QApplication>
#include <QScreen>
#include <QDebug>

namespace freight::ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    SetupUI();
    SetupStyles();
    SetupAdBanner();
}

MainWindow::~MainWindow() = default;

void MainWindow::SetupUI() {
    auto &cfg = core::AppConfig::Instance();
    auto &icons = IconManager::Instance();

    setWindowTitle(cfg.GetAppName() + " - " + cfg.GetCompanyName());
    setWindowIcon(icons.GetIcon("app_logo", IconCategory::LOGO, IconSize::SIZE_32));
    resize(900, 650);

    central_widget_ = new QWidget(this);
    setCentralWidget(central_widget_);

    main_layout_ = new QVBoxLayout(central_widget_);
    main_layout_->setContentsMargins(30, 20, 30, 20);
    main_layout_->setSpacing(20);

    // === 顶部广告位 ===
    ad_banner_ = new QFrame();
    ad_banner_->setObjectName("adBanner");
    ad_banner_->setFixedHeight(100);
    main_layout_->addWidget(ad_banner_);

    QVBoxLayout *ad_layout = new QVBoxLayout(ad_banner_);
    ad_layout->setContentsMargins(30, 10, 30, 10);

    ad_label_ = new QLabel();
    ad_label_->setObjectName("adLabel");
    ad_label_->setAlignment(Qt::AlignCenter);
    ad_layout->addWidget(ad_label_);

    QLabel *ad_sub = new QLabel("更多优惠请拨打客服热线：17771300068 / 19171045360");
    ad_sub->setObjectName("adSubLabel");
    ad_sub->setAlignment(Qt::AlignCenter);
    ad_layout->addWidget(ad_sub);

    // === 卡片按钮区 ===
    card_area_ = new QFrame();
    card_area_->setObjectName("cardArea");
    main_layout_->addWidget(card_area_, 1);

    card_layout_ = new QGridLayout(card_area_);
    card_layout_->setContentsMargins(40, 30, 40, 30);
    card_layout_->setSpacing(20);

    auto createCardBtn = [&](const QString &text, const QString &iconName) {
        auto *btn = new QPushButton("  " + text);
        btn->setObjectName("cardButton");
        btn->setIcon(icons.CardIcon(iconName));
        btn->setIconSize(QSize(64, 64));
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    btn_single_calc_ = createCardBtn("单条计算", "calc_single");
    btn_batch_calc_ = createCardBtn("批量计算", "calc_batch");
    btn_compare_ = createCardBtn("对比分析", "compare");
    btn_history_ = createCardBtn("历史记录", "history");

    card_layout_->addWidget(btn_single_calc_, 0, 0);
    card_layout_->addWidget(btn_batch_calc_, 0, 1);
    card_layout_->addWidget(btn_compare_, 1, 0);
    card_layout_->addWidget(btn_history_, 1, 1);

    connect(btn_single_calc_, &QPushButton::clicked, this, &MainWindow::OnSingleCalc);
    connect(btn_batch_calc_, &QPushButton::clicked, this, &MainWindow::OnBatchCalc);
    connect(btn_compare_, &QPushButton::clicked, this, &MainWindow::OnCompare);
    connect(btn_history_, &QPushButton::clicked, this, &MainWindow::OnHistory);

    // === 设置按钮区 ===
    setting_area_ = new QFrame();
    setting_area_->setObjectName("settingArea");
    main_layout_->addWidget(setting_area_);

    setting_layout_ = new QHBoxLayout(setting_area_);
    setting_layout_->setContentsMargins(20, 10, 20, 10);
    setting_layout_->setSpacing(15);

    auto createSettingBtn = [&](const QString &text, const QString &iconName) {
        auto *btn = new QPushButton("  " + text);
        btn->setObjectName("settingButton");
        btn->setIcon(icons.SettingIcon(iconName));
        btn->setIconSize(QSize(20, 20));
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    btn_rule_setting_ = createSettingBtn("规则设置", "rule_setting");
    btn_customer_setting_ = createSettingBtn("客户规则设置", "customer");
    btn_system_setting_ = createSettingBtn("系统设置", "system_setting");
    btn_about_ = createSettingBtn("关于", "about");

    setting_layout_->addWidget(btn_rule_setting_);
    setting_layout_->addWidget(btn_customer_setting_);
    setting_layout_->addWidget(btn_system_setting_);
    setting_layout_->addStretch();
    setting_layout_->addWidget(btn_about_);

    connect(btn_rule_setting_, &QPushButton::clicked, this, &MainWindow::OnRuleSetting);
    connect(btn_customer_setting_, &QPushButton::clicked, this, &MainWindow::OnCustomerSetting);
    connect(btn_system_setting_, &QPushButton::clicked, this, &MainWindow::OnSystemSetting);
    connect(btn_about_, &QPushButton::clicked, this, &MainWindow::OnAbout);

    // === 底部信息 ===
    footer_label_ = new QLabel();
    footer_label_->setObjectName("footerLabel");
    footer_label_->setAlignment(Qt::AlignCenter);
    footer_label_->setOpenExternalLinks(true);
    footer_label_->setText(
        cfg.GetCompanyName() + "  © 2026   |   "
        "<a href='http://" + cfg.GetWebsite() + "' style='color:#909399; text-decoration:none;'>"
        + cfg.GetWebsite() + "</a>"
    );
    main_layout_->addWidget(footer_label_);

    // 居中显示
    QScreen *screen = QApplication::primaryScreen();
    QRect geom = screen->availableGeometry();
    move((geom.width() - width()) / 2, (geom.height() - height()) / 2);
}

void MainWindow::SetupStyles() {
    setStyleSheet(R"QSS(
QMainWindow {
    background-color: #f5f7fa;
}

#adBanner {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #667eea, stop:1 #764ba2);
    border-radius: 12px;
}

#adLabel {
    color: white;
    font-size: 22px;
    font-weight: bold;
}

#adSubLabel {
    color: rgba(255,255,255,0.85);
    font-size: 13px;
}

#cardArea {
    background-color: #ffffff;
    border-radius: 12px;
}

QPushButton#cardButton {
    background-color: #ffffff;
    border: 1px solid #e4e7ed;
    border-radius: 12px;
    padding: 30px 20px;
    font-size: 18px;
    font-weight: 500;
    color: #303133;
    min-height: 180px;
    text-align: center;
}

QPushButton#cardButton:hover {
    border: 1px solid #409eff;
    background-color: #f0f7ff;
}

QPushButton#cardButton:pressed {
    background-color: #ecf5ff;
}

#settingArea {
    background-color: #ffffff;
    border-radius: 10px;
}

QPushButton#settingButton {
    background-color: #f5f7fa;
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    padding: 12px 20px;
    font-size: 14px;
    color: #606266;
}

QPushButton#settingButton:hover {
    border-color: #409eff;
    color: #409eff;
    background-color: #f0f7ff;
}

QPushButton#settingButton:pressed {
    background-color: #ecf5ff;
}

#footerLabel {
    color: #909399;
    font-size: 12px;
    padding: 10px 0;
}
    )QSS");
}

void MainWindow::SetupAdBanner() {
    ad_texts_ << "🎉 小乔运费结算 - 智能运费计算，高效又准确！"
              << "📦 支持中通、圆通、顺丰等多家快递价格对比"
              << "💰 批量计算500万条仅需3分钟，省时省力"
              << "🐱 杭州喵喵至家，买软件送公司IT运维，让你省心省钱";

    ad_index_ = 0;
    ad_label_->setText(ad_texts_[0]);

    ad_timer_ = new QTimer(this);
    ad_timer_->setInterval(4000);
    connect(ad_timer_, &QTimer::timeout, this, &MainWindow::OnAdTimer);
    ad_timer_->start();
}

void MainWindow::OnAdTimer() {
    ad_index_ = (ad_index_ + 1) % ad_texts_.size();
    ad_label_->setText(ad_texts_[ad_index_]);
}

void MainWindow::OnSingleCalc() {
    dialogs::SingleCalcDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnBatchCalc() {
    dialogs::BatchCalcDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnCompare() {
    dialogs::CompareDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnHistory() {
    dialogs::HistoryDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnRuleSetting() {
    dialogs::RuleSettingDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnCustomerSetting() {
    dialogs::CustomerSettingDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnSystemSetting() {
    dialogs::SystemSettingDialog dlg(this);
    dlg.exec();
}

void MainWindow::OnAbout() {
    dialogs::AboutDialog dlg(this);
    dlg.exec();
}

} // namespace freight::ui
