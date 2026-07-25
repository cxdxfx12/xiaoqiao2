#include "ui/dialogs/template_edit_dialog.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDateEdit>
#include <QDebug>
#include <QSet>
#include <QMap>
#include <QFont>
#include <QWidget>

namespace freight::ui::dialogs {

TemplateEditDialog::TemplateEditDialog(const QString &template_id, QWidget *parent)
    : QDialog(parent), template_id_(template_id) {
    is_new_ = template_id_.isEmpty();
    auto &cfg = core::AppConfig::Instance();
    repo_ = new db::SqliteRuleRepository(cfg.GetRulesDbPath());
    repo_->Init();
    SetupUI();
    LoadData();
}

TemplateEditDialog::~TemplateEditDialog() {
    delete repo_;
}

void TemplateEditDialog::SetTemplateId(const QString &id) {
    template_id_ = id;
    is_new_ = id.isEmpty();
    LoadData();
}

double TemplateEditDialog::CurrentAddUnit() const {
    const double preset = cb_add_unit_->currentData().toDouble();
    return preset > 0 ? preset : spn_add_unit_custom_->value();
}
int TemplateEditDialog::CurrentVolDivisor() const {
    const int preset = cb_vol_divisor_->currentData().toInt();
    return preset > 0 ? preset : spn_vol_divisor_custom_->value();
}
QString TemplateEditDialog::CurrentRoundingMode() const {
    return cb_rounding_mode_->currentData().toString();
}

void TemplateEditDialog::SyncVolDivisorComboFromValue(int value) {
    for (int i = 0; i < cb_vol_divisor_->count(); ++i) {
        if (cb_vol_divisor_->itemData(i).toInt() == value) {
            cb_vol_divisor_->setCurrentIndex(i);
            spn_vol_divisor_custom_->setEnabled(false);
            return;
        }
    }
    int idx = cb_vol_divisor_->findData(-1);
    if (idx >= 0) cb_vol_divisor_->setCurrentIndex(idx);
    spn_vol_divisor_custom_->setValue(value);
    spn_vol_divisor_custom_->setEnabled(true);
}
void TemplateEditDialog::SyncAddUnitComboFromValue(double value) {
    for (int i = 0; i < cb_add_unit_->count(); ++i) {
        const double d = cb_add_unit_->itemData(i).toDouble();
        if (d > 0 && qAbs(d - value) < 0.0001) {
            cb_add_unit_->setCurrentIndex(i);
            spn_add_unit_custom_->setEnabled(false);
            return;
        }
    }
    int idx = cb_add_unit_->findData(-1.0);
    if (idx >= 0) cb_add_unit_->setCurrentIndex(idx);
    spn_add_unit_custom_->setValue(value);
    spn_add_unit_custom_->setEnabled(true);
}
void TemplateEditDialog::SyncRoundingComboFromMode(const QString &mode) {
    for (int i = 0; i < cb_rounding_mode_->count(); ++i) {
        if (cb_rounding_mode_->itemData(i).toString() == mode) {
            cb_rounding_mode_->setCurrentIndex(i);
            return;
        }
    }
    int idx = cb_rounding_mode_->findData("ceil_0_1kg");
    if (idx >= 0) cb_rounding_mode_->setCurrentIndex(idx);
}

void TemplateEditDialog::SetupUI() {
    setWindowTitle(is_new_ ? "新增模板" : "编辑模板");
    resize(1200, 740);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(15, 15, 15, 15);
    main_layout->setSpacing(10);

    // ====== 模板基础信息 ======
    auto *info_group = new QGroupBox("模板信息");
    auto *info_form = new QFormLayout(info_group);

    edt_id_ = new QLineEdit();
    edt_name_ = new QLineEdit();
    edt_carrier_ = new QLineEdit();
    chk_default_ = new QCheckBox("设为默认模板");
    edt_desc_ = new QLineEdit();

    if (is_new_) {
        edt_id_->setPlaceholderText("如 zto_standard");
    } else {
        edt_id_->setReadOnly(true);
    }

    info_form->addRow("模板ID:",   edt_id_);
    info_form->addRow("模板名称:", edt_name_);
    info_form->addRow("承运商:",   edt_carrier_);
    info_form->addRow("",          chk_default_);
    info_form->addRow("描述:",     edt_desc_);

    main_layout->addWidget(info_group);

    // ====== 📐 计费参数（新 GroupBox）======
    auto *calc_group = new QGroupBox("📐 计费参数（绑定本模板的客户将默认继承，客户可单独覆写）");
    auto *calc_form = new QFormLayout(calc_group);

    // ① 首重
    spn_first_weight_ = new QDoubleSpinBox();
    spn_first_weight_->setRange(0.1, 100.0);
    spn_first_weight_->setSingleStep(0.5);
    spn_first_weight_->setDecimals(3);
    spn_first_weight_->setSuffix(" kg");
    calc_form->addRow("首重:", spn_first_weight_);

    // ② 续重进位（全新）
    cb_rounding_mode_ = new QComboBox();
    cb_rounding_mode_->addItem("0.1kg 进一（国标推荐 · 极兔/韵达 2024 公示版）", "ceil_0_1kg");
    cb_rounding_mode_->addItem("1kg 进一（通达系网点传统默认）",             "ceil_1kg");
    cb_rounding_mode_->addItem("0.5kg 进一（顺丰特惠 / EMS 国际件）",          "ceil_0_5kg");
    cb_rounding_mode_->addItem("0.1kg 四舍五入（小包专线 · 罕见）",            "round_0_1kg");
    cb_rounding_mode_->addItem("不进位 · 按实际小数（按 kg 精确计费）",         "floor_no_round");
    calc_form->addRow("续重进位规则:", cb_rounding_mode_);

    // ③ 续重单位（preset + 自定义）
    cb_add_unit_ = new QComboBox();
    cb_add_unit_->addItem("1.0 kg（90% 客户 · 最常用）", 1.0);
    cb_add_unit_->addItem("0.5 kg（电商中件 · 顺丰特惠）", 0.5);
    cb_add_unit_->addItem("0.1 kg（精确到 100g · 顺丰标准 / 极兔）", 0.1);
    cb_add_unit_->addItem("自定义 (小数 kg)…", -1.0);
    spn_add_unit_custom_ = new QDoubleSpinBox();
    spn_add_unit_custom_->setRange(0.01, 5.0);
    spn_add_unit_custom_->setDecimals(3);
    spn_add_unit_custom_->setSingleStep(0.05);
    spn_add_unit_custom_->setSuffix(" kg");
    spn_add_unit_custom_->setEnabled(false);
    auto *add_unit_row = new QHBoxLayout();
    add_unit_row->setContentsMargins(0,0,0,0);
    add_unit_row->setSpacing(6);
    add_unit_row->addWidget(cb_add_unit_, 3);
    add_unit_row->addWidget(spn_add_unit_custom_, 2);
    auto *add_unit_wrap = new QWidget();
    add_unit_wrap->setLayout(add_unit_row);
    calc_form->addRow("续重单位:", add_unit_wrap);
    connect(cb_add_unit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](){
        const bool custom = cb_add_unit_->currentData().toDouble() < 0;
        spn_add_unit_custom_->setEnabled(custom);
    });

    // ④ 体积重除数（preset + 自定义）
    cb_vol_divisor_ = new QComboBox();
    cb_vol_divisor_->addItem("6000（普通快递 / 顺丰标准 · 最常用）", 6000);
    cb_vol_divisor_->addItem("5000（大客抛比 · 小体积轻货更贵）",   5000);
    cb_vol_divisor_->addItem("8000（快运 / 中通快运 · 大包划算）",  8000);
    cb_vol_divisor_->addItem("4800（德邦零担 / 精准卡航）",         4800);
    cb_vol_divisor_->addItem("12000（顺丰重货 / 中铁快运）",        12000);
    cb_vol_divisor_->addItem("自定义…", -1);
    spn_vol_divisor_custom_ = new QSpinBox();
    spn_vol_divisor_custom_->setRange(1000, 20000);
    spn_vol_divisor_custom_->setSingleStep(100);
    spn_vol_divisor_custom_->setSuffix(" cm³/kg");
    spn_vol_divisor_custom_->setEnabled(false);
    auto *vol_div_row = new QHBoxLayout();
    vol_div_row->setContentsMargins(0,0,0,0);
    vol_div_row->setSpacing(6);
    vol_div_row->addWidget(cb_vol_divisor_, 3);
    vol_div_row->addWidget(spn_vol_divisor_custom_, 2);
    auto *vol_div_wrap = new QWidget();
    vol_div_wrap->setLayout(vol_div_row);
    auto *vol_hint = new QLabel("<span style='color:#909399;font-size:12px;'>公式：长(cm)×宽(cm)×高(cm) ÷ 除数 = 体积重量(kg)。取(实重, 体积重)较大值计费</span>");
    vol_hint->setWordWrap(true);
    auto *vol_vbox = new QVBoxLayout();
    vol_vbox->setContentsMargins(0,0,0,0);
    vol_vbox->setSpacing(2);
    vol_vbox->addWidget(vol_div_wrap);
    vol_vbox->addWidget(vol_hint);
    auto *vol_vbox_wrap = new QWidget(); vol_vbox_wrap->setLayout(vol_vbox);
    calc_form->addRow("体积重除数:", vol_vbox_wrap);
    connect(cb_vol_divisor_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](){
        const bool custom = cb_vol_divisor_->currentData().toInt() < 0;
        spn_vol_divisor_custom_->setEnabled(custom);
    });

    // ⑤ 无重量默认运费
    spn_no_weight_fee_ = new QDoubleSpinBox();
    spn_no_weight_fee_->setRange(0, 9999);
    spn_no_weight_fee_->setSingleStep(0.5);
    spn_no_weight_fee_->setDecimals(2);
    spn_no_weight_fee_->setPrefix("¥ ");
    calc_form->addRow("无重量面单默认运费:", spn_no_weight_fee_);

    main_layout->addWidget(calc_group);

    // ====== Tab 页：阶梯价格 / 分区省份 / 燃油附加费 ======
    auto *tab_widget = new QTabWidget();

    // --- 阶梯价格（矩阵布局，按用户格式） ---
    auto *pricing_tab = new QWidget();
    auto *pricing_layout = new QVBoxLayout(pricing_tab);
    pricing_table_ = new QTableWidget(0, 10);
    pricing_table_->setHorizontalHeaderLabels(
        {"报价区域", "目的省份", "0-0.5KG", "0.51KG-1KG", "1-2KG", "2-3KG",
         "3-30KG\n首重", "3-30KG\n续重", "30KG以上\n首重", "30KG以上\n续重"});
    pricing_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pricing_table_->verticalHeader()->setVisible(false);
    pricing_table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    pricing_table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    // 表头样式
    pricing_table_->horizontalHeader()->setMinimumHeight(40);
    pricing_table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignCenter);
    for (int i = 0; i < 10; i++) {
        if (auto *h = pricing_table_->horizontalHeaderItem(i))
            h->setTextAlignment(Qt::AlignCenter);
    }
    pricing_layout->addWidget(pricing_table_);

    auto *pricing_btn_layout = new QHBoxLayout();
    auto *btn_save_pricing = new QPushButton(" 保存价格修改");
    btn_save_pricing->setCursor(Qt::PointingHandCursor);
    pricing_btn_layout->addStretch();
    pricing_btn_layout->addWidget(btn_save_pricing);
    pricing_layout->addLayout(pricing_btn_layout);

    tab_widget->addTab(pricing_tab, "阶梯价格");

    // --- 分区省份 ---
    auto *zone_tab = new QWidget();
    auto *zone_layout = new QVBoxLayout(zone_tab);
    zone_table_ = new QTableWidget(0, 3);
    zone_table_->setHorizontalHeaderLabels({"分区代码", "分区名称", "包含省份(逗号分隔)"});
    zone_table_->horizontalHeader()->setStretchLastSection(true);
    zone_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    zone_table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    zone_layout->addWidget(zone_table_);
    tab_widget->addTab(zone_tab, "分区省份");

    // --- 燃油附加费 ---
    auto *fuel_tab = new QWidget();
    auto *fuel_layout = new QVBoxLayout(fuel_tab);
    fuel_table_ = new QTableWidget(0, 2);
    fuel_table_->setHorizontalHeaderLabels({"生效日期", "费率"});
    fuel_table_->horizontalHeader()->setStretchLastSection(true);
    fuel_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fuel_table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    fuel_layout->addWidget(fuel_table_);
    tab_widget->addTab(fuel_tab, "燃油附加费");

    main_layout->addWidget(tab_widget);

    // ====== 底部按钮 ======
    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    auto *btn_cancel = new QPushButton(" 取消");
    btn_cancel->setCursor(Qt::PointingHandCursor);
    auto *btn_save = new QPushButton(" 保存");
    btn_save->setCursor(Qt::PointingHandCursor);
    btn_save->setObjectName("primaryBtn");

    btn_layout->addWidget(btn_cancel);
    btn_layout->addWidget(btn_save);
    main_layout->addLayout(btn_layout);

    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_save, &QPushButton::clicked, this, &TemplateEditDialog::OnSave);
    connect(btn_save_pricing, &QPushButton::clicked, this, &TemplateEditDialog::OnSavePricing);
    connect(pricing_table_, &QTableWidget::cellChanged, this, &TemplateEditDialog::OnPricingCellChanged);

    setStyleSheet(R"QSS(
QDialog { background-color: #f5f7fa; }
QGroupBox {
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    margin-top: 10px;
    padding-top: 10px;
    background: white;
}
QGroupBox::title {
    left: 12px;
    subcontrol-origin: margin;
    padding: 0 4px;
}
QTabWidget::pane {
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    background: white;
    top: -1px;
}
QTabBar::tab {
    padding: 8px 20px;
    border: 1px solid #e4e7ed;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    background: #f5f7fa;
    margin-right: 4px;
}
QTabBar::tab:selected {
    background: white;
    color: #409eff;
}
QTableWidget {
    border: 1px solid #ebeef5;
    border-radius: 6px;
    gridline-color: #ebeef5;
    background: white;
}
QTableWidget::item { padding: 4px; }
QHeaderView::section {
    background: #f5f7fa;
    padding: 8px 6px;
    border: none;
    border-right: 1px solid #ebeef5;
    border-bottom: 1px solid #ebeef5;
    font-weight: 500;
}
QPushButton {
    padding: 6px 14px;
    border-radius: 4px;
    border: 1px solid #dcdfe6;
    background: white;
}
QPushButton:hover {
    border-color: #409eff;
    color: #409eff;
}
QPushButton#primaryBtn {
    background-color: #409eff;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
}
QPushButton#primaryBtn:hover { background-color: #66b1ff; }
    )QSS");
}

void TemplateEditDialog::LoadData() {
    if (is_new_) {
        spn_first_weight_->setValue(1.0);
        SyncRoundingComboFromMode("ceil_0_1kg");
        SyncAddUnitComboFromValue(1.0);
        SyncVolDivisorComboFromValue(6000);
        spn_no_weight_fee_->setValue(0.0);
        return;
    }

    // 加载模板基本信息
    auto tpl = repo_->GetTemplate(template_id_);
    if (tpl.isEmpty()) {
        QMessageBox::warning(this, "错误", "模板不存在: " + template_id_);
        return;
    }

    edt_id_->setText(tpl["template_id"].toString());
    edt_name_->setText(tpl["template_name"].toString());
    edt_carrier_->setText(tpl["carrier_name"].toString());
    chk_default_->setChecked(tpl["is_default"].toBool());
    edt_desc_->setText(tpl["description"].toString());

    spn_first_weight_->setValue(tpl["first_weight"].toDouble());
    spn_no_weight_fee_->setValue(tpl.value("default_no_weight_fee", 0).toDouble());

    // --- 续重单位：优先读新字段 tpl_additional_unit，否则 fallback 老字段 additional_unit
    bool ok_add = false;
    double add_unit = tpl.value("tpl_additional_unit").toDouble(&ok_add);
    if (!ok_add || add_unit <= 0) add_unit = tpl["additional_unit"].toDouble();
    if (add_unit <= 0) add_unit = 1.0;
    SyncAddUnitComboFromValue(add_unit);

    // --- 体积重除数：优先 tpl_vol_divisor (int)，否则 fallback 老 vol_weight_ratio (double)
    bool ok_vol = false;
    int vol_div = tpl.value("tpl_vol_divisor").toInt(&ok_vol);
    if (!ok_vol || vol_div <= 0) {
        double v = tpl["vol_weight_ratio"].toDouble();
        vol_div = v > 1 ? static_cast<int>(v) : 6000;
    }
    SyncVolDivisorComboFromValue(vol_div);

    // --- 续重进位：只在新字段 tpl_rounding_mode 有值，否则默认 ceil_0_1kg (国标推荐)
    QString rounding = tpl.value("tpl_rounding_mode").toString().trimmed();
    if (rounding.isEmpty()) rounding = "ceil_0_1kg";
    SyncRoundingComboFromMode(rounding);

    // 加载阶梯价格 - 矩阵布局（每行一个省份，列为重量区间）
    loading_data_ = true;

    QSqlQuery q(repo_->Database());
    q.prepare("SELECT group_code, group_name, sort_order FROM zone_groups "
              "WHERE template_id = ? ORDER BY sort_order");
    q.addBindValue(template_id_);
    q.exec();

    pricing_table_->setRowCount(0);
    int row = 0;

    while (q.next()) {
        QString group_code = q.value(0).toString();
        QString group_name = q.value(1).toString();

        // 获取该分区的省份列表
        QSqlQuery q2(repo_->Database());
        q2.prepare("SELECT province FROM zone_group_provinces WHERE template_id = ? AND group_code = ? ORDER BY province");
        q2.addBindValue(template_id_);
        q2.addBindValue(group_code);
        q2.exec();

        QStringList provinces;
        while (q2.next()) {
            provinces << q2.value(0).toString();
        }

        // 获取该分区的阶梯价格
        QSqlQuery q3(repo_->Database());
        q3.prepare("SELECT tier_code, first_price, additional_price FROM tiered_pricing "
                   "WHERE template_id = ? AND group_code = ? ORDER BY sort_order");
        q3.addBindValue(template_id_);
        q3.addBindValue(group_code);
        q3.exec();

        QMap<QString, QPair<double, double>> tier_prices; // tier_code -> (first_price, add_price)
        while (q3.next()) {
            tier_prices[q3.value(0).toString()] = qMakePair(q3.value(1).toDouble(), q3.value(2).toDouble());
        }

        int zone_start_row = row;
        for (int i = 0; i < provinces.size(); i++) {
            pricing_table_->insertRow(row);

            // 报价区域（只在第一行显示名称，其他行留空用于合并）
            auto *zone_item = new QTableWidgetItem(i == 0 ? group_name : "");
            zone_item->setData(Qt::UserRole, group_code);
            zone_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            zone_item->setTextAlignment(Qt::AlignCenter);
            zone_item->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
            pricing_table_->setItem(row, 0, zone_item);

            // 目的省份
            auto *prov_item = new QTableWidgetItem(provinces[i]);
            prov_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            prov_item->setTextAlignment(Qt::AlignCenter);
            pricing_table_->setItem(row, 1, prov_item);

            // 价格列
            auto setPrice = [&](int col, const QString &tier_code, bool is_add = false) {
                auto it = tier_prices.find(tier_code);
                if (it != tier_prices.end()) {
                    double val = is_add ? it.value().second : it.value().first;
                    auto *item = new QTableWidgetItem(QString::number(val, 'f', 2));
                    item->setTextAlignment(Qt::AlignCenter);
                    pricing_table_->setItem(row, col, item);
                }
            };

            setPrice(2, "tier_0_0.5");
            setPrice(3, "tier_0.5_1");
            setPrice(4, "tier_1_2");
            setPrice(5, "tier_2_3");
            setPrice(6, "tier_3_30");         // 3-30KG 首重
            setPrice(7, "tier_3_30", true);   // 3-30KG 续重
            setPrice(8, "tier_30_plus");       // 30KG以上 首重
            setPrice(9, "tier_30_plus", true); // 30KG以上 续重

            row++;
        }

        // 合并报价区域单元格
        if (provinces.size() > 1) {
            pricing_table_->setSpan(zone_start_row, 0, provinces.size(), 1);
        }
    }

    loading_data_ = false;

    // 加载分区省份
    q.prepare("SELECT group_code, group_name FROM zone_groups WHERE template_id = ? ORDER BY sort_order");
    q.addBindValue(template_id_);
    q.exec();

    zone_table_->setRowCount(0);
    row = 0;
    while (q.next()) {
        QString gcode = q.value(0).toString();
        QString gname = q.value(1).toString();

        // 查该分区的省份
        QSqlQuery q2(repo_->Database());
        q2.prepare("SELECT province FROM zone_group_provinces WHERE template_id = ? AND group_code = ?");
        q2.addBindValue(template_id_);
        q2.addBindValue(gcode);
        q2.exec();

        QStringList provinces;
        while (q2.next()) {
            provinces << q2.value(0).toString();
        }

        zone_table_->insertRow(row);
        zone_table_->setItem(row, 0, new QTableWidgetItem(gcode));
        zone_table_->setItem(row, 1, new QTableWidgetItem(gname));
        zone_table_->setItem(row, 2, new QTableWidgetItem(provinces.join(",")));
        // 分区代码只读
        zone_table_->item(row, 0)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        row++;
    }

    // 加载燃油附加费
    q.prepare("SELECT effective_date, rate FROM fuel_surcharge WHERE template_id = ? ORDER BY effective_date DESC");
    q.addBindValue(template_id_);
    q.exec();

    fuel_table_->setRowCount(0);
    row = 0;
    while (q.next()) {
        fuel_table_->insertRow(row);
        fuel_table_->setItem(row, 0, new QTableWidgetItem(q.value(0).toString()));
        fuel_table_->setItem(row, 1, new QTableWidgetItem(QString::number(q.value(1).toDouble(), 'f', 4)));
        row++;
    }
}

void TemplateEditDialog::OnSave() {
    if (edt_name_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入模板名称");
        return;
    }

    QVariantMap tpl;
    QString tid = is_new_ ? edt_id_->text().trimmed() : template_id_;
    if (tid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入模板ID");
        return;
    }

    tpl["template_id"] = tid;
    tpl["template_name"] = edt_name_->text().trimmed();
    tpl["carrier_name"] = edt_carrier_->text().trimmed();
    tpl["first_weight"] = spn_first_weight_->value();
    // 新旧两列都写，保证向后兼容
    const double add_unit = CurrentAddUnit();
    const int vol_divisor = CurrentVolDivisor();
    tpl["additional_unit"]      = add_unit;              // 老列兼容
    tpl["tpl_additional_unit"]  = add_unit;              // 新列 (schema 14)
    tpl["vol_weight_ratio"]     = static_cast<double>(vol_divisor); // 老列兼容
    tpl["tpl_vol_divisor"]      = vol_divisor;           // 新列 (schema 14)
    tpl["tpl_rounding_mode"]    = CurrentRoundingMode(); // 新列 (schema 14)
    tpl["default_no_weight_fee"] = spn_no_weight_fee_->value();
    tpl["description"] = edt_desc_->text().trimmed();

    bool ok;
    if (is_new_) {
        ok = repo_->AddTemplate(tpl);
        if (ok && chk_default_->isChecked()) {
            QSqlQuery q(repo_->Database());
            q.exec("UPDATE freight_templates SET is_default = 0");
            q.prepare("UPDATE freight_templates SET is_default = 1 WHERE template_id = ?");
            q.addBindValue(tid);
            q.exec();
        }
    } else {
        ok = repo_->UpdateTemplate(tpl);
        if (ok && chk_default_->isChecked()) {
            QSqlQuery q(repo_->Database());
            q.exec("UPDATE freight_templates SET is_default = 0");
            q.prepare("UPDATE freight_templates SET is_default = 1 WHERE template_id = ?");
            q.addBindValue(tid);
            q.exec();
        }
    }

    if (ok) {
        // 同时保存价格修改
        OnSavePricing();
        QMessageBox::information(this, "成功", "模板保存成功");
        accept();
    } else {
        QMessageBox::critical(this, "错误", "保存失败，请检查输入");
    }
}

void TemplateEditDialog::OnSavePricing() {
    if (is_new_) {
        QMessageBox::warning(this, "提示", "请先保存模板基本信息");
        return;
    }

    auto *editor = pricing_table_->indexWidget(pricing_table_->currentIndex());
    if (editor) {
        pricing_table_->itemDelegate()->commitData(editor);
        pricing_table_->itemDelegate()->closeEditor(editor, QAbstractItemDelegate::NoHint);
    }

    QSqlDatabase db = repo_->Database();
    if (!db.transaction()) {
        QMessageBox::critical(this, "错误", "无法启动事务: " + db.lastError().text());
        return;
    }

    bool success = true;
    QString error_msg;
    int total_updated = 0;
    QSet<QString> processed_zones;

    for (int row = 0; row < pricing_table_->rowCount(); row++) {
        auto *zone_item = pricing_table_->item(row, 0);
        if (!zone_item) continue;
        QString group_code = zone_item->data(Qt::UserRole).toString();
        if (group_code.isEmpty() || processed_zones.contains(group_code)) continue;
        processed_zones.insert(group_code);

        bool ok_p05, ok_p1, ok_p2, ok_p3, ok_midf, ok_mida, ok_bigf, ok_biga;
        double p05 = pricing_table_->item(row, 2)->text().toDouble(&ok_p05);
        double p1 = pricing_table_->item(row, 3)->text().toDouble(&ok_p1);
        double p2 = pricing_table_->item(row, 4)->text().toDouble(&ok_p2);
        double p3 = pricing_table_->item(row, 5)->text().toDouble(&ok_p3);
        double mid_first = pricing_table_->item(row, 6)->text().toDouble(&ok_midf);
        double mid_add = pricing_table_->item(row, 7)->text().toDouble(&ok_mida);
        double big_first = pricing_table_->item(row, 8)->text().toDouble(&ok_bigf);
        double big_add = pricing_table_->item(row, 9)->text().toDouble(&ok_biga);

        if (!ok_p05 || !ok_p1 || !ok_p2 || !ok_p3 || !ok_midf || !ok_mida || !ok_bigf || !ok_biga) {
            error_msg = QString("第%1行价格格式不正确").arg(row + 1);
            success = false;
            break;
        }

        QVector<QPair<QString, QPair<double, double>>> tiers = {
            {"tier_0_0.5", {p05, 0.0}},
            {"tier_0.5_1", {p1, 0.0}},
            {"tier_1_2", {p2, 0.0}},
            {"tier_2_3", {p3, 0.0}},
            {"tier_3_30", {mid_first, mid_add}},
            {"tier_30_plus", {big_first, big_add}},
        };

        for (const auto &tier : tiers) {
            QSqlQuery q(db);
            q.prepare("UPDATE tiered_pricing SET first_price=?, additional_price=? "
                      "WHERE template_id=? AND group_code=? AND tier_code=?");
            q.addBindValue(tier.second.first);
            q.addBindValue(tier.second.second);
            q.addBindValue(template_id_);
            q.addBindValue(group_code);
            q.addBindValue(tier.first);

            if (!q.exec()) {
                error_msg = "保存失败: " + q.lastError().text();
                success = false;
                break;
            }

            if (q.numRowsAffected() == 0) {
                QSqlQuery q2(db);
                q2.prepare("INSERT INTO tiered_pricing "
                           "(template_id, group_code, tier_code, tier_name, "
                           " min_weight, max_weight, first_price, additional_price, sort_order) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
                q2.addBindValue(template_id_);
                q2.addBindValue(group_code);
                q2.addBindValue(tier.first);
                q2.addBindValue("");
                q2.addBindValue(0);
                q2.addBindValue(0);
                q2.addBindValue(tier.second.first);
                q2.addBindValue(tier.second.second);
                q2.addBindValue(0);

                if (!q2.exec()) {
                    error_msg = "插入失败: " + q2.lastError().text();
                    success = false;
                    break;
                }
                total_updated++;
            } else {
                total_updated += q.numRowsAffected();
            }
        }

        if (!success) break;
    }

    if (success) {
        if (!db.commit()) {
            error_msg = "提交事务失败: " + db.lastError().text();
            success = false;
        }
    }

    if (!success) {
        db.rollback();
        QMessageBox::critical(this, "错误", error_msg);
        return;
    }

    QMessageBox::information(this, "成功", QString("报价表已保存，共更新 %1 条记录").arg(total_updated));
}

void TemplateEditDialog::OnPricingCellChanged(int row, int col) {
    if (loading_data_) return;
    if (col < 2 || col >= 10) return; // 只同步价格列（2~9）

    auto *zone_item = pricing_table_->item(row, 0);
    if (!zone_item) return;
    QString group_code = zone_item->data(Qt::UserRole).toString();
    if (group_code.isEmpty()) return;

    auto *changed_item = pricing_table_->item(row, col);
    if (!changed_item) return;
    QString new_value = changed_item->text();

    // 同步同分区其他行的价格
    loading_data_ = true;
    for (int r = 0; r < pricing_table_->rowCount(); r++) {
        if (r == row) continue;
        auto *zi = pricing_table_->item(r, 0);
        if (!zi) continue;
        if (zi->data(Qt::UserRole).toString() == group_code) {
            auto *price_item = pricing_table_->item(r, col);
            if (price_item) {
                price_item->setText(new_value);
            }
        }
    }
    loading_data_ = false;
}

void TemplateEditDialog::OnSaveZones() {
    // 保存分区省份修改
    if (is_new_) return;

    QSqlQuery q(repo_->Database());
    for (int row = 0; row < zone_table_->rowCount(); row++) {
        QString gcode = zone_table_->item(row, 0)->text();
        QString gname = zone_table_->item(row, 1)->text();
        QString provinces_str = zone_table_->item(row, 2)->text();

        // 更新分区名称
        q.prepare("UPDATE zone_groups SET group_name=? WHERE template_id=? AND group_code=?");
        q.addBindValue(gname);
        q.addBindValue(template_id_);
        q.addBindValue(gcode);
        q.exec();

        // 更新省份：先删后插
        q.prepare("DELETE FROM zone_group_provinces WHERE template_id=? AND group_code=?");
        q.addBindValue(template_id_);
        q.addBindValue(gcode);
        q.exec();

        QStringList provinces = provinces_str.split(",", Qt::SkipEmptyParts);
        for (const auto &p : provinces) {
            q.prepare("INSERT INTO zone_group_provinces (template_id, group_code, province) VALUES (?,?,?)");
            q.addBindValue(template_id_);
            q.addBindValue(gcode);
            q.addBindValue(p.trimmed());
            q.exec();
        }
    }
}

void TemplateEditDialog::OnSaveFuel() {
    if (is_new_) return;

    // 先删后插
    QSqlQuery q(repo_->Database());
    q.prepare("DELETE FROM fuel_surcharge WHERE template_id=?");
    q.addBindValue(template_id_);
    q.exec();

    for (int row = 0; row < fuel_table_->rowCount(); row++) {
        QString date = fuel_table_->item(row, 0)->text();
        double rate = fuel_table_->item(row, 1)->text().toDouble();

        q.prepare("INSERT INTO fuel_surcharge (template_id, effective_date, rate) VALUES (?,?,?)");
        q.addBindValue(template_id_);
        q.addBindValue(date);
        q.addBindValue(rate);
        q.exec();
    }
}

} // namespace freight::ui::dialogs
