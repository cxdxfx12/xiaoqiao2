#include "license_generator_widget.hpp"
#include "core/license_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QDateTime>
#include <QRegularExpression>

namespace freight::tools {

using namespace freight::core;

LicenseGeneratorWidget::LicenseGeneratorWidget(QWidget *parent) : QWidget(parent) {
    SetupUI();
}

LicenseGeneratorWidget::~LicenseGeneratorWidget() = default;

void LicenseGeneratorWidget::SetupUI() {
    setWindowTitle("小乔运费结算 - 授权生成器");
    resize(520, 560);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(12);

    // 标题
    auto *lbl_title = new QLabel("授权码生成器");
    lbl_title->setStyleSheet("font-size: 20px; font-weight: bold; color: #303133;");
    lbl_title->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(lbl_title);

    auto *lbl_subtitle = new QLabel("用于生成客户授权码");
    lbl_subtitle->setStyleSheet("font-size: 12px; color: #909399;");
    lbl_subtitle->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(lbl_subtitle);

    main_layout->addSpacing(8);

    // 输入表单
    auto *form_group = new QGroupBox("参数设置");
    form_group->setStyleSheet("QGroupBox { font-weight: 600; color: #303133; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 12px; } QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }");
    auto *form_layout = new QFormLayout(form_group);
    form_layout->setContentsMargins(16, 12, 16, 12);
    form_layout->setSpacing(10);
    form_layout->setLabelAlignment(Qt::AlignRight);

    // 机器码
    edit_machine_ = new QLineEdit();
    edit_machine_->setPlaceholderText("格式：XXXX-XXXX-XXXX-XXXX");
    edit_machine_->setStyleSheet("padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px; font-family: 'Menlo','Courier New',monospace;");
    form_layout->addRow("机器码：", edit_machine_);

    // 授权类型
    combo_type_ = new QComboBox();
    combo_type_->addItem("试用版", static_cast<int>(LicenseType::Trial));
    combo_type_->addItem("个人版", static_cast<int>(LicenseType::Personal));
    combo_type_->addItem("企业版", static_cast<int>(LicenseType::Enterprise));
    combo_type_->addItem("永久版", static_cast<int>(LicenseType::Permanent));
    combo_type_->setCurrentIndex(1);
    combo_type_->setStyleSheet("padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px;");
    form_layout->addRow("授权类型：", combo_type_);

    // 有效期天数
    spin_days_ = new QSpinBox();
    spin_days_->setRange(1, 36500);
    spin_days_->setValue(365);
    spin_days_->setSuffix(" 天");
    spin_days_->setStyleSheet("padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px;");
    form_layout->addRow("有效期：", spin_days_);

    // 到期日期
    edit_expire_ = new QDateTimeEdit(QDateTime::currentDateTime().addDays(365));
    edit_expire_->setCalendarPopup(true);
    edit_expire_->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    edit_expire_->setStyleSheet("padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px;");
    form_layout->addRow("到期日期：", edit_expire_);

    // 密钥
    edit_secret_ = new QLineEdit();
    edit_secret_->setPlaceholderText("留空使用默认密钥");
    edit_secret_->setEchoMode(QLineEdit::Password);
    edit_secret_->setStyleSheet("padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px; font-family: 'Menlo','Courier New',monospace;");
    form_layout->addRow("密钥：", edit_secret_);

    main_layout->addWidget(form_group);

    // 生成按钮
    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    btn_generate_ = new QPushButton(" 生成授权码");
    btn_generate_->setCursor(Qt::PointingHandCursor);
    btn_generate_->setStyleSheet("padding: 10px 24px; background-color: #409eff; color: white; border: none; border-radius: 6px; font-size: 14px; font-weight: 500;");
    btn_layout->addWidget(btn_generate_);

    main_layout->addLayout(btn_layout);

    main_layout->addSpacing(4);

    // 结果区域
    auto *result_group = new QGroupBox("生成结果");
    result_group->setStyleSheet("QGroupBox { font-weight: 600; color: #303133; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 12px; } QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }");
    auto *result_layout = new QVBoxLayout(result_group);
    result_layout->setContentsMargins(16, 12, 16, 12);
    result_layout->setSpacing(8);

    text_result_ = new QTextEdit();
    text_result_->setReadOnly(true);
    text_result_->setPlaceholderText("生成的授权码将显示在这里...");
    text_result_->setStyleSheet("padding: 8px; border: 1px solid #dcdfe6; border-radius: 6px; font-family: 'Menlo','Courier New',monospace; font-size: 13px; background: #f5f7fa;");
    text_result_->setFixedHeight(100);
    result_layout->addWidget(text_result_);

    // 验证状态
    lbl_validate_ = new QLabel("");
    lbl_validate_->setAlignment(Qt::AlignCenter);
    lbl_validate_->setStyleSheet("font-size: 12px;");
    result_layout->addWidget(lbl_validate_);

    // 复制按钮
    auto *copy_layout = new QHBoxLayout();
    copy_layout->addStretch();

    btn_copy_ = new QPushButton(" 复制授权码");
    btn_copy_->setCursor(Qt::PointingHandCursor);
    btn_copy_->setEnabled(false);
    btn_copy_->setStyleSheet("padding: 6px 16px; border: 1px solid #dcdfe6; border-radius: 6px; background: white; color: #606266;");
    copy_layout->addWidget(btn_copy_);

    result_layout->addLayout(copy_layout);
    main_layout->addWidget(result_group);

    main_layout->addStretch();

    // 连接信号
    connect(btn_generate_, &QPushButton::clicked, this, &LicenseGeneratorWidget::OnGenerate);
    connect(btn_copy_, &QPushButton::clicked, this, &LicenseGeneratorWidget::OnCopy);
    connect(combo_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LicenseGeneratorWidget::OnTypeChanged);
    connect(spin_days_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int days) {
        edit_expire_->setDateTime(QDateTime::currentDateTime().addDays(days));
    });
    connect(edit_expire_, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &dt) {
        int days = QDateTime::currentDateTime().daysTo(dt);
        if (days > 0) {
            spin_days_->blockSignals(true);
            spin_days_->setValue(days);
            spin_days_->blockSignals(false);
        }
    });

    setStyleSheet(R"QSS(
QWidget {
    background-color: #ffffff;
    color: #303133;
    font-size: 13px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDateTimeEdit:focus, QTextEdit:focus {
    border: 1px solid #409eff;
}
    )QSS");
}

void LicenseGeneratorWidget::OnTypeChanged(int index) {
    LicenseType type = static_cast<LicenseType>(combo_type_->itemData(index).toInt());
    bool is_permanent = (type == LicenseType::Permanent);
    spin_days_->setEnabled(!is_permanent);
    edit_expire_->setEnabled(!is_permanent);
}

void LicenseGeneratorWidget::OnGenerate() {
    QString machine = edit_machine_->text().trimmed().toUpper();
    if (machine.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入机器码");
        edit_machine_->setFocus();
        return;
    }

    // 校验机器码格式（允许带或不带分隔符）
    QString clean_machine = machine;
    clean_machine.remove("-");
    if (clean_machine.length() != 32) {
        QMessageBox::warning(this, "提示", "机器码格式不正确，应为32位字符（或XXXX-XXXX-XXXX-XXXX格式）");
        edit_machine_->setFocus();
        return;
    }

    LicenseType type = static_cast<LicenseType>(combo_type_->currentData().toInt());
    QDateTime expire = edit_expire_->dateTime();

    QByteArray secret_key;
    if (!edit_secret_->text().trimmed().isEmpty()) {
        secret_key = QByteArray::fromHex(edit_secret_->text().trimmed().toUtf8());
    } else {
        secret_key = LicenseManager::DefaultSecretKey();
    }

    QString license_key = LicenseManager::GenerateLicenseKey(machine, type, expire, secret_key);

    // 验证
    LicenseInfo info = LicenseManager::VerifyLicenseKey(license_key, machine, secret_key);

    // 显示结果
    text_result_->setText(license_key);
    btn_copy_->setEnabled(true);

    if (info.valid && !info.IsExpired()) {
        lbl_validate_->setText("✓ 验证通过 - " + info.TypeString() + "，有效期至 " + info.expire_date.toString("yyyy-MM-dd"));
        lbl_validate_->setStyleSheet("font-size: 12px; color: #67c23a;");
    } else {
        lbl_validate_->setText("✗ 验证失败: " + info.error_msg);
        lbl_validate_->setStyleSheet("font-size: 12px; color: #f56c6c;");
    }
}

void LicenseGeneratorWidget::OnCopy() {
    QString key = text_result_->toPlainText().trimmed();
    if (key.isEmpty()) return;

    QGuiApplication::clipboard()->setText(key);
    QMessageBox::information(this, "成功", "授权码已复制到剪贴板");
}

void LicenseGeneratorWidget::OnValidate() {
    OnGenerate();
}

} // namespace freight::tools
