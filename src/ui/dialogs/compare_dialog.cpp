#include "ui/dialogs/compare_dialog.hpp"
#include "services/calc_service.hpp"
#include "ui/icon_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QDebug>

namespace freight::ui::dialogs {

CompareDialog::CompareDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
}

CompareDialog::~CompareDialog() = default;

void CompareDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("对比分析");
    setWindowIcon(icons.CardIcon("compare"));
    resize(700, 500);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    // 输入区域
    auto *input_group = new QGroupBox("对比条件");
    auto *form_layout = new QFormLayout(input_group);
    form_layout->setContentsMargins(20, 20, 20, 20);
    form_layout->setSpacing(12);

    cbo_province_ = new QComboBox();
    cbo_province_->addItems({"江苏","浙江","安徽","上海","山东","广东","福建","北京",
                              "河南","湖北","湖南","江西","天津","河北","山西","广西",
                              "四川","重庆","陕西","贵州","辽宁","吉林","黑龙江","云南",
                              "海南","甘肃","青海","内蒙古","宁夏","新疆","西藏"});
    form_layout->addRow("目的省份：", cbo_province_);

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

    main_layout->addWidget(input_group);

    // 结果表格
    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels({"模板名称", "基础运费", "燃油附加费", "地区加价", "策略加价", "总运费"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    main_layout->addWidget(table_, 1);

    // 按钮
    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    btn_compare_ = new QPushButton(" 开始对比");
    btn_compare_->setIcon(icons.ActionIcon("calculate"));
    btn_compare_->setObjectName("primaryBtn");
    btn_compare_->setCursor(Qt::PointingHandCursor);
    btn_compare_->setDefault(true);
    btn_layout->addWidget(btn_compare_);

    btn_close_ = new QPushButton(" 关闭");
    btn_close_->setObjectName("normalBtn");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_close_);

    main_layout->addLayout(btn_layout);

    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_compare_, &QPushButton::clicked, this, &CompareDialog::OnCompare);

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
QTableWidget {
    border: 1px solid #ebeef5;
    border-radius: 6px;
    gridline-color: #ebeef5;
    background: white;
}
QTableWidget::item { padding: 8px; }
QHeaderView::section {
    background: #f5f7fa;
    padding: 10px 8px;
    border: none;
    border-right: 1px solid #ebeef5;
    border-bottom: 1px solid #ebeef5;
    font-weight: 500;
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

void CompareDialog::OnCompare() {
    QString province = cbo_province_->currentText();
    double weight = spn_weight_->value();
    double vol_weight = spn_vol_weight_->value();

    services::CalcService calc_svc;

    QList<QPair<QString, QString>> templates = {
        {"中通标准快递", "zto_standard"},
    };

    table_->setRowCount(templates.size());

    for (int i = 0; i < templates.size(); i++) {
        QString tpl_name = templates[i].first;
        QString tpl_id = templates[i].second;

        auto result = calc_svc.CalcSingle(province, weight, vol_weight, tpl_id);

        table_->setItem(i, 0, new QTableWidgetItem(tpl_name));

        if (result.success) {
            table_->setItem(i, 1, new QTableWidgetItem("¥ " + QString::number(result.base_fee, 'f', 2)));
            table_->setItem(i, 2, new QTableWidgetItem("¥ " + QString::number(result.fuel_surcharge, 'f', 2)));
            table_->setItem(i, 3, new QTableWidgetItem("¥ " + QString::number(result.remote_surcharge, 'f', 2)));
            table_->setItem(i, 4, new QTableWidgetItem("¥ " + QString::number(result.strategy_surcharge, 'f', 2)));
            auto *total_item = new QTableWidgetItem("¥ " + QString::number(result.total_fee, 'f', 2));
            total_item->setForeground(QBrush(QColor("#f56c6c")));
            total_item->setFont(QFont(table_->font().family(), table_->font().pointSize(), QFont::Bold));
            table_->setItem(i, 5, total_item);
        } else {
            table_->setItem(i, 1, new QTableWidgetItem("-"));
            table_->setItem(i, 2, new QTableWidgetItem("-"));
            table_->setItem(i, 3, new QTableWidgetItem("-"));
            table_->setItem(i, 4, new QTableWidgetItem("-"));
            table_->setItem(i, 5, new QTableWidgetItem("计算失败: " + result.error_msg));
        }
    }
}

} // namespace freight::ui::dialogs
