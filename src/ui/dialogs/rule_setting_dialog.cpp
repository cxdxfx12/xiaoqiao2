#include "ui/dialogs/rule_setting_dialog.hpp"
#include "ui/dialogs/template_edit_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "services/rule_service.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QRandomGenerator>

namespace freight::ui::dialogs {

RuleSettingDialog::RuleSettingDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
    LoadData();
}

RuleSettingDialog::~RuleSettingDialog() = default;

void RuleSettingDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("规则设置");
    setWindowIcon(icons.SettingIcon("rule_setting"));
    resize(850, 600);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    tab_widget_ = new QTabWidget();

    // ====== 运费模板 ======
    auto *tpl_tab = new QWidget();
    auto *tpl_layout = new QVBoxLayout(tpl_tab);
    tpl_table_ = new QTableWidget(0, 4);
    tpl_table_->setHorizontalHeaderLabels({"模板ID", "模板名称", "承运商", "默认"});
    tpl_table_->horizontalHeader()->setStretchLastSection(true);
    tpl_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tpl_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tpl_layout->addWidget(tpl_table_);

    auto *tpl_btn_layout = new QHBoxLayout();
    auto *btn_add_tpl = new QPushButton(" 新增模板");
    btn_add_tpl->setIcon(icons.ActionIcon("add"));
    btn_add_tpl->setCursor(Qt::PointingHandCursor);
    auto *btn_edit_tpl = new QPushButton(" 编辑");
    btn_edit_tpl->setCursor(Qt::PointingHandCursor);
    auto *btn_del_tpl = new QPushButton(" 删除");
    btn_del_tpl->setIcon(icons.ActionIcon("delete"));
    btn_del_tpl->setCursor(Qt::PointingHandCursor);
    tpl_btn_layout->addWidget(btn_add_tpl);
    tpl_btn_layout->addWidget(btn_edit_tpl);
    tpl_btn_layout->addWidget(btn_del_tpl);
    tpl_btn_layout->addStretch();
    tpl_layout->addLayout(tpl_btn_layout);

    tab_widget_->addTab(tpl_tab, "运费模板");

    connect(btn_add_tpl, &QPushButton::clicked, this, [this]() {
        TemplateEditDialog dlg("", this);
        if (dlg.exec() == QDialog::Accepted) {
            LoadData();
        }
    });
    connect(btn_edit_tpl, &QPushButton::clicked, this, [this]() {
        if (tpl_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一个模板");
            return;
        }
        QString tid = tpl_table_->item(tpl_table_->currentRow(), 0)->text();
        TemplateEditDialog dlg(tid, this);
        if (dlg.exec() == QDialog::Accepted) {
            LoadData();
        }
    });
    connect(btn_del_tpl, &QPushButton::clicked, this, [this]() {
        if (tpl_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一个模板");
            return;
        }
        QString tid = tpl_table_->item(tpl_table_->currentRow(), 0)->text();
        auto ret = QMessageBox::question(this, "确认删除",
            QString("确定要删除模板 \"%1\" 吗？\n关联的分区、阶梯价格等数据也会一并删除。").arg(tid),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            auto &cfg = core::AppConfig::Instance();
            db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
            repo.Init();
            repo.DeleteTemplate(tid);
            LoadData();
        }
    });

    // ====== 加价策略 ======
    auto *surcharge_tab = new QWidget();
    auto *surcharge_layout = new QVBoxLayout(surcharge_tab);
    surcharge_table_ = new QTableWidget(0, 7);
    surcharge_table_->setHorizontalHeaderLabels({"ID", "策略名称", "范围", "类型", "金额/比例", "优先级", "启用"});
    surcharge_table_->setColumnHidden(0, true);
    surcharge_table_->horizontalHeader()->setStretchLastSection(true);
    surcharge_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    surcharge_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    surcharge_layout->addWidget(surcharge_table_);

    auto *s_btn_layout = new QHBoxLayout();
    auto *btn_add_s = new QPushButton(" 新增策略");
    btn_add_s->setIcon(icons.ActionIcon("add"));
    btn_add_s->setCursor(Qt::PointingHandCursor);
    auto *btn_edit_s = new QPushButton(" 编辑");
    btn_edit_s->setCursor(Qt::PointingHandCursor);
    auto *btn_del_s = new QPushButton(" 删除");
    btn_del_s->setIcon(icons.ActionIcon("delete"));
    btn_del_s->setCursor(Qt::PointingHandCursor);
    s_btn_layout->addWidget(btn_add_s);
    s_btn_layout->addWidget(btn_edit_s);
    s_btn_layout->addWidget(btn_del_s);
    s_btn_layout->addStretch();
    surcharge_layout->addLayout(s_btn_layout);

    tab_widget_->addTab(surcharge_tab, "加价策略");

    connect(btn_add_s, &QPushButton::clicked, this, [this]() {
        ShowSurchargeDialog(true);
    });
    connect(btn_edit_s, &QPushButton::clicked, this, [this]() {
        if (surcharge_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条策略");
            return;
        }
        ShowSurchargeDialog(false);
    });
    connect(btn_del_s, &QPushButton::clicked, this, [this]() {
        if (surcharge_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条策略");
            return;
        }
        QString sid = surcharge_table_->item(surcharge_table_->currentRow(), 0)->text();
        QString sname = surcharge_table_->item(surcharge_table_->currentRow(), 1)->text();
        auto ret = QMessageBox::question(this, "确认删除",
            QString("确定要删除策略 \"%1\" 吗？").arg(sname),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            auto &cfg = core::AppConfig::Instance();
            db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
            repo.Init();
            repo.DeleteSurchargeStrategy(sid);
            LoadData();
        }
    });

    // ====== 燃油附加费 ======
    auto *fuel_tab = new QWidget();
    auto *fuel_layout = new QVBoxLayout(fuel_tab);
    fuel_table_ = new QTableWidget(0, 5);
    fuel_table_->setHorizontalHeaderLabels({"ID", "生效日期", "费率(%)", "模板", "启用"});
    fuel_table_->setColumnHidden(0, true);
    fuel_table_->horizontalHeader()->setStretchLastSection(true);
    fuel_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fuel_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fuel_layout->addWidget(fuel_table_);

    auto *fuel_btn_layout = new QHBoxLayout();
    auto *btn_add_fuel = new QPushButton(" 新增");
    btn_add_fuel->setIcon(icons.ActionIcon("add"));
    btn_add_fuel->setCursor(Qt::PointingHandCursor);
    auto *btn_edit_fuel = new QPushButton(" 编辑");
    btn_edit_fuel->setCursor(Qt::PointingHandCursor);
    auto *btn_del_fuel = new QPushButton(" 删除");
    btn_del_fuel->setIcon(icons.ActionIcon("delete"));
    btn_del_fuel->setCursor(Qt::PointingHandCursor);
    fuel_btn_layout->addWidget(btn_add_fuel);
    fuel_btn_layout->addWidget(btn_edit_fuel);
    fuel_btn_layout->addWidget(btn_del_fuel);
    fuel_btn_layout->addStretch();
    fuel_layout->addLayout(fuel_btn_layout);

    tab_widget_->addTab(fuel_tab, "燃油附加费");

    connect(btn_add_fuel, &QPushButton::clicked, this, [this]() {
        ShowFuelDialog(true);
    });
    connect(btn_edit_fuel, &QPushButton::clicked, this, [this]() {
        if (fuel_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条记录");
            return;
        }
        ShowFuelDialog(false);
    });
    connect(btn_del_fuel, &QPushButton::clicked, this, [this]() {
        if (fuel_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条记录");
            return;
        }
        int id = fuel_table_->item(fuel_table_->currentRow(), 0)->text().toInt();
        auto ret = QMessageBox::question(this, "确认删除", "确定要删除该燃油附加费记录吗？",
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            auto &cfg = core::AppConfig::Instance();
            db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
            repo.Init();
            repo.DeleteFuelSurcharge(id);
            LoadData();
        }
    });

    // ====== 偏远地区 ======
    auto *remote_tab = new QWidget();
    auto *remote_layout = new QVBoxLayout(remote_tab);
    remote_table_ = new QTableWidget(0, 6);
    remote_table_->setHorizontalHeaderLabels({"ID", "省份", "城市", "区县", "附加费(元)", "启用"});
    remote_table_->setColumnHidden(0, true);
    remote_table_->horizontalHeader()->setStretchLastSection(true);
    remote_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    remote_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    remote_layout->addWidget(remote_table_);

    auto *remote_btn_layout = new QHBoxLayout();
    auto *btn_add_remote = new QPushButton(" 新增");
    btn_add_remote->setIcon(icons.ActionIcon("add"));
    btn_add_remote->setCursor(Qt::PointingHandCursor);
    auto *btn_edit_remote = new QPushButton(" 编辑");
    btn_edit_remote->setCursor(Qt::PointingHandCursor);
    auto *btn_del_remote = new QPushButton(" 删除");
    btn_del_remote->setIcon(icons.ActionIcon("delete"));
    btn_del_remote->setCursor(Qt::PointingHandCursor);
    remote_btn_layout->addWidget(btn_add_remote);
    remote_btn_layout->addWidget(btn_edit_remote);
    remote_btn_layout->addWidget(btn_del_remote);
    remote_btn_layout->addStretch();
    remote_layout->addLayout(remote_btn_layout);

    tab_widget_->addTab(remote_tab, "偏远地区");

    connect(btn_add_remote, &QPushButton::clicked, this, [this]() {
        ShowRemoteDialog(true);
    });
    connect(btn_edit_remote, &QPushButton::clicked, this, [this]() {
        if (remote_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条记录");
            return;
        }
        ShowRemoteDialog(false);
    });
    connect(btn_del_remote, &QPushButton::clicked, this, [this]() {
        if (remote_table_->currentRow() < 0) {
            QMessageBox::warning(this, "提示", "请先选择一条记录");
            return;
        }
        int id = remote_table_->item(remote_table_->currentRow(), 0)->text().toInt();
        auto ret = QMessageBox::question(this, "确认删除", "确定要删除该偏远地区记录吗？",
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            auto &cfg = core::AppConfig::Instance();
            db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
            repo.Init();
            repo.DeleteRemoteArea(id);
            LoadData();
        }
    });

    connect(fuel_table_, &QTableWidget::cellClicked, this, &RuleSettingDialog::OnFuelItemClicked);
    connect(remote_table_, &QTableWidget::cellClicked, this, &RuleSettingDialog::OnRemoteItemClicked);

    main_layout->addWidget(tab_widget_);

    // 底部按钮
    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    btn_close_ = new QPushButton(" 关闭");
    btn_close_->setObjectName("normalBtn");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_close_);

    btn_apply_ = new QPushButton(" 应用");
    btn_apply_->setObjectName("primaryBtn");
    btn_apply_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_apply_);

    main_layout->addLayout(btn_layout);

    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_apply_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "提示", "规则已应用到内存，下次计算将使用新规则");
    });

    setStyleSheet(R"QSS(
QDialog { background-color: #f5f7fa; }
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
QTableWidget::item { padding: 6px; }
QHeaderView::section {
    background: #f5f7fa;
    padding: 10px 8px;
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
    cursor: pointer;
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
QPushButton#normalBtn {
    background-color: #ffffff;
    color: #606266;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
    padding: 8px 20px;
}
QPushButton#normalBtn:hover {
    border-color: #409eff;
    color: #409eff;
}
    )QSS");
}

void RuleSettingDialog::LoadData() {
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    // 加载模板
    auto templates = repo.ListTemplates();
    tpl_table_->setRowCount(templates.size());
    for (int i = 0; i < templates.size(); i++) {
        const auto &t = templates[i].toMap();
        tpl_table_->setItem(i, 0, new QTableWidgetItem(t["template_id"].toString()));
        tpl_table_->setItem(i, 1, new QTableWidgetItem(t["template_name"].toString()));
        tpl_table_->setItem(i, 2, new QTableWidgetItem(t["carrier_name"].toString()));
        tpl_table_->setItem(i, 3, new QTableWidgetItem(t["is_default"].toBool() ? "是" : "否"));
    }

    // 加载加价策略
    auto strategies = repo.ListSurchargeStrategies();
    surcharge_table_->setRowCount(strategies.size());
    for (int i = 0; i < strategies.size(); i++) {
        const auto &s = strategies[i].toMap();

        QString scope_text;
        QString scope = s["strategy_scope"].toString();
        if (scope == "global") scope_text = "全局";
        else if (scope == "template") scope_text = "模板级";
        else if (scope == "province") scope_text = "省份级";
        else if (scope == "customer") scope_text = "客户级";
        else scope_text = scope;

        QString type_text;
        QString type = s["strategy_type"].toString();
        if (type == "fixed") type_text = "固定加价";
        else if (type == "percentage") type_text = "比例加价";
        else if (type == "per_weight") type_text = "按重量";
        else if (type == "per_volume") type_text = "按体积";
        else type_text = type;

        QString amount_text;
        if (type == "percentage") {
            amount_text = QString::number(s["amount"].toDouble() * 100, 'f', 1) + "%";
        } else if (type == "fixed") {
            amount_text = "¥ " + QString::number(s["amount"].toDouble(), 'f', 2);
        } else if (type == "per_weight") {
            amount_text = "¥ " + QString::number(s["amount"].toDouble(), 'f', 2) + "/kg";
        } else {
            amount_text = QString::number(s["amount"].toDouble(), 'f', 2);
        }

        surcharge_table_->setItem(i, 0, new QTableWidgetItem(s["strategy_id"].toString()));
        surcharge_table_->setItem(i, 1, new QTableWidgetItem(s["strategy_name"].toString()));
        surcharge_table_->setItem(i, 2, new QTableWidgetItem(scope_text));
        surcharge_table_->setItem(i, 3, new QTableWidgetItem(type_text));
        surcharge_table_->setItem(i, 4, new QTableWidgetItem(amount_text));
        surcharge_table_->setItem(i, 5, new QTableWidgetItem(QString::number(s["priority"].toInt())));
        surcharge_table_->setItem(i, 6, new QTableWidgetItem(s["is_active"].toBool() ? "✅ 启用" : "❌ 停用"));
    }

    // 加载燃油附加费
    auto fuels = repo.ListFuelSurcharges("zto_standard");
    fuel_table_->setRowCount(fuels.size());
    for (int i = 0; i < fuels.size(); i++) {
        const auto &f = fuels[i].toMap();
        fuel_table_->setItem(i, 0, new QTableWidgetItem(QString::number(f["id"].toInt())));
        fuel_table_->setItem(i, 1, new QTableWidgetItem(f["effective_date"].toString()));
        fuel_table_->setItem(i, 2, new QTableWidgetItem(QString::number(f["rate"].toDouble() * 100, 'f', 2) + "%"));
        fuel_table_->setItem(i, 3, new QTableWidgetItem(f["template_id"].toString()));
        bool active = f["is_active"].toBool();
        auto *status_item = new QTableWidgetItem(active ? "✅ 启用" : "❌ 停用");
        status_item->setForeground(active ? QColor("#67c23a") : QColor("#909399"));
        fuel_table_->setItem(i, 4, status_item);
    }

    // 加载偏远地区
    auto remotes = repo.ListRemoteAreas("zto_standard");
    remote_table_->setRowCount(remotes.size());
    for (int i = 0; i < remotes.size(); i++) {
        const auto &r = remotes[i].toMap();
        remote_table_->setItem(i, 0, new QTableWidgetItem(QString::number(r["id"].toInt())));
        remote_table_->setItem(i, 1, new QTableWidgetItem(r["province"].toString()));
        remote_table_->setItem(i, 2, new QTableWidgetItem(r["city"].toString()));
        remote_table_->setItem(i, 3, new QTableWidgetItem(r["district"].toString()));
        remote_table_->setItem(i, 4, new QTableWidgetItem("¥ " + QString::number(r["surcharge"].toDouble(), 'f', 2)));
        bool active = r["is_active"].toBool();
        auto *status_item = new QTableWidgetItem(active ? "✅ 启用" : "❌ 停用");
        status_item->setForeground(active ? QColor("#67c23a") : QColor("#909399"));
        remote_table_->setItem(i, 5, status_item);
    }
}

void RuleSettingDialog::ShowSurchargeDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增加价策略" : "编辑加价策略");
    dlg.resize(420, 440);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(12);

    auto *name_edit = new QLineEdit();
    name_edit->setPlaceholderText("请输入策略名称");

    auto *scope_combo = new QComboBox();
    scope_combo->addItem("全局", "global");
    scope_combo->addItem("模板级", "template");
    scope_combo->addItem("省份级", "province");
    scope_combo->addItem("客户级", "customer");

    auto *type_combo = new QComboBox();
    type_combo->addItem("固定加价(元)", "fixed");
    type_combo->addItem("比例加价(%)", "percentage");
    type_combo->addItem("按重量(元/kg)", "per_weight");

    auto *amount_spin = new QDoubleSpinBox();
    amount_spin->setRange(0, 99999);
    amount_spin->setDecimals(2);
    amount_spin->setSingleStep(0.1);

    auto *priority_spin = new QSpinBox();
    priority_spin->setRange(0, 999);
    priority_spin->setValue(50);

    auto *active_check = new QCheckBox("启用");
    active_check->setChecked(true);

    auto *min_weight_spin = new QDoubleSpinBox();
    min_weight_spin->setRange(0, 9999);
    min_weight_spin->setDecimals(3);
    min_weight_spin->setSingleStep(0.5);
    min_weight_spin->setSpecialValueText("不限制");

    auto *max_weight_spin = new QDoubleSpinBox();
    max_weight_spin->setRange(0, 9999);
    max_weight_spin->setDecimals(3);
    max_weight_spin->setSingleStep(0.5);
    max_weight_spin->setSpecialValueText("不限制");

    auto *desc_edit = new QLineEdit();
    desc_edit->setPlaceholderText("选填");

    QString sid_to_edit;
    if (!is_add && surcharge_table_->currentRow() >= 0) {
        sid_to_edit = surcharge_table_->item(surcharge_table_->currentRow(), 0)->text();
        auto s = repo.GetSurchargeStrategy(sid_to_edit);
        name_edit->setText(s["strategy_name"].toString());

        int scope_idx = scope_combo->findData(s["strategy_scope"].toString());
        if (scope_idx >= 0) scope_combo->setCurrentIndex(scope_idx);

        int type_idx = type_combo->findData(s["strategy_type"].toString());
        if (type_idx >= 0) type_combo->setCurrentIndex(type_idx);

        amount_spin->setValue(s["amount"].toDouble());
        priority_spin->setValue(s["priority"].toInt());
        active_check->setChecked(s["is_active"].toBool());
        min_weight_spin->setValue(s["min_weight"].toDouble());
        max_weight_spin->setValue(s["max_weight"].toDouble());
        desc_edit->setText(s["description"].toString());
    }

    form->addRow("策略名称:", name_edit);
    form->addRow("适用范围:", scope_combo);
    form->addRow("策略类型:", type_combo);
    form->addRow("金额/比例:", amount_spin);
    form->addRow("优先级:", priority_spin);
    form->addRow("", active_check);
    form->addRow("最小重量:", min_weight_spin);
    form->addRow("最大重量:", max_weight_spin);
    form->addRow("描述:", desc_edit);

    layout->addLayout(form);
    layout->addStretch();

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);

    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        if (name_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "提示", "策略名称不能为空");
            return;
        }

        QVariantMap strategy;
        strategy["strategy_name"] = name_edit->text().trimmed();
        strategy["strategy_scope"] = scope_combo->currentData().toString();
        strategy["strategy_type"] = type_combo->currentData().toString();
        strategy["amount"] = amount_spin->value();
        strategy["priority"] = priority_spin->value();
        strategy["is_active"] = active_check->isChecked();
        strategy["min_weight"] = min_weight_spin->value();
        strategy["max_weight"] = max_weight_spin->value();
        strategy["description"] = desc_edit->text().trimmed();
        strategy["template_id"] = "zto_standard";

        bool ok = false;
        if (is_add) {
            QString sid = "surcharge_" + QString::number(QDateTime::currentSecsSinceEpoch())
                        + "_" + QString::number(QRandomGenerator::global()->generate() % 10000);
            strategy["strategy_id"] = sid;
            ok = repo.AddSurchargeStrategy(strategy);
        } else {
            strategy["strategy_id"] = sid_to_edit;
            ok = repo.UpdateSurchargeStrategy(strategy);
        }

        if (ok) {
            LoadData();
            QMessageBox::information(this, "成功", is_add ? "策略已添加" : "策略已更新");
        } else {
            QMessageBox::warning(this, "错误", is_add ? "添加失败" : "更新失败");
        }
    }
}

void RuleSettingDialog::OnFuelItemClicked(int row, int col) {
    if (col != 4) return; // 只响应"启用"列
    int id = fuel_table_->item(row, 0)->text().toInt();
    bool current_active = fuel_table_->item(row, 4)->text().contains("启用");
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();
    repo.SetFuelSurchargeActive(id, !current_active);
    LoadData();
}

void RuleSettingDialog::OnRemoteItemClicked(int row, int col) {
    if (col != 5) return; // 只响应"启用"列
    int id = remote_table_->item(row, 0)->text().toInt();
    bool current_active = remote_table_->item(row, 5)->text().contains("启用");
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();
    repo.SetRemoteAreaActive(id, !current_active);
    LoadData();
}

void RuleSettingDialog::ShowFuelDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增燃油附加费" : "编辑燃油附加费");
    dlg.resize(380, 200);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    auto *date_edit = new QLineEdit("2026-01-01");
    auto *rate_spin = new QDoubleSpinBox();
    rate_spin->setRange(0, 100);
    rate_spin->setDecimals(2);
    rate_spin->setSingleStep(0.1);
    rate_spin->setSuffix(" %");
    rate_spin->setValue(0.0);

    int id_to_edit = -1;
    if (!is_add && fuel_table_->currentRow() >= 0) {
        id_to_edit = fuel_table_->item(fuel_table_->currentRow(), 0)->text().toInt();
        auto fuels = repo.ListFuelSurcharges("zto_standard");
        for (const auto &f : fuels) {
            if (f.toMap()["id"].toInt() == id_to_edit) {
                date_edit->setText(f.toMap()["effective_date"].toString());
                rate_spin->setValue(f.toMap()["rate"].toDouble() * 100);
                break;
            }
        }
    }

    form->addRow("生效日期:", date_edit);
    form->addRow("费率:", rate_spin);
    layout->addLayout(form);
    layout->addStretch();

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);
    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        QVariantMap fuel;
        fuel["template_id"] = "zto_standard";
        fuel["effective_date"] = date_edit->text().trimmed();
        fuel["rate"] = rate_spin->value() / 100.0;

        bool ok = false;
        if (is_add) {
            ok = repo.AddFuelSurcharge(fuel);
        } else {
            ok = repo.UpdateFuelSurcharge(id_to_edit, fuel);
        }

        if (ok) {
            LoadData();
            QMessageBox::information(this, "成功", is_add ? "已添加" : "已更新");
        } else {
            QMessageBox::warning(this, "错误", is_add ? "添加失败" : "更新失败");
        }
    }
}

void RuleSettingDialog::ShowRemoteDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增偏远地区" : "编辑偏远地区");
    dlg.resize(380, 260);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    auto *prov_edit = new QLineEdit();
    auto *city_edit = new QLineEdit();
    auto *district_edit = new QLineEdit();
    auto *surcharge_spin = new QDoubleSpinBox();
    surcharge_spin->setRange(0, 9999);
    surcharge_spin->setDecimals(2);
    surcharge_spin->setSingleStep(0.5);
    surcharge_spin->setPrefix("¥ ");
    surcharge_spin->setValue(5.0);

    int id_to_edit = -1;
    if (!is_add && remote_table_->currentRow() >= 0) {
        id_to_edit = remote_table_->item(remote_table_->currentRow(), 0)->text().toInt();
        auto remotes = repo.ListRemoteAreas("zto_standard");
        for (const auto &r : remotes) {
            if (r.toMap()["id"].toInt() == id_to_edit) {
                prov_edit->setText(r.toMap()["province"].toString());
                city_edit->setText(r.toMap()["city"].toString());
                district_edit->setText(r.toMap()["district"].toString());
                surcharge_spin->setValue(r.toMap()["surcharge"].toDouble());
                break;
            }
        }
    }

    form->addRow("省份:", prov_edit);
    form->addRow("城市:", city_edit);
    form->addRow("区县:", district_edit);
    form->addRow("附加费:", surcharge_spin);
    layout->addLayout(form);
    layout->addStretch();

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);
    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        if (prov_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "提示", "省份不能为空");
            return;
        }

        QVariantMap area;
        area["template_id"] = "zto_standard";
        area["province"] = prov_edit->text().trimmed();
        area["city"] = city_edit->text().trimmed();
        area["district"] = district_edit->text().trimmed();
        area["surcharge"] = surcharge_spin->value();

        bool ok = false;
        if (is_add) {
            ok = repo.AddRemoteArea(area);
        } else {
            ok = repo.UpdateRemoteArea(id_to_edit, area);
        }

        if (ok) {
            LoadData();
            QMessageBox::information(this, "成功", is_add ? "已添加" : "已更新");
        } else {
            QMessageBox::warning(this, "错误", is_add ? "添加失败" : "更新失败");
        }
    }
}

} // namespace freight::ui::dialogs
