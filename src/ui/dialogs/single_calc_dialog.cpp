#include "ui/dialogs/single_calc_dialog.hpp"
#include "services/calc_service.hpp"
#include "ui/icon_manager.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDebug>

namespace freight::ui::dialogs {

SingleCalcDialog::SingleCalcDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
}

SingleCalcDialog::~SingleCalcDialog() = default;

void SingleCalcDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("单条计算");
    setWindowIcon(icons.CardIcon("calc_single"));
    resize(500, 550);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    // 输入区域
    auto *input_group = new QGroupBox("输入信息");
    auto *form_layout = new QFormLayout(input_group);
    form_layout->setContentsMargins(20, 20, 20, 20);
    form_layout->setSpacing(12);

    edt_order_id_ = new QLineEdit();
    edt_order_id_->setPlaceholderText("选填，运单号");
    form_layout->addRow("运单号：", edt_order_id_);

    cbo_province_ = new QComboBox();
    cbo_province_->addItems({"江苏","浙江","安徽","上海","山东","广东","福建","北京",
                              "河南","湖北","湖南","江西","天津","河北","山西","广西",
                              "四川","重庆","陕西","贵州","辽宁","吉林","黑龙江","云南",
                              "海南","甘肃","青海","内蒙古","宁夏","新疆","西藏"});
    form_layout->addRow("目的省份：", cbo_province_);

    edt_city_ = new QLineEdit();
    edt_city_->setPlaceholderText("选填");
    form_layout->addRow("目的城市：", edt_city_);

    spn_weight_ = new QDoubleSpinBox();
    spn_weight_->setRange(0.01, 99999.0);
    spn_weight_->setDecimals(3);
    spn_weight_->setValue(1.0);
    spn_weight_->setSuffix(" kg");
    form_layout->addRow("实际重量：", spn_weight_);

    spn_vol_weight_ = new QDoubleSpinBox();
    spn_vol_weight_->setRange(0.0, 99999.0);
    spn_vol_weight_->setDecimals(3);
    spn_vol_weight_->setValue(0.0);
    spn_vol_weight_->setSuffix(" kg");
    form_layout->addRow("体积重量：", spn_vol_weight_);

    cbo_template_ = new QComboBox();
    // 从数据库加载所有模板
    {
        auto &cfg = core::AppConfig::Instance();
        db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
        repo.Init();
        auto templates = repo.ListTemplates();
        for (const auto &t : templates) {
            auto m = t.toMap();
            cbo_template_->addItem(m["template_name"].toString(), m["template_id"].toString());
        }
        if (cbo_template_->count() == 0) {
            cbo_template_->addItem("中通标准快递", "zto_standard");
        }
    }
    form_layout->addRow("运费模板：", cbo_template_);

    main_layout->addWidget(input_group);

    // 结果区域
    auto *result_group = new QGroupBox("计算结果");
    auto *result_layout = new QFormLayout(result_group);
    result_layout->setContentsMargins(20, 20, 20, 20);
    result_layout->setSpacing(10);

    auto addResult = [&](const QString &label, QLabel **out) {
        *out = new QLabel("—");
        (*out)->setStyleSheet("font-size: 14px; color: #303133;");
        result_layout->addRow(label + "：", *out);
    };

    addResult("计费重量", &lbl_result_charge_weight_);
    addResult("基础运费", &lbl_result_base_fee_);
    addResult("燃油附加费", &lbl_result_fuel_);
    addResult("策略加价", &lbl_result_strategy_);

    lbl_result_total_ = new QLabel("—");
    lbl_result_total_->setStyleSheet("font-size: 22px; font-weight: bold; color: #f56c6c;");
    result_layout->addRow("总运费：", lbl_result_total_);

    lbl_result_status_ = new QLabel("请输入信息后点击计算");
    lbl_result_status_->setStyleSheet("color: #909399; font-size: 12px;");
    result_layout->addRow("", lbl_result_status_);

    main_layout->addWidget(result_group);

    // 按钮区域
    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    btn_clear_ = new QPushButton(" 重置");
    btn_clear_->setIcon(icons.ActionIcon("cancel"));
    btn_clear_->setObjectName("normalBtn");
    btn_clear_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_clear_);

    btn_calc_ = new QPushButton(" 开始计算");
    btn_calc_->setIcon(icons.ActionIcon("calculate"));
    btn_calc_->setObjectName("primaryBtn");
    btn_calc_->setCursor(Qt::PointingHandCursor);
    btn_calc_->setDefault(true);
    btn_layout->addWidget(btn_calc_);

    btn_close_ = new QPushButton(" 关闭");
    btn_close_->setObjectName("normalBtn");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_close_);

    main_layout->addLayout(btn_layout);

    connect(btn_calc_, &QPushButton::clicked, this, &SingleCalcDialog::OnCalc);
    connect(btn_clear_, &QPushButton::clicked, this, &SingleCalcDialog::OnClear);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);

    setStyleSheet(R"QSS(
QDialog { background-color: #f5f7fa; }
QGroupBox {
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    margin-top: 12px;
    padding-top: 12px;
    font-weight: 500;
    color: #303133;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
}
QLineEdit, QComboBox, QDoubleSpinBox {
    padding: 6px 10px;
    border: 1px solid #dcdfe6;
    border-radius: 4px;
    background: white;
    min-height: 24px;
}
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {
    border-color: #409eff;
}
QPushButton#primaryBtn {
    background-color: #409eff;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
    font-size: 14px;
}
QPushButton#primaryBtn:hover { background-color: #66b1ff; }
QPushButton#primaryBtn:pressed { background-color: #3a8ee6; }

QPushButton#normalBtn {
    background-color: #ffffff;
    color: #606266;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
    padding: 8px 20px;
    font-size: 14px;
}
QPushButton#normalBtn:hover {
    border-color: #409eff;
    color: #409eff;
}
    )QSS");
}

void SingleCalcDialog::OnCalc() {
    QString province = cbo_province_->currentText();
    double weight = spn_weight_->value();
    double vol_weight = spn_vol_weight_->value();
    QString tpl_id = cbo_template_->currentData().toString();

    services::CalcService calc_svc;
    auto result = calc_svc.CalcSingle(province, weight, vol_weight, tpl_id);

    if (result.success) {
        lbl_result_charge_weight_->setText(QString::number(result.charge_weight, 'f', 3) + " kg");
        lbl_result_base_fee_->setText("¥ " + QString::number(result.base_fee, 'f', 2));
        lbl_result_fuel_->setText("¥ " + QString::number(result.fuel_surcharge, 'f', 2));
        lbl_result_strategy_->setText("¥ " + QString::number(result.strategy_surcharge, 'f', 2));
        lbl_result_total_->setText("¥ " + QString::number(result.total_fee, 'f', 2));
        lbl_result_status_->setText("✅ 计算成功");
        lbl_result_status_->setStyleSheet("color: #67c23a; font-size: 12px;");
    } else {
        lbl_result_status_->setText("❌ 计算失败：" + result.error_msg);
        lbl_result_status_->setStyleSheet("color: #f56c6c; font-size: 12px;");
    }
}

void SingleCalcDialog::OnClear() {
    edt_order_id_->clear();
    cbo_province_->setCurrentIndex(0);
    edt_city_->clear();
    spn_weight_->setValue(1.0);
    spn_vol_weight_->setValue(0.0);
    lbl_result_charge_weight_->setText("—");
    lbl_result_base_fee_->setText("—");
    lbl_result_fuel_->setText("—");
    lbl_result_strategy_->setText("—");
    lbl_result_total_->setText("—");
    lbl_result_status_->setText("请输入信息后点击计算");
    lbl_result_status_->setStyleSheet("color: #909399; font-size: 12px;");
}

} // namespace freight::ui::dialogs
