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
#include <QPlainTextEdit>

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
    auto *s_filter_layout = new QHBoxLayout();
    s_filter_layout->addWidget(new QLabel("模板筛选:"));
    cb_surcharge_filter_ = new QComboBox();
    cb_surcharge_filter_->addItem("全部模板", "");
    cb_surcharge_filter_->setMinimumWidth(180);
    s_filter_layout->addWidget(cb_surcharge_filter_);
    s_filter_layout->addStretch();
    surcharge_layout->addLayout(s_filter_layout);

    surcharge_table_ = new QTableWidget(0, 8);
    surcharge_table_->setHorizontalHeaderLabels({"ID", "策略名称", "范围", "绑定模板", "类型", "金额/比例", "优先级", "启用"});
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

    connect(cb_surcharge_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RuleSettingDialog::OnSurchargeFilterChanged);
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
    auto *fuel_filter_layout = new QHBoxLayout();
    fuel_filter_layout->addWidget(new QLabel("模板筛选:"));
    cb_fuel_filter_ = new QComboBox();
    cb_fuel_filter_->addItem("全部模板", "");
    cb_fuel_filter_->setMinimumWidth(180);
    fuel_filter_layout->addWidget(cb_fuel_filter_);
    fuel_filter_layout->addStretch();
    fuel_layout->addLayout(fuel_filter_layout);

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

    connect(cb_fuel_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RuleSettingDialog::OnFuelFilterChanged);
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

    // ====== 地区加价 ======
    auto *remote_tab = new QWidget();
    auto *remote_layout = new QVBoxLayout(remote_tab);
    auto *remote_filter_layout = new QHBoxLayout();
    remote_filter_layout->addWidget(new QLabel("模板筛选:"));
    cb_remote_filter_ = new QComboBox();
    cb_remote_filter_->addItem("全部模板", "");
    cb_remote_filter_->setMinimumWidth(180);
    remote_filter_layout->addWidget(cb_remote_filter_);
    remote_filter_layout->addStretch();
    remote_layout->addLayout(remote_filter_layout);

    remote_table_ = new QTableWidget(0, 7);
    remote_table_->setHorizontalHeaderLabels({"ID", "省份", "城市", "区县", "加价(元)", "模板", "启用"});
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

    tab_widget_->addTab(remote_tab, "地区加价");

    connect(cb_remote_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RuleSettingDialog::OnRemoteFilterChanged);
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
        auto ret = QMessageBox::question(this, "确认删除", "确定要删除该地区加价记录吗？",
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

    // ====== Tab 6: 表头映射关键字 ======
    auto *mapping_tab = new QWidget();
    SetupMappingTab(mapping_tab);
    mapping_tab_idx_ = tab_widget_->addTab(mapping_tab, "🧭 表头关键字");

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

    // 刷新三个模板筛选下拉框（保持当前选中值）
    auto refresh_filter = [](QComboBox *cb, const QVariantList &tpls) {
        QString cur = cb->currentData().toString();
        cb->blockSignals(true);
        cb->clear();
        cb->addItem("全部模板", "");
        for (const auto &tv : tpls) {
            const auto &t = tv.toMap();
            QString label = QString("%1 (%2)").arg(t["template_name"].toString(), t["template_id"].toString());
            cb->addItem(label, t["template_id"].toString());
        }
        int idx = cb->findData(cur);
        if (idx < 0) {
            // 默认选中第一个非"全部"（也就是第一个模板）
            if (cb->count() > 1) idx = 1;
            else idx = 0;
        }
        cb->setCurrentIndex(idx);
        cb->blockSignals(false);
    };
    refresh_filter(cb_fuel_filter_, templates);
    refresh_filter(cb_remote_filter_, templates);
    refresh_filter(cb_surcharge_filter_, templates);

    for (int i = 0; i < templates.size(); i++) {
        const auto &t = templates[i].toMap();
        tpl_table_->setItem(i, 0, new QTableWidgetItem(t["template_id"].toString()));
        tpl_table_->setItem(i, 1, new QTableWidgetItem(t["template_name"].toString()));
        tpl_table_->setItem(i, 2, new QTableWidgetItem(t["carrier_name"].toString()));
        tpl_table_->setItem(i, 3, new QTableWidgetItem(t["is_default"].toBool() ? "是" : "否"));
    }

    // 加价策略：按筛选模板过滤（或全部），列表列从7→8，多一列绑定模板
    QString surcharge_filter = cb_surcharge_filter_ ? cb_surcharge_filter_->currentData().toString() : "";
    QVariantList strategies_all = repo.ListSurchargeStrategies();
    QVariantList strategies;
    if (surcharge_filter.isEmpty()) {
        strategies = strategies_all;
    } else {
        for (const auto &sv : strategies_all) {
            const auto &s = sv.toMap();
            if (s["template_id"].toString() == surcharge_filter) {
                strategies << s;
            }
        }
    }
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
        } else if (type == "per_volume") {
            amount_text = "¥ " + QString::number(s["amount"].toDouble(), 'f', 2) + "/kg 体积重";
        } else {
            amount_text = QString::number(s["amount"].toDouble(), 'f', 2);
        }

        surcharge_table_->setItem(i, 0, new QTableWidgetItem(s["strategy_id"].toString()));
        surcharge_table_->setItem(i, 1, new QTableWidgetItem(s["strategy_name"].toString()));
        surcharge_table_->setItem(i, 2, new QTableWidgetItem(scope_text));
        surcharge_table_->setItem(i, 3, new QTableWidgetItem(s["template_id"].toString()));
        surcharge_table_->setItem(i, 4, new QTableWidgetItem(type_text));
        surcharge_table_->setItem(i, 5, new QTableWidgetItem(amount_text));
        surcharge_table_->setItem(i, 6, new QTableWidgetItem(QString::number(s["priority"].toInt())));
        surcharge_table_->setItem(i, 7, new QTableWidgetItem(s["is_active"].toBool() ? "✅ 启用" : "❌ 停用"));
    }

    // 燃油附加费：按筛选模板过滤
    QString fuel_filter = cb_fuel_filter_ ? cb_fuel_filter_->currentData().toString() : "";
    auto fuels = repo.ListFuelSurcharges(fuel_filter);
    fuel_table_->setRowCount(fuels.size());
    for (int i = 0; i < fuels.size(); i++) {
        const auto &f = fuels[i].toMap();
        fuel_table_->setItem(i, 0, new QTableWidgetItem(QString::number(f["id"].toInt())));
        fuel_table_->setItem(i, 1, new QTableWidgetItem(f["effective_date"].toString()));
        fuel_table_->setItem(i, 2, new QTableWidgetItem(QString::number(f["rate"].toDouble() * 100, 'f', 2) + "%"));
        QString tpl_str = f["template_id"].toString();
        fuel_table_->setItem(i, 3, new QTableWidgetItem(tpl_str == "*" ? "◆ 全局（所有模板通用）" : tpl_str));
        bool active = f["is_active"].toBool();
        auto *status_item = new QTableWidgetItem(active ? "✅ 启用" : "❌ 停用");
        status_item->setForeground(active ? QColor("#67c23a") : QColor("#909399"));
        fuel_table_->setItem(i, 4, status_item);
    }

    // 地区加价：按筛选模板过滤，列数从6→7（加了模板列）
    QString remote_filter = cb_remote_filter_ ? cb_remote_filter_->currentData().toString() : "";
    auto remotes = repo.ListRemoteAreas(remote_filter);
    remote_table_->setRowCount(remotes.size());
    for (int i = 0; i < remotes.size(); i++) {
        const auto &r = remotes[i].toMap();
        remote_table_->setItem(i, 0, new QTableWidgetItem(QString::number(r["id"].toInt())));
        remote_table_->setItem(i, 1, new QTableWidgetItem(r["province"].toString()));
        remote_table_->setItem(i, 2, new QTableWidgetItem(r["city"].toString()));
        remote_table_->setItem(i, 3, new QTableWidgetItem(r["district"].toString()));
        remote_table_->setItem(i, 4, new QTableWidgetItem("¥ " + QString::number(r["surcharge"].toDouble(), 'f', 2)));
        QString tpl_r = r["template_id"].toString();
        remote_table_->setItem(i, 5, new QTableWidgetItem(tpl_r == "*" ? "◆ 全局（所有模板通用）" : tpl_r));
        bool active = r["is_active"].toBool();
        auto *status_item = new QTableWidgetItem(active ? "✅ 启用" : "❌ 停用");
        status_item->setForeground(active ? QColor("#67c23a") : QColor("#909399"));
        remote_table_->setItem(i, 6, status_item);
    }

    LoadMappingTable();
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
    scope_combo->addItem("省份级", "province");
    scope_combo->addItem("客户级", "customer");

    auto *type_combo = new QComboBox();
    type_combo->addItem("固定加价(元)", "fixed");
    type_combo->addItem("比例加价(%)", "percentage");
    type_combo->addItem("按重量(元/kg)", "per_weight");
    type_combo->addItem("按体积(元/kg 体积重)", "per_volume");

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

    auto *tpl_combo = new QComboBox();
    auto tpls = repo.ListTemplates();
    QString default_tpl = cb_surcharge_filter_ && !cb_surcharge_filter_->currentData().toString().isEmpty()
                              ? cb_surcharge_filter_->currentData().toString()
                              : (tpls.isEmpty() ? QString() : tpls.first().toMap()["template_id"].toString());
    for (const auto &tv : tpls) {
        const auto &t = tv.toMap();
        tpl_combo->addItem(QString("%1 (%2)").arg(t["template_name"].toString(), t["template_id"].toString()),
                           t["template_id"].toString());
    }

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

        int ti = tpl_combo->findData(s["template_id"].toString());
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    } else {
        // 新增：默认选当前筛选模板
        int ti = tpl_combo->findData(default_tpl);
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    }

    form->addRow("策略名称:", name_edit);
    form->addRow("绑定模板:", tpl_combo);
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
        if (tpl_combo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, "提示", "请先添加运费模板，再创建加价策略");
            return;
        }

        QVariantMap strategy;
        strategy["strategy_name"] = name_edit->text().trimmed();
        strategy["template_id"] = tpl_combo->currentData().toString();
        strategy["strategy_scope"] = scope_combo->currentData().toString();
        strategy["strategy_type"] = type_combo->currentData().toString();
        strategy["amount"] = amount_spin->value();
        strategy["priority"] = priority_spin->value();
        strategy["is_active"] = active_check->isChecked();
        strategy["min_weight"] = min_weight_spin->value();
        strategy["max_weight"] = max_weight_spin->value();
        strategy["description"] = desc_edit->text().trimmed();

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
    if (col != 6) return; // 启用列在第6列（ID/省/市/区/加价/模板/启用 → 第7列，索引6）
    int id = remote_table_->item(row, 0)->text().toInt();
    bool current_active = remote_table_->item(row, 6)->text().contains("启用");
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();
    repo.SetRemoteAreaActive(id, !current_active);
    LoadData();
}

void RuleSettingDialog::OnFuelFilterChanged(int) { LoadData(); }
void RuleSettingDialog::OnRemoteFilterChanged(int) { LoadData(); }
void RuleSettingDialog::OnSurchargeFilterChanged(int) { LoadData(); }

void RuleSettingDialog::OpenMappingTab() {
    if (mapping_tab_idx_ >= 0) {
        tab_widget_->setCurrentIndex(mapping_tab_idx_);
    }
}

void RuleSettingDialog::SetupMappingTab(QWidget *tab) {
    auto &icons = IconManager::Instance();
    auto *root = new QVBoxLayout(tab);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *tip = new QLabel("每行 1 个标准列，多个关键字用「、顿号」分隔（其他常用分隔符逗号/分号/空格/换行在编辑弹窗也能自动识别）。\n"
                           "蓝色=系统默认关键字，橙色=你自定义添加的关键字。编辑后自动保存，重启也生效。");
    tip->setWordWrap(true);
    tip->setStyleSheet("color:#606266;padding:10px 12px;background:#ecf5ff;border:1px solid #d9ecff;border-radius:6px;line-height:1.55;");
    root->addWidget(tip);

    auto *add_bar = new QHBoxLayout();
    add_bar->setSpacing(8);
    add_bar->addWidget(new QLabel("⚡ 快速追加关键字到:"));
    cb_mapping_quick_std_ = new QComboBox();
    for (const QString &std_name : core::AppConfig::StandardColumnOrder()) {
        cb_mapping_quick_std_->addItem(core::AppConfig::StandardColumnToCn(std_name), std_name);
    }
    cb_mapping_quick_std_->setMinimumWidth(160);
    add_bar->addWidget(cb_mapping_quick_std_);

    ed_mapping_quick_kw_ = new QLineEdit();
    ed_mapping_quick_kw_->setPlaceholderText("例如：商家单号、抛重、实际KG、实际重量（多词可用、顿号一次多个）");
    add_bar->addWidget(ed_mapping_quick_kw_, 1);

    btn_mapping_quick_add_ = new QPushButton(" ➕ 添加");
    btn_mapping_quick_add_->setIcon(icons.ActionIcon("add"));
    btn_mapping_quick_add_->setObjectName("primaryBtn");
    btn_mapping_quick_add_->setCursor(Qt::PointingHandCursor);
    add_bar->addWidget(btn_mapping_quick_add_);
    root->addLayout(add_bar);

    mapping_table_ = new QTableWidget(0, 4);
    mapping_table_->setHorizontalHeaderLabels({"标准列(中文)", "标准列(英文)", "关键字（顿号分隔，双击或点右侧编辑）", "操作"});
    mapping_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    mapping_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mapping_table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mapping_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    mapping_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mapping_table_->setWordWrap(true);
    mapping_table_->verticalHeader()->setVisible(false);
    mapping_table_->horizontalHeader()->setMinimumSectionSize(40);
    mapping_table_->setMinimumHeight(420);
    root->addWidget(mapping_table_, 1);

    auto *op_bar = new QHBoxLayout();
    btn_mapping_reset_ = new QPushButton(" 🔄 恢复默认关键字（清空所有自定义）");
    btn_mapping_reset_->setIcon(icons.ActionIcon("reset"));
    btn_mapping_reset_->setCursor(Qt::PointingHandCursor);
    btn_mapping_apply_ = new QPushButton(" ✅ 应用（立即生效）");
    btn_mapping_apply_->setObjectName("primaryBtn");
    btn_mapping_apply_->setCursor(Qt::PointingHandCursor);
    op_bar->addWidget(btn_mapping_reset_);
    op_bar->addStretch();
    op_bar->addWidget(btn_mapping_apply_);
    root->addLayout(op_bar);

    connect(btn_mapping_quick_add_, &QPushButton::clicked, this, &RuleSettingDialog::OnQuickAddKeyword);
    connect(ed_mapping_quick_kw_, &QLineEdit::returnPressed, this, &RuleSettingDialog::OnQuickAddKeyword);
    connect(btn_mapping_reset_, &QPushButton::clicked, this, &RuleSettingDialog::OnResetMappingKeywords);
    connect(btn_mapping_apply_, &QPushButton::clicked, this, &RuleSettingDialog::OnApplyMappingKeywords);
    connect(mapping_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == 2) OnEditMappingRow(row);
    });
}

static QStringList split_keywords(const QString &raw) {
    QString s = raw;
    for (const QChar sep : {
        QChar(0x3001), QChar(';'), QChar(0xFF1B), QChar(','),
        QChar(0xFF0C), QChar('|'), QChar('/'), QChar('\\'),
        QChar('\t'), QChar('\n'), QChar('\r')
    }) {
        s.replace(sep, ' ');
    }
    QStringList all = s.split(' ', Qt::SkipEmptyParts, Qt::CaseInsensitive);
    QStringList out;
    QSet<QString> seen;
    for (const QString &k : all) {
        QString t = k.trimmed();
        if (t.isEmpty()) continue;
        if (seen.contains(t.toLower())) continue;
        seen.insert(t.toLower());
        out.append(t);
    }
    return out;
}

static QStringList case_unique(const QStringList &list) {
    QStringList out;
    QSet<QString> seen;
    for (const QString &k : list) {
        QString t = k.trimmed();
        if (t.isEmpty()) continue;
        if (seen.contains(t.toLower())) continue;
        seen.insert(t.toLower());
        out.append(t);
    }
    return out;
}

void RuleSettingDialog::LoadMappingTable() {
    if (!mapping_table_) return;
    auto &cfg = core::AppConfig::Instance();
    const auto &defs = core::AppConfig::DefaultMappingKeywords();
    const auto cust = cfg.GetMappingKeywords();
    const auto &req = core::AppConfig::RequiredStandardColumns();
    mapping_table_->blockSignals(true);
    mapping_table_->setRowCount(0);

    const auto &order = core::AppConfig::StandardColumnOrder();
    for (const QString &std_col : order) {
        int r = mapping_table_->rowCount();
        mapping_table_->insertRow(r);

        QString cn = core::AppConfig::StandardColumnToCn(std_col);
        if (req.contains(std_col)) cn.prepend("★ ");
        auto *cn_item = new QTableWidgetItem(cn);
        cn_item->setFlags(cn_item->flags() & ~Qt::ItemIsEditable);
        cn_item->setFont(QFont(QString::fromUtf8("PingFang SC"), -1, QFont::DemiBold));
        if (req.contains(std_col)) cn_item->setForeground(QColor(245, 108, 108));
        mapping_table_->setItem(r, 0, cn_item);

        auto *en_item = new QTableWidgetItem(std_col);
        en_item->setFlags(en_item->flags() & ~Qt::ItemIsEditable);
        en_item->setForeground(QColor(144, 147, 153));
        mapping_table_->setItem(r, 1, en_item);

        const QStringList def_list = defs.value(std_col);
        QStringList cust_list = cust.value(std_col);
        QSet<QString> def_low;
        for (const QString &d : def_list) def_low.insert(d.toLower());
        QStringList pure_cust;
        for (const QString &c : cust_list) {
            if (!def_low.contains(c.toLower())) pure_cust.append(c);
        }
        pure_cust = case_unique(pure_cust);

        QString html;
        QStringList def_parts;
        for (const QString &d : case_unique(def_list)) {
            def_parts.append(QString("<span style=\"color:#1f6feb;\">%1</span>").arg(d.toHtmlEscaped()));
        }
        QStringList cust_parts;
        for (const QString &c : pure_cust) {
            cust_parts.append(QString("<span style=\"color:#d97706;font-weight:600;\">%1</span>").arg(c.toHtmlEscaped()));
        }
        QStringList all_parts = def_parts + cust_parts;
        html = all_parts.join("、");
        auto *kw_item = new QTableWidgetItem();
        kw_item->setData(Qt::UserRole, std_col);
        kw_item->setData(Qt::UserRole + 1, pure_cust.join("、"));
        kw_item->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        QLabel *lbl = new QLabel();
        lbl->setTextFormat(Qt::RichText);
        lbl->setText(html);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("padding:4px 6px;background:#ffffff;");
        lbl->setMargin(4);
        mapping_table_->setCellWidget(r, 2, lbl);
        QObject::connect(lbl, &QLabel::linkActivated, lbl, [this, r](const QString &) { OnEditMappingRow(r); });
        mapping_table_->item(r, 0)->setData(Qt::UserRole, std_col);

        auto *op_cell = new QWidget();
        auto *op_layout = new QHBoxLayout(op_cell);
        op_layout->setContentsMargins(6, 4, 6, 4);
        op_layout->setSpacing(6);
        QPushButton *btn_edit = new QPushButton(" ✏️ 编辑");
        btn_edit->setCursor(Qt::PointingHandCursor);
        btn_edit->setStyleSheet("QPushButton{padding:4px 10px;border-radius:5px;background:#fef3c7;border:1px solid #fcd34d;color:#92400e;}QPushButton:hover{background:#fde68a;}");
        QPushButton *btn_reset_row = new QPushButton(" 🔙 恢复此行默认");
        btn_reset_row->setCursor(Qt::PointingHandCursor);
        btn_reset_row->setStyleSheet("QPushButton{padding:4px 10px;border-radius:5px;background:#e0e7ff;border:1px solid #c7d2fe;color:#3730a3;}QPushButton:hover{background:#c7d2fe;}");
        op_layout->addWidget(btn_edit);
        op_layout->addWidget(btn_reset_row);
        op_layout->addStretch();
        mapping_table_->setCellWidget(r, 3, op_cell);
        QObject::connect(btn_edit, &QPushButton::clicked, this, [this, r]() { OnEditMappingRow(r); });
        QObject::connect(btn_reset_row, &QPushButton::clicked, this, [this, r]() { OnResetMappingRow(r); });
    }
    mapping_table_->blockSignals(false);
    mapping_table_->resizeRowsToContents();
}

static QString std_col_of_row(QTableWidget *tbl, int row) {
    if (!tbl || row < 0 || row >= tbl->rowCount()) return {};
    if (auto *en = tbl->item(row, 1)) return en->text();
    return {};
}

void RuleSettingDialog::OnEditMappingRow(int row) {
    QString std_col = std_col_of_row(mapping_table_, row);
    if (std_col.isEmpty()) return;
    auto &cfg = core::AppConfig::Instance();
    const auto &defs = core::AppConfig::DefaultMappingKeywords();
    QStringList cur_def = case_unique(defs.value(std_col));
    QStringList cur_custom = case_unique(cfg.GetMappingKeywords().value(std_col));
    QSet<QString> def_low;
    for (const QString &d : cur_def) def_low.insert(d.toLower());
    QStringList pure_custom;
    for (const QString &c : cur_custom) {
        if (!def_low.contains(c.toLower())) pure_custom.append(c);
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QString("编辑关键字：%1").arg(core::AppConfig::StandardColumnToCn(std_col)));
    dlg.resize(620, 520);
    dlg.setModal(true);
    auto &icons = IconManager::Instance();

    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto *tip = new QLabel(
        QString("标准列：<b>%1</b>（<span style=\"color:#6b7280;\">%2</span>）<br/>"
                "分隔符自动识别：<b>「、顿号 / 逗号 , ， / 分号 ; ； / 空格 / 换行」</b>均可分隔多个关键字<br/>"
                "<span style=\"color:#1f6feb;\">蓝色 = 系统默认（建议保留）</span> / "
                "<span style=\"color:#d97706;font-weight:600;\">橙色 = 自定义追加</span> / "
                "没写在下方的默认关键字会被系统保留（只读，无法删除）。")
            .arg(core::AppConfig::StandardColumnToCn(std_col), std_col));
    tip->setWordWrap(true);
    tip->setStyleSheet("padding:10px;background:#f0f9ff;border:1px solid #bae6fd;border-radius:6px;color:#334155;");
    root->addWidget(tip);

    auto *hint1 = new QLabel(QString("🔵 系统默认关键字（只读预览）：%1").arg(cur_def.join("、")));
    hint1->setStyleSheet("color:#1e3a8a;padding:6px 8px;background:#eff6ff;border-radius:5px;");
    hint1->setWordWrap(true);
    root->addWidget(hint1);

    auto *lbl_custom = new QLabel("🟠 在此处编辑/追加自定义关键字（可一次写一堆）：");
    lbl_custom->setStyleSheet("font-weight:600;color:#78350f;");
    root->addWidget(lbl_custom);

    auto *ed = new QPlainTextEdit();
    ed->setPlaceholderText("示例：\n商家单号、商家编码\nbox_no、快递编号\n面单号、发货单号\n\n（空行会自动忽略）");
    ed->setPlainText(pure_custom.join("\n"));
    ed->setStyleSheet("QPlainTextEdit{font-family:Menlo,Consolas,Monospace;font-size:13px;padding:8px;border:1px solid #d1d5db;border-radius:6px;}");
    root->addWidget(ed, 1);

    auto *btn_row = new QHBoxLayout();
    auto *btn_paste_ton = new QPushButton(" 🔤 从顿号字符串粘贴成多行");
    btn_paste_ton->setIcon(icons.ActionIcon("reset"));
    auto *btn_clear_custom = new QPushButton(" 🗑 清空自定义");
    auto *btn_cancel = new QPushButton(" 取消");
    auto *btn_ok = new QPushButton(" 💾 确认保存");
    btn_ok->setObjectName("primaryBtn");
    btn_paste_ton->setCursor(Qt::PointingHandCursor);
    btn_clear_custom->setCursor(Qt::PointingHandCursor);
    btn_cancel->setCursor(Qt::PointingHandCursor);
    btn_ok->setCursor(Qt::PointingHandCursor);
    btn_row->addWidget(btn_paste_ton);
    btn_row->addWidget(btn_clear_custom);
    btn_row->addStretch();
    btn_row->addWidget(btn_cancel);
    btn_row->addWidget(btn_ok);
    root->addLayout(btn_row);

    bool ok_clicked = false;
    connect(btn_ok, &QPushButton::clicked, &dlg, [&ok_clicked, &dlg]() { ok_clicked = true; dlg.accept(); });
    connect(btn_cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btn_clear_custom, &QPushButton::clicked, ed, [ed]() {
        auto ret = QMessageBox::question(ed->window(), "清空自定义", "确定清空下方所有自定义关键字吗？\n（系统默认关键字保持不变）",
                                         QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) ed->clear();
    });
    connect(btn_paste_ton, &QPushButton::clicked, this, [ed]() {
        bool ok = false;
        QString raw = QInputDialog::getMultiLineText(
            ed->window(), "粘贴顿号分隔的关键字",
            "把要追加的关键字一次性粘贴进来，分隔符随便（顿号/逗号/换行都可，自动拆分去重）：",
            ed->toPlainText(), &ok);
        if (!ok) return;
        QStringList parts = split_keywords(raw);
        QString cur = ed->toPlainText();
        QStringList merged = split_keywords(cur);
        QSet<QString> seen;
        for (const QString &p : merged) seen.insert(p.toLower());
        for (const QString &p : parts) {
            if (!seen.contains(p.toLower())) { merged.append(p); seen.insert(p.toLower()); }
        }
        ed->setPlainText(merged.join("\n"));
    });

    if (dlg.exec() != QDialog::Accepted || !ok_clicked) return;

    QStringList new_custom = split_keywords(ed->toPlainText());
    QSet<QString> def_low_set;
    for (const QString &d : cur_def) def_low_set.insert(d.toLower());
    QStringList final_custom;
    for (const QString &k : new_custom) {
        if (def_low_set.contains(k.toLower())) continue;
        final_custom.append(k);
    }
    final_custom = case_unique(final_custom);

    const QStringList old_custom = pure_custom;
    QSet<QString> new_low;
    for (const QString &c : final_custom) new_low.insert(c.toLower());
    for (const QString &old_k : old_custom) {
        if (!new_low.contains(old_k.toLower())) {
            cfg.RemoveMappingKeyword(std_col, old_k);
        }
    }
    for (const QString &new_k : final_custom) {
        cfg.AddMappingKeyword(std_col, new_k);
    }
    LoadMappingTable();
}

void RuleSettingDialog::OnResetMappingRow(int row) {
    QString std_col = std_col_of_row(mapping_table_, row);
    if (std_col.isEmpty()) return;
    auto ret = QMessageBox::question(this, "恢复此行默认",
        QString("确定清空「%1」下所有自定义关键字并恢复系统默认吗？\n（系统默认关键字不会丢失）")
            .arg(core::AppConfig::StandardColumnToCn(std_col)),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    auto &cfg = core::AppConfig::Instance();
    QStringList old = cfg.GetMappingKeywords().value(std_col);
    for (const QString &k : old) cfg.RemoveMappingKeyword(std_col, k);
    LoadMappingTable();
}

void RuleSettingDialog::OnQuickAddKeyword() {
    if (!cb_mapping_quick_std_ || !ed_mapping_quick_kw_) return;
    QString raw = ed_mapping_quick_kw_->text().trimmed();
    if (raw.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入关键字（可以一次填多个：商家单号、抛重、实际KG）");
        return;
    }
    QString std_col = cb_mapping_quick_std_->currentData().toString();
    QStringList parts = split_keywords(raw);
    if (parts.isEmpty()) {
        QMessageBox::warning(this, "提示", "未识别出有效关键字");
        return;
    }
    auto &cfg = core::AppConfig::Instance();
    QStringList added;
    for (const QString &k : parts) {
        if (cfg.GetEffectiveMappingKeywords().value(std_col).contains(k, Qt::CaseInsensitive)) continue;
        cfg.AddMappingKeyword(std_col, k);
        added.append(k);
    }
    ed_mapping_quick_kw_->clear();
    LoadMappingTable();
    if (!added.isEmpty()) {
        QMessageBox::information(this, "已追加",
            QString("已为「%1」追加 %2 条自定义关键字：%3")
                .arg(core::AppConfig::StandardColumnToCn(std_col))
                .arg(added.size()).arg(added.join("、")));
    } else {
        QMessageBox::information(this, "没有变化", "输入的关键字已经存在于系统默认或自定义中，无需重复添加。");
    }
}

void RuleSettingDialog::OnResetMappingKeywords() {
    auto ret = QMessageBox::question(this, "确认恢复",
        "确定清空所有自定义关键字并恢复系统默认吗？",
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    core::AppConfig::Instance().ResetMappingKeywords();
    LoadMappingTable();
}

void RuleSettingDialog::OnApplyMappingKeywords() {
    auto &cfg = core::AppConfig::Instance();
    const auto &defs = core::AppConfig::DefaultMappingKeywords();
    int total_custom = 0;
    for (auto it = defs.cbegin(); it != defs.cend(); ++it) {
        total_custom += cfg.GetMappingKeywords().value(it.key()).size();
    }
    QMessageBox::information(this, "已应用",
        QString("关键字已立即生效（系统默认 + %1 条自定义），下次自动映射会使用。").arg(total_custom));
}

void RuleSettingDialog::ShowFuelDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增燃油附加费" : "编辑燃油附加费");
    dlg.resize(420, 230);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    auto *tpl_combo = new QComboBox();
    auto tpls = repo.ListTemplates();
    QString default_tpl = cb_fuel_filter_ && !cb_fuel_filter_->currentData().toString().isEmpty()
                              ? cb_fuel_filter_->currentData().toString()
                              : "*";
    tpl_combo->addItem("◆ 全局（所有模板通用）", "*");
    for (const auto &tv : tpls) {
        const auto &t = tv.toMap();
        tpl_combo->addItem(QString("%1 (%2)").arg(t["template_name"].toString(), t["template_id"].toString()),
                           t["template_id"].toString());
    }

    auto *date_edit = new QLineEdit(QDate::currentDate().toString("yyyy-MM-dd"));
    auto *rate_spin = new QDoubleSpinBox();
    rate_spin->setRange(0, 100);
    rate_spin->setDecimals(2);
    rate_spin->setSingleStep(0.1);
    rate_spin->setSuffix(" %");
    rate_spin->setValue(0.0);

    int id_to_edit = -1;
    QString orig_tpl;
    if (!is_add && fuel_table_->currentRow() >= 0) {
        id_to_edit = fuel_table_->item(fuel_table_->currentRow(), 0)->text().toInt();
        auto fuels = repo.ListFuelSurcharges("");
        for (const auto &f : fuels) {
            if (f.toMap()["id"].toInt() == id_to_edit) {
                date_edit->setText(f.toMap()["effective_date"].toString());
                rate_spin->setValue(f.toMap()["rate"].toDouble() * 100);
                orig_tpl = f.toMap()["template_id"].toString();
                break;
            }
        }
        int ti = tpl_combo->findData(orig_tpl);
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    } else {
        int ti = tpl_combo->findData(default_tpl);
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    }

    form->addRow("模板:", tpl_combo);
    form->addRow("生效日期:", date_edit);
    form->addRow("费率:", rate_spin);
    layout->addLayout(form);
    layout->addStretch();

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);
    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        if (tpl_combo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, "提示", "请先添加运费模板，再设置燃油附加费");
            return;
        }
        QVariantMap fuel;
        fuel["template_id"] = tpl_combo->currentData().toString();
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
    dlg.setWindowTitle(is_add ? "新增地区加价" : "编辑地区加价");
    dlg.resize(420, 300);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    auto *tpl_combo = new QComboBox();
    auto tpls = repo.ListTemplates();
    QString default_tpl = cb_remote_filter_ && !cb_remote_filter_->currentData().toString().isEmpty()
                              ? cb_remote_filter_->currentData().toString()
                              : "*";
    tpl_combo->addItem("◆ 全局（所有模板通用）", "*");
    for (const auto &tv : tpls) {
        const auto &t = tv.toMap();
        tpl_combo->addItem(QString("%1 (%2)").arg(t["template_name"].toString(), t["template_id"].toString()),
                           t["template_id"].toString());
    }

    auto *prov_combo = new QComboBox();
    prov_combo->setEditable(true);
    prov_combo->addItem(""); // 空=不限制（配合仅填城市用）
    const QStringList provinces = {
        "北京","天津","上海","重庆",
        "河北","山西","辽宁","吉林","黑龙江","江苏","浙江","安徽","福建","江西","山东",
        "河南","湖北","湖南","广东","海南","四川","贵州","云南","陕西","甘肃","青海","台湾",
        "内蒙古","广西","西藏","宁夏","新疆",
        "香港","澳门"
    };
    prov_combo->addItems(provinces);
    auto *city_edit = new QLineEdit();
    auto *district_edit = new QLineEdit();
    auto *surcharge_spin = new QDoubleSpinBox();
    surcharge_spin->setRange(0, 9999);
    surcharge_spin->setDecimals(2);
    surcharge_spin->setSingleStep(0.5);
    surcharge_spin->setPrefix("¥ ");
    surcharge_spin->setValue(5.0);

    int id_to_edit = -1;
    QString orig_tpl;
    QRegularExpression suffix_re(R"((省|市|维吾尔自治区|回族自治区|壮族自治区|自治区|自治州|地区|盟)$)");
    QRegularExpression city_suffix_re(R"((市|区|县|旗|自治县|林区)$)");
    auto normalize_prov = [&](QString s) -> QString {
        s = s.trimmed();
        s.remove(suffix_re);
        return s.trimmed();
    };
    auto normalize_city = [&](QString s) -> QString {
        s = s.trimmed();
        s.remove(city_suffix_re);
        return s.trimmed();
    };
    if (!is_add && remote_table_->currentRow() >= 0) {
        id_to_edit = remote_table_->item(remote_table_->currentRow(), 0)->text().toInt();
        auto remotes = repo.ListRemoteAreas("");
        for (const auto &r : remotes) {
            if (r.toMap()["id"].toInt() == id_to_edit) {
                QString prov_val = normalize_prov(r.toMap()["province"].toString());
                int idx = prov_combo->findText(prov_val);
                if (idx >= 0) prov_combo->setCurrentIndex(idx);
                else prov_combo->setCurrentText(prov_val);
                city_edit->setText(normalize_city(r.toMap()["city"].toString()));
                district_edit->setText(r.toMap()["district"].toString().trimmed());
                surcharge_spin->setValue(r.toMap()["surcharge"].toDouble());
                orig_tpl = r.toMap()["template_id"].toString();
                break;
            }
        }
        int ti = tpl_combo->findData(orig_tpl);
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    } else {
        int ti = tpl_combo->findData(default_tpl);
        if (ti >= 0) tpl_combo->setCurrentIndex(ti);
    }

    form->addRow("模板:", tpl_combo);
    form->addRow("省份:", prov_combo);
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
        if (tpl_combo->currentData().toString().isEmpty()) {
            QMessageBox::warning(this, "提示", "请先添加运费模板，再设置地区加价");
            return;
        }
        QString prov_norm = normalize_prov(prov_combo->currentText());
        QString city_norm = normalize_city(city_edit->text());
        QString dist_norm = district_edit->text().trimmed();
        if (prov_norm.isEmpty() && city_norm.isEmpty() && dist_norm.isEmpty()) {
            QMessageBox::warning(this, "提示", "省份/城市/区县 至少填一个");
            return;
        }
        if (!prov_norm.isEmpty() && !city_norm.isEmpty() && prov_norm == city_norm) {
            city_norm.clear();
        }

        QVariantMap area;
        area["template_id"] = tpl_combo->currentData().toString();
        area["province"] = prov_norm;
        area["city"] = city_norm;
        area["district"] = dist_norm;
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
