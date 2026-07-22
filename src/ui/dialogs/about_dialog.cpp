#include "ui/dialogs/about_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QLabel>
#include <QString>
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QDebug>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QClipboard>
#include <QGuiApplication>

namespace freight::ui::dialogs {

QString GenerateMachineCode() {
    QString raw;

    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : ifaces) {
        if (iface.isValid() && !iface.hardwareAddress().isEmpty()
            && (iface.type() == QNetworkInterface::Ethernet
                || iface.type() == QNetworkInterface::Wifi)) {
            raw += iface.hardwareAddress();
        }
    }

    QString machine_id = QString::fromStdString(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex().toStdString()
    ).toUpper();

    // 格式化为 XXXX-XXXX-XXXX-XXXX
    QString formatted;
    for (int i = 0; i < machine_id.length(); i += 4) {
        if (i > 0) formatted += "-";
        formatted += machine_id.mid(i, 4);
    }
    return formatted;
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::SetupUI() {
    auto &cfg = core::AppConfig::Instance();
    auto &icons = IconManager::Instance();

    setWindowTitle("关于");
    setWindowIcon(icons.SettingIcon("about"));
    resize(460, 500);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(30, 30, 30, 20);
    main_layout->setSpacing(10);
    main_layout->setAlignment(Qt::AlignTop);

    // Logo
    lbl_logo_ = new QLabel();
    lbl_logo_->setAlignment(Qt::AlignCenter);
    QPixmap logo = icons.GetIcon("app_logo", IconCategory::LOGO, IconSize::SIZE_64)
        .pixmap(64, 64);
    lbl_logo_->setPixmap(logo);
    main_layout->addWidget(lbl_logo_);

    // 应用名
    lbl_app_name_ = new QLabel(cfg.GetAppName());
    lbl_app_name_->setAlignment(Qt::AlignCenter);
    lbl_app_name_->setStyleSheet("font-size: 22px; font-weight: bold; color: #303133;");
    main_layout->addWidget(lbl_app_name_);

    // 版本
    lbl_version_ = new QLabel("版本 " + cfg.GetVersion());
    lbl_version_->setAlignment(Qt::AlignCenter);
    lbl_version_->setStyleSheet("font-size: 13px; color: #909399;");
    main_layout->addWidget(lbl_version_);

    main_layout->addSpacing(10);

    auto *line1 = new QFrame();
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet("color: #e4e7ed;");
    main_layout->addWidget(line1);

    main_layout->addSpacing(8);

    lbl_company_ = new QLabel(cfg.GetCompanyName());
    lbl_company_->setAlignment(Qt::AlignCenter);
    lbl_company_->setStyleSheet("font-size: 14px; color: #606266; font-weight: 500;");
    main_layout->addWidget(lbl_company_);

    main_layout->addSpacing(4);

    lbl_website_ = new QLabel("官网：<a href='http://www.hbdxm.com' style='color:#409eff; text-decoration:none;'>www.hbdxm.com</a>");
    lbl_website_->setAlignment(Qt::AlignCenter);
    lbl_website_->setOpenExternalLinks(true);
    lbl_website_->setStyleSheet("font-size: 13px;");
    main_layout->addWidget(lbl_website_);

    lbl_phone_ = new QLabel("客服热线：" + cfg.GetServicePhone());
    lbl_phone_->setAlignment(Qt::AlignCenter);
    lbl_phone_->setStyleSheet("font-size: 13px; color: #606266;");
    main_layout->addWidget(lbl_phone_);

    main_layout->addSpacing(10);

    auto *line2 = new QFrame();
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet("color: #e4e7ed;");
    main_layout->addWidget(line2);

    main_layout->addSpacing(8);

    // 授权信息区域
    QString machine_code = GenerateMachineCode();

    auto *auth_label = new QLabel("授权信息");
    auth_label->setStyleSheet("font-size: 14px; font-weight: 600; color: #303133;");
    main_layout->addWidget(auth_label);

    main_layout->addSpacing(4);

    // 机器码
    auto *mc_layout = new QHBoxLayout();
    auto *mc_label = new QLabel("机器码：");
    mc_label->setStyleSheet("font-size: 12px; color: #909399; min-width: 60px;");
    auto *mc_value = new QLabel(machine_code);
    mc_value->setStyleSheet("font-size: 13px; color: #303133; font-family: 'Menlo','Courier New',monospace;");
    mc_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copy_btn = new QPushButton("复制");
    copy_btn->setCursor(Qt::PointingHandCursor);
    copy_btn->setStyleSheet("padding: 2px 8px; font-size: 11px; border: 1px solid #dcdfe6; border-radius: 4px; background: white;");
    mc_layout->addWidget(mc_label);
    mc_layout->addWidget(mc_value, 1);
    mc_layout->addWidget(copy_btn);
    main_layout->addLayout(mc_layout);

    main_layout->addSpacing(6);

    // 授权状态
    auto *lic_layout = new QHBoxLayout();
    auto *lic_label = new QLabel("授权状态：");
    lic_label->setStyleSheet("font-size: 12px; color: #909399; min-width: 60px;");
    auto *lic_value = new QLabel("试用版");
    lic_value->setStyleSheet("font-size: 13px; color: #e6a23c; font-weight: 500;");
    lic_layout->addWidget(lic_label);
    lic_layout->addWidget(lic_value, 1);
    main_layout->addLayout(lic_layout);

    main_layout->addSpacing(4);

    // 授权有效期
    auto *exp_layout = new QHBoxLayout();
    auto *exp_label = new QLabel("有效期：");
    exp_label->setStyleSheet("font-size: 12px; color: #909399; min-width: 60px;");
    auto *exp_value = new QLabel("30天试用");
    exp_value->setStyleSheet("font-size: 13px; color: #606266;");
    exp_layout->addWidget(exp_label);
    exp_layout->addWidget(exp_value, 1);
    main_layout->addLayout(exp_layout);

    main_layout->addStretch();

    // 按钮
    auto *btn_layout = new QHBoxLayout();
    auto *btn_activate = new QPushButton(" 授权激活");
    btn_activate->setCursor(Qt::PointingHandCursor);
    btn_activate->setStyleSheet("padding: 8px 16px; border: 1px solid #409eff; color: #409eff; border-radius: 6px; background: white;");
    btn_layout->addWidget(btn_activate);
    btn_layout->addStretch();

    auto *btn_ok = new QPushButton(" 确定");
    btn_ok->setObjectName("primaryBtn");
    btn_ok->setCursor(Qt::PointingHandCursor);
    btn_ok->setDefault(true);
    btn_layout->addWidget(btn_ok);
    main_layout->addLayout(btn_layout);

    connect(btn_ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(copy_btn, &QPushButton::clicked, this, [machine_code]() {
        QGuiApplication::clipboard()->setText(machine_code);
        QMessageBox::information(nullptr, "提示", "机器码已复制到剪贴板");
    });
    connect(btn_activate, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString key = QInputDialog::getText(this, "授权激活",
            "请输入授权码：", QLineEdit::Normal, "", &ok);
        if (ok && !key.trimmed().isEmpty()) {
            QMessageBox::information(this, "提示", "授权验证功能开发中...\n\n请联系客服：17771300068");
        }
    });

    setStyleSheet(R"QSS(
QDialog {
    background-color: #ffffff;
    border-radius: 12px;
}
QPushButton#primaryBtn {
    background-color: #409eff;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 8px 24px;
    font-size: 14px;
}
QPushButton#primaryBtn:hover { background-color: #66b1ff; }
    )QSS");
}

} // namespace freight::ui::dialogs
