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
#include <QDateEdit>
#include <QScrollArea>
#include <QButtonGroup>
#include <QRadioButton>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGroupBox>
#include <QRegularExpression>

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

    // ====== Tab 7: 拉均重合同 ======
    auto *lajz_tab = new QWidget();
    SetupLajzTab(lajz_tab);
    lajz_tab_idx_ = tab_widget_->addTab(lajz_tab, "拉均重合同");

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
    LoadLajzTable();
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

void RuleSettingDialog::SetupLajzTab(QWidget *tab) {
    auto &icons = IconManager::Instance();
    auto *root = new QVBoxLayout(tab);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    lajz_table_ = new QTableWidget(0, 17);
    lajz_table_->setHorizontalHeaderLabels({
        "启停", "合同ID", "合同名称", "合同编号", "版本", "绑定模板",
        "生效起", "生效至", "基准均重kg", "进池上限kg", "封顶kg",
        "基础价", "步长kg", "每步加价", "最少票数", "超上限模式", "复用分组"
    });
    lajz_table_->horizontalHeader()->setStretchLastSection(true);
    lajz_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    lajz_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    lajz_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lajz_table_->setMinimumHeight(420);
    root->addWidget(lajz_table_, 1);

    auto *btn_layout = new QHBoxLayout();
    auto *btn_add = new QPushButton(" 新增合同");
    btn_add->setIcon(icons.ActionIcon("add"));
    btn_add->setCursor(Qt::PointingHandCursor);
    auto *btn_edit = new QPushButton(" 编辑");
    btn_edit->setCursor(Qt::PointingHandCursor);
    auto *btn_del = new QPushButton(" 删除");
    btn_del->setIcon(icons.ActionIcon("delete"));
    btn_del->setCursor(Qt::PointingHandCursor);
    auto *btn_enable = new QPushButton(" 启用");
    btn_enable->setCursor(Qt::PointingHandCursor);
    auto *btn_disable = new QPushButton(" 停用");
    btn_disable->setCursor(Qt::PointingHandCursor);

    btn_layout->addWidget(btn_add);
    btn_layout->addWidget(btn_edit);
    btn_layout->addWidget(btn_del);
    btn_layout->addStretch();
    btn_layout->addWidget(btn_enable);
    btn_layout->addWidget(btn_disable);
    root->addLayout(btn_layout);

    connect(lajz_table_, &QTableWidget::cellClicked, this, &RuleSettingDialog::OnLajzItemClicked);
    connect(btn_add, &QPushButton::clicked, this, &RuleSettingDialog::OnLajzAdd);
    connect(btn_edit, &QPushButton::clicked, this, &RuleSettingDialog::OnLajzEdit);
    connect(btn_del, &QPushButton::clicked, this, &RuleSettingDialog::OnLajzDel);
    connect(btn_enable, &QPushButton::clicked, this, [this]() { OnLajzToggle(true); });
    connect(btn_disable, &QPushButton::clicked, this, [this]() { OnLajzToggle(false); });
}

void RuleSettingDialog::LoadLajzTable() {
    if (!lajz_table_) return;
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto list = repo.ListAvgWeightTemplates();
    lajz_table_->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const auto m = list[i].toMap();
        bool active = m["is_active"].toBool();
        auto *act_item = new QTableWidgetItem(active ? "是" : "否");
        act_item->setForeground(active ? QColor("#67c23a") : QColor("#909399"));
        act_item->setTextAlignment(Qt::AlignCenter);
        act_item->setData(Qt::UserRole, m["avg_tpl_id"].toString());
        lajz_table_->setItem(i, 0, act_item);

        lajz_table_->setItem(i, 1, new QTableWidgetItem(m["avg_tpl_id"].toString()));
        lajz_table_->setItem(i, 2, new QTableWidgetItem(m["name"].toString()));
        lajz_table_->setItem(i, 3, new QTableWidgetItem(m["contract_no"].toString()));
        lajz_table_->setItem(i, 4, new QTableWidgetItem(QString::number(m["version"].toInt())));
        lajz_table_->setItem(i, 5, new QTableWidgetItem(m["template_id"].toString()));
        lajz_table_->setItem(i, 6, new QTableWidgetItem(m["effective_from"].toString()));
        lajz_table_->setItem(i, 7, new QTableWidgetItem(m["effective_to"].toString()));
        lajz_table_->setItem(i, 8, new QTableWidgetItem(QString::number(m["base_avg_kg"].toDouble(), 'f', 3)));
        lajz_table_->setItem(i, 9, new QTableWidgetItem(QString::number(m["avg_pool_max_kg"].toDouble(), 'f', 3)));
        lajz_table_->setItem(i, 10, new QTableWidgetItem(QString::number(m["avg_fee_cap_kg"].toDouble(), 'f', 3)));
        lajz_table_->setItem(i, 11, new QTableWidgetItem(QString::number(m["base_fee"].toDouble(), 'f', 2)));
        lajz_table_->setItem(i, 12, new QTableWidgetItem(QString::number(m["step_kg"].toDouble(), 'f', 3)));
        lajz_table_->setItem(i, 13, new QTableWidgetItem(QString::number(m["step_fee"].toDouble(), 'f', 2)));
        lajz_table_->setItem(i, 14, new QTableWidgetItem(QString::number(m["min_tickets"].toInt())));

        int ocm = m["over_cap_mode"].toInt();
        lajz_table_->setItem(i, 15, new QTableWidgetItem(ocm == 0 ? "封顶" : "整池回阶梯"));

        int rzg = m["reuse_zone_groups"].toInt();
        auto *rzg_item = new QTableWidgetItem(rzg == 1 ? "是" : "否");
        rzg_item->setTextAlignment(Qt::AlignCenter);
        lajz_table_->setItem(i, 16, rzg_item);
    }
    lajz_table_->resizeColumnsToContents();
}

void RuleSettingDialog::OpenAvgWeightTab() {
    if (lajz_tab_idx_ >= 0) {
        tab_widget_->setCurrentIndex(lajz_tab_idx_);
    }
}

void RuleSettingDialog::OnLajzItemClicked(int row, int col) {
    if (col != 0) return;
    QString avg_tpl_id = lajz_table_->item(row, 0)->data(Qt::UserRole).toString();
    if (avg_tpl_id.isEmpty()) return;
    bool current_active = lajz_table_->item(row, 0)->text() == "是";
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();
    repo.SetAvgWeightTemplateActive(avg_tpl_id, !current_active);
    LoadLajzTable();
}

void RuleSettingDialog::OnLajzToggle(bool active) {
    if (lajz_table_->currentRow() < 0) {
        QMessageBox::warning(this, "提示", "请先选择一条合同记录");
        return;
    }
    QString avg_tpl_id = lajz_table_->item(lajz_table_->currentRow(), 1)->text();
    if (avg_tpl_id.isEmpty()) return;
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();
    repo.SetAvgWeightTemplateActive(avg_tpl_id, active);
    LoadLajzTable();
}

void RuleSettingDialog::OnLajzAdd() {
    ShowLajzDialog(true);
}

void RuleSettingDialog::OnLajzEdit() {
    if (lajz_table_->currentRow() < 0) {
        QMessageBox::warning(this, "提示", "请先选择一条合同记录");
        return;
    }
    ShowLajzDialog(false);
}

void RuleSettingDialog::OnLajzDel() {
    if (lajz_table_->currentRow() < 0) {
        QMessageBox::warning(this, "提示", "请先选择一条合同记录");
        return;
    }
    QString avg_tpl_id = lajz_table_->item(lajz_table_->currentRow(), 1)->text();
    QString name = lajz_table_->item(lajz_table_->currentRow(), 2)->text();
    auto ret = QMessageBox::question(this, "确认删除",
        QString("确定要删除合同 \"%1\" (%2) 吗？").arg(name, avg_tpl_id),
        QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        auto &cfg = core::AppConfig::Instance();
        db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
        repo.Init();
        repo.DeleteAvgWeightTemplate(avg_tpl_id);
        LoadLajzTable();
    }
}

void RuleSettingDialog::ShowLajzDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增拉均重合同" : "编辑拉均重合同");
    dlg.resize(960, 860);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    const QStringList ALL_PROVINCES = {
        "北京","天津","河北","山西","内蒙古","辽宁","吉林","黑龙江",
        "上海","江苏","浙江","安徽","福建","江西","山东","河南",
        "湖北","湖南","广东","广西","海南","重庆","四川","贵州",
        "云南","西藏","陕西","甘肃","青海","宁夏","新疆","台湾",
        "香港","澳门"
    };

    auto templates_with_zones = repo.ListCourierTemplatesWithZones();
    auto find_tpl_map = [&](const QString &tid) -> QVariantMap {
        for (const auto &tv : templates_with_zones) {
            auto t = tv.toMap();
            if (t["template_id"].toString() == tid) return t;
        }
        return {};
    };

    QSet<QString> sel_tpl_groups;
    QSet<QString> excl_tplg_provs;

    QMap<QString, QStringList> mem_b_zones;
    int b_zone_next_idx = 0;

    auto *layout = new QVBoxLayout(&dlg);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *scroll_widget = new QWidget();
    auto *form = new QFormLayout(scroll_widget);
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    scroll->setWidget(scroll_widget);
    layout->addWidget(scroll, 1);

    auto *ed_avg_tpl_id = new QLineEdit();
    ed_avg_tpl_id->setPlaceholderText("例如: AVG_2026_SF");
    auto *ed_name = new QLineEdit();
    ed_name->setPlaceholderText("合同名称");
    auto *ed_contract_no = new QLineEdit();
    ed_contract_no->setPlaceholderText("合同编号");

    auto *template_combo = new QComboBox();
    template_combo->addItem("⚑ 全部模板通配（新订单匹配任何模板）", "");
    {
        QVariantList inactive_tpls;
        for (const auto &tv : templates_with_zones) {
            auto t = tv.toMap();
            if (t["is_active"].toBool()) {
                QString label = QString("%1 (%2) - 已启用 ✅")
                    .arg(t["template_name"].toString(), t["template_id"].toString());
                template_combo->addItem(label, t["template_id"].toString());
            } else {
                inactive_tpls << t;
            }
        }
        for (const auto &tv : inactive_tpls) {
            auto t = tv.toMap();
            QString label = QString("%1 (%2) - 未启用 ⚠️")
                .arg(t["template_name"].toString(), t["template_id"].toString());
            template_combo->addItem(label, t["template_id"].toString());
        }
    }

    auto *sp_version = new QSpinBox();
    sp_version->setRange(1, 99999);
    sp_version->setValue(1);

    auto *sp_min_tickets = new QSpinBox();
    sp_min_tickets->setRange(0, 9999999);
    sp_min_tickets->setValue(50);

    auto *sp_base_avg_kg = new QDoubleSpinBox();
    sp_base_avg_kg->setRange(0, 99999);
    sp_base_avg_kg->setDecimals(3);
    sp_base_avg_kg->setSingleStep(0.1);
    sp_base_avg_kg->setValue(0.3);

    auto *sp_avg_pool_max_kg = new QDoubleSpinBox();
    sp_avg_pool_max_kg->setRange(0, 99999);
    sp_avg_pool_max_kg->setDecimals(3);
    sp_avg_pool_max_kg->setSingleStep(0.1);
    sp_avg_pool_max_kg->setValue(1.0);

    auto *sp_avg_fee_cap_kg = new QDoubleSpinBox();
    sp_avg_fee_cap_kg->setRange(0, 99999);
    sp_avg_fee_cap_kg->setDecimals(3);
    sp_avg_fee_cap_kg->setSingleStep(0.1);
    sp_avg_fee_cap_kg->setValue(1.0);

    auto *sp_base_fee = new QDoubleSpinBox();
    sp_base_fee->setRange(0, 999999);
    sp_base_fee->setDecimals(2);
    sp_base_fee->setSingleStep(0.1);
    sp_base_fee->setValue(2.7);

    auto *sp_step_kg = new QDoubleSpinBox();
    sp_step_kg->setRange(0, 99999);
    sp_step_kg->setDecimals(3);
    sp_step_kg->setSingleStep(0.05);
    sp_step_kg->setValue(0.1);

    auto *sp_step_fee = new QDoubleSpinBox();
    sp_step_fee->setRange(0, 999999);
    sp_step_fee->setDecimals(2);
    sp_step_fee->setSingleStep(0.05);
    sp_step_fee->setValue(0.2);

    auto *de_from = new QDateEdit(QDate::currentDate());
    de_from->setCalendarPopup(true);
    de_from->setDisplayFormat("yyyy-MM-dd");

    auto *de_to = new QDateEdit(QDate::currentDate().addYears(1));
    de_to->setCalendarPopup(true);
    de_to->setDisplayFormat("yyyy-MM-dd");
    de_to->setSpecialValueText(" ");
    de_to->setMinimumDate(QDate(2000, 1, 1));

    auto *cb_over_cap = new QComboBox();
    cb_over_cap->addItem("封顶", 0);
    cb_over_cap->addItem("整池回阶梯", 1);

    auto *chk_active = new QCheckBox("启用");
    chk_active->setChecked(true);

    // ====== 绑定说明（顶部提示，避免用户不知道如何生效）======
    auto *binding_box = new QGroupBox("💡 如何让这份合同生效？—— 两种绑定方式（任选一种）");
    auto *binding_vl = new QVBoxLayout(binding_box);
    binding_vl->setContentsMargins(12, 18, 12, 12);
    binding_vl->setSpacing(6);
    auto *bind_lbl = new QLabel();
    bind_lbl->setWordWrap(true);
    bind_lbl->setTextFormat(Qt::RichText);
    bind_lbl->setStyleSheet("color:#303133;font-size:13px;line-height:1.6;");
    bind_lbl->setText(
        "① <b>【客户级绑定 · 推荐】</b> 去「客户设置」→ 编辑客户 → 「拉均重合同」下拉里选本合同 → "
        "<b>该客户所有订单（无论默认模板是哪一个）都走本合同</b>（优先级最高）。<br>"
        "② <b>【模板级绑定】</b> 上方「绑定运费模板」下拉选一个具体模板（不要选「全部模板通配」）→ "
        "凡匹配到该运费模板的订单，若客户未设置客户级绑定，则自动回退到本合同。"
    );
    binding_vl->addWidget(bind_lbl);

    // ====== 方案A / 方案B 单选 ======
    auto *gb_match_mode = new QGroupBox("📋 进池省份 · 匹配方式");
    auto *gb_match_vl = new QVBoxLayout(gb_match_mode);
    gb_match_vl->setSpacing(10);

    auto *bg_plan = new QButtonGroup(&dlg);
    auto *rb_plan_a = new QRadioButton("○ A. 复用运费模板已有分区（推荐 · 零配置）");
    auto *rb_plan_b = new QRadioButton("○ B. 自定义拉均重专属省份（仅本合同生效）");
    bg_plan->addButton(rb_plan_a, 1);
    bg_plan->addButton(rb_plan_b, 0);
    rb_plan_a->setChecked(true);

    // ====== 方案A 界面 ======
    auto *plan_a_widget = new QWidget();
    auto *plan_a_hl = new QHBoxLayout(plan_a_widget);
    plan_a_hl->setContentsMargins(0, 0, 0, 0);
    plan_a_hl->setSpacing(10);

    // 方案A 左：分区勾选
    auto *a_gb_left = new QGroupBox("1) 勾选参与拉均重的分区");
    a_gb_left->setMinimumWidth(300);
    a_gb_left->setMaximumWidth(300);
    auto *a_left_vl = new QVBoxLayout(a_gb_left);
    auto *a_hint_label = new QLabel();
    a_hint_label->setWordWrap(true);
    a_hint_label->setStyleSheet("color:#909399;padding:4px 6px;background:#f5f7fa;border-radius:4px;font-size:12px;");
    a_left_vl->addWidget(a_hint_label);

    auto *a_scroll = new QScrollArea();
    a_scroll->setWidgetResizable(true);
    a_scroll->setFrameShape(QFrame::NoFrame);
    a_scroll->setStyleSheet("QScrollArea{background:#fafbfc;border:1px solid #ebeef5;border-radius:4px;}");
    auto *a_scroll_widget = new QWidget();
    auto *a_checkbox_layout = new QVBoxLayout(a_scroll_widget);
    a_checkbox_layout->setContentsMargins(6, 6, 6, 6);
    a_checkbox_layout->setSpacing(4);
    a_checkbox_layout->addStretch();
    a_scroll->setWidget(a_scroll_widget);
    a_left_vl->addWidget(a_scroll, 1);

    // 方案A 右：预览+排除省
    auto *a_gb_right = new QGroupBox("2) 分区省份预览 + 排除省黑名单");
    auto *a_right_vl = new QVBoxLayout(a_gb_right);
    auto *a_preview_tip = new QLabel("预览区：根据上方勾选的分区，合并去重的省份如下。对个别想临时剔除的省，点击下方「添加到排除省」。");
    a_preview_tip->setWordWrap(true);
    a_preview_tip->setStyleSheet("color:#606266;font-size:12px;");
    a_right_vl->addWidget(a_preview_tip);

    auto *a_preview_edit = new QPlainTextEdit();
    a_preview_edit->setReadOnly(true);
    a_preview_edit->setFixedHeight(100);
    a_preview_edit->setPlaceholderText("（勾选分区后，此处自动显示合并后的省份列表）");
    a_right_vl->addWidget(a_preview_edit);

    auto *a_excl_bar = new QHBoxLayout();
    auto *a_exclude_combo = new QComboBox();
    a_exclude_combo->setMinimumWidth(140);
    auto *btn_add_exclude = new QPushButton("+ 加入黑名单");
    btn_add_exclude->setCursor(Qt::PointingHandCursor);
    a_excl_bar->addWidget(a_exclude_combo);
    a_excl_bar->addWidget(btn_add_exclude);
    a_excl_bar->addStretch();
    a_right_vl->addLayout(a_excl_bar);

    auto *a_excl_tags_tip = new QLabel("排除省黑名单（点 ✕ 移除）：");
    a_excl_tags_tip->setStyleSheet("color:#606266;font-size:12px;");
    a_right_vl->addWidget(a_excl_tags_tip);

    auto *a_excl_tags_scroll = new QScrollArea();
    a_excl_tags_scroll->setWidgetResizable(true);
    a_excl_tags_scroll->setFrameShape(QFrame::NoFrame);
    a_excl_tags_scroll->setFixedHeight(64);
    auto *a_excl_tags_widget = new QWidget();
    auto *a_excl_tags_layout = new QHBoxLayout(a_excl_tags_widget);
    a_excl_tags_layout->setContentsMargins(2, 2, 2, 2);
    a_excl_tags_layout->setSpacing(6);
    a_excl_tags_layout->addStretch();
    a_excl_tags_scroll->setWidget(a_excl_tags_widget);
    a_right_vl->addWidget(a_excl_tags_scroll);

    plan_a_hl->addWidget(a_gb_left);
    plan_a_hl->addWidget(a_gb_right, 1);

    // ====== 方案B 界面 ======
    auto *plan_b_widget = new QWidget();
    auto *plan_b_vl = new QVBoxLayout(plan_b_widget);
    plan_b_vl->setContentsMargins(0, 0, 0, 0);
    plan_b_vl->setSpacing(8);

    auto *bg_b_poolmode = new QButtonGroup(&dlg);
    auto *rb_b_global = new QRadioButton("○ 全部省份合一个池（默认最简单）");
    auto *rb_b_zones = new QRadioButton("○ 按自定义分区独立分池（每个分区一个均重池）");
    bg_b_poolmode->addButton(rb_b_global, 0);
    bg_b_poolmode->addButton(rb_b_zones, 1);
    rb_b_global->setChecked(true);
    auto *b_mode_hl = new QHBoxLayout();
    b_mode_hl->addWidget(rb_b_global);
    b_mode_hl->addSpacing(24);
    b_mode_hl->addWidget(rb_b_zones);
    b_mode_hl->addStretch();
    plan_b_vl->addLayout(b_mode_hl);

    // 方案B：按分区时的顶部操作条
    auto *b_zones_section = new QWidget();
    auto *b_zones_vl = new QVBoxLayout(b_zones_section);
    b_zones_vl->setContentsMargins(0, 0, 0, 0);
    b_zones_vl->setSpacing(6);

    auto *b_zones_main_hl = new QHBoxLayout();
    b_zones_main_hl->setSpacing(10);

    // 左侧分区列表
    auto *b_zone_col = new QWidget();
    b_zone_col->setMinimumWidth(180);
    b_zone_col->setMaximumWidth(180);
    auto *b_zone_col_vl = new QVBoxLayout(b_zone_col);
    b_zone_col_vl->setContentsMargins(0, 0, 0, 0);
    b_zone_col_vl->setSpacing(4);

    auto *b_zone_btn_bar = new QHBoxLayout();
    b_zone_btn_bar->setSpacing(4);
    auto *btn_b_zone_add = new QPushButton("+新建");
    auto *btn_b_zone_ren = new QPushButton("重命名");
    auto *btn_b_zone_del = new QPushButton("删除");
    btn_b_zone_add->setCursor(Qt::PointingHandCursor);
    btn_b_zone_ren->setCursor(Qt::PointingHandCursor);
    btn_b_zone_del->setCursor(Qt::PointingHandCursor);
    btn_b_zone_add->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    btn_b_zone_ren->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    btn_b_zone_del->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    b_zone_btn_bar->addWidget(btn_b_zone_add);
    b_zone_btn_bar->addWidget(btn_b_zone_ren);
    b_zone_btn_bar->addWidget(btn_b_zone_del);
    b_zone_col_vl->addLayout(b_zone_btn_bar);

    auto *b_zone_list = new QListWidget();
    b_zone_list->setStyleSheet("QListWidget{border:1px solid #ebeef5;border-radius:4px;}");
    b_zone_col_vl->addWidget(b_zone_list, 1);

    // 右侧省份穿梭框
    auto *b_shuttle_col = new QWidget();
    auto *b_shuttle_vl = new QVBoxLayout(b_shuttle_col);
    b_shuttle_vl->setContentsMargins(0, 0, 0, 0);
    b_shuttle_vl->setSpacing(4);

    auto *b_avail_filter = new QLineEdit();
    b_avail_filter->setPlaceholderText("🔍 搜索省名...");
    b_shuttle_vl->addWidget(b_avail_filter);

    auto *b_shuttle_hl = new QHBoxLayout();
    b_shuttle_hl->setSpacing(6);

    auto *b_avail_col_wrap = new QWidget();
    auto *b_avail_col_vl = new QVBoxLayout(b_avail_col_wrap);
    b_avail_col_vl->setContentsMargins(0, 0, 0, 0);
    b_avail_col_vl->setSpacing(4);
    auto *b_avail_list = new QListWidget();
    b_avail_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    b_avail_list->setStyleSheet("QListWidget{border:1px solid #ebeef5;border-radius:4px;}");
    for (const auto &p : ALL_PROVINCES) {
        b_avail_list->addItem(p);
    }
    b_avail_col_vl->addWidget(b_avail_list, 1);
    auto *b_avail_btn_hl = new QHBoxLayout();
    auto *btn_b_avail_all = new QPushButton("全选");
    auto *btn_b_avail_inv = new QPushButton("反选");
    btn_b_avail_all->setCursor(Qt::PointingHandCursor);
    btn_b_avail_inv->setCursor(Qt::PointingHandCursor);
    btn_b_avail_all->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    btn_b_avail_inv->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    b_avail_btn_hl->addWidget(btn_b_avail_all);
    b_avail_btn_hl->addWidget(btn_b_avail_inv);
    b_avail_col_vl->addLayout(b_avail_btn_hl);

    auto *b_mid_btns = new QWidget();
    auto *b_mid_vl = new QVBoxLayout(b_mid_btns);
    b_mid_vl->setContentsMargins(0, 0, 0, 0);
    b_mid_vl->addStretch();
    auto *btn_move_right = new QPushButton(">>");
    auto *btn_move_left = new QPushButton("<<");
    btn_move_right->setCursor(Qt::PointingHandCursor);
    btn_move_left->setCursor(Qt::PointingHandCursor);
    btn_move_right->setFixedWidth(48);
    btn_move_left->setFixedWidth(48);
    b_mid_vl->addWidget(btn_move_right);
    b_mid_vl->addSpacing(8);
    b_mid_vl->addWidget(btn_move_left);
    b_mid_vl->addStretch();

    auto *b_selected_col_wrap = new QWidget();
    auto *b_selected_col_vl = new QVBoxLayout(b_selected_col_wrap);
    b_selected_col_vl->setContentsMargins(0, 0, 0, 0);
    b_selected_col_vl->setSpacing(4);
    auto *b_selected_list = new QListWidget();
    b_selected_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    b_selected_list->setStyleSheet("QListWidget{border:1px solid #ebeef5;border-radius:4px;}");
    b_selected_col_vl->addWidget(b_selected_list, 1);
    auto *b_selected_btn_hl = new QHBoxLayout();
    auto *btn_b_sel_clear = new QPushButton("清空已选");
    btn_b_sel_clear->setCursor(Qt::PointingHandCursor);
    btn_b_sel_clear->setStyleSheet("QPushButton{padding:3px 8px;font-size:12px;}");
    b_selected_btn_hl->addStretch();
    b_selected_btn_hl->addWidget(btn_b_sel_clear);
    b_selected_col_vl->addLayout(b_selected_btn_hl);

    b_shuttle_hl->addWidget(b_avail_col_wrap, 1);
    b_shuttle_hl->addWidget(b_mid_btns);
    b_shuttle_hl->addWidget(b_selected_col_wrap, 1);
    b_shuttle_vl->addLayout(b_shuttle_hl, 1);

    b_zones_main_hl->addWidget(b_zone_col);
    b_zones_main_hl->addWidget(b_shuttle_col, 1);
    b_zones_vl->addLayout(b_zones_main_hl, 1);

    plan_b_vl->addWidget(b_zones_section, 1);

    gb_match_vl->addWidget(rb_plan_a);
    gb_match_vl->addWidget(plan_a_widget);
    gb_match_vl->addWidget(rb_plan_b);
    gb_match_vl->addWidget(plan_b_widget);

    // ============= 辅助 Lambda（先声明为 std::function，解决相互调用顺序问题）=============
    std::function<void()> rebuild_a_preview_and_excl_combo;
    std::function<void()> rebuild_a_checkboxes = [&]() {
        QLayoutItem *child;
        while ((child = a_checkbox_layout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }

        QString cur_tid = template_combo->currentData().toString();
        bool is_wildcard = cur_tid.isEmpty();

        if (is_wildcard) {
            a_hint_label->setText("通配模式：勾选后，每个订单匹配到自己的运费模板分区时生效，模板名前缀用于区分。");
            a_hint_label->setStyleSheet("color:#909399;padding:4px 6px;background:#ecf5ff;border:1px solid #d9ecff;border-radius:4px;font-size:12px;");
        } else {
            auto tm = find_tpl_map(cur_tid);
            auto groups = tm["groups"].toList();
            if (groups.size() == 0) {
                a_hint_label->setText("⚠️ 当前选中模板尚未建立分区，请先到「运费模板」Tab 建立或切换到通配");
                a_hint_label->setStyleSheet("color:#f56c6c;padding:4px 6px;background:#fef0f0;border:1px solid #fde2e2;border-radius:4px;font-size:12px;font-weight:600;");
            } else {
                a_hint_label->setText("勾选后仅下列分区命中的订单可进拉均重池（默认：0勾选=未建分区表→所有分区均可进池）");
                a_hint_label->setStyleSheet("color:#909399;padding:4px 6px;background:#f5f7fa;border-radius:4px;font-size:12px;");
            }
        }

        struct CBItem {
            QString key;
            QString text;
        };
        QList<CBItem> items;

        if (is_wildcard) {
            for (const auto &tv : templates_with_zones) {
                auto t = tv.toMap();
                QString tid = t["template_id"].toString();
                QString tname = t["template_name"].toString();
                auto groups = t["groups"].toList();
                for (const auto &gv : groups) {
                    auto g = gv.toMap();
                    QString key = QString("%1::%2").arg(tid, g["group_code"].toString());
                    QString txt = QString("%1 - %2(%3) - %4省")
                        .arg(tname, g["group_name"].toString(), g["group_code"].toString())
                        .arg(g["province_count"].toInt());
                    items << CBItem{key, txt};
                }
            }
        } else {
            auto tm = find_tpl_map(cur_tid);
            auto groups = tm["groups"].toList();
            for (const auto &gv : groups) {
                auto g = gv.toMap();
                QString key = QString("%1::%2").arg(cur_tid, g["group_code"].toString());
                QString txt = QString("%1(%2) - %3省")
                    .arg(g["group_name"].toString(), g["group_code"].toString())
                    .arg(g["province_count"].toInt());
                items << CBItem{key, txt};
            }
        }

        for (const auto &it : items) {
            auto *cb = new QCheckBox(it.text);
            cb->blockSignals(true);
            cb->setChecked(sel_tpl_groups.contains(it.key));
            cb->setProperty("key", it.key);
            cb->blockSignals(false);
            QObject::connect(cb, &QCheckBox::toggled, &dlg, [&, it](bool checked) {
                if (checked) sel_tpl_groups.insert(it.key);
                else sel_tpl_groups.remove(it.key);
                rebuild_a_preview_and_excl_combo();
            });
            a_checkbox_layout->addWidget(cb);
        }
        a_checkbox_layout->addStretch();
    };

    rebuild_a_preview_and_excl_combo = [&]() {
        QSet<QString> merged_provs_set;
        QMap<QString, QSet<QString>> key_to_provs;

        for (const auto &key : sel_tpl_groups) {
            auto parts = key.split("::");
            if (parts.size() < 2) continue;
            QString tid = parts[0];
            QString gcode = parts[1];
            auto tm = find_tpl_map(tid);
            if (tm.isEmpty()) continue;
            auto groups = tm["groups"].toList();
            for (const auto &gv : groups) {
                auto g = gv.toMap();
                if (g["group_code"].toString() == gcode) {
                    auto provs = g["provinces"].toStringList();
                    for (const auto &p : provs) {
                        merged_provs_set.insert(p);
                        key_to_provs[key].insert(p);
                    }
                    break;
                }
            }
        }

        // 预览文本（排除黑名单中的省份显示）
        QSet<QString> excl_display_provs;
        for (const auto &ek : excl_tplg_provs) {
            auto ps = ek.split("::");
            if (ps.size() >= 3) excl_display_provs.insert(ps[2]);
        }

        QStringList preview_lines;
        QStringList merged_list = merged_provs_set.values();
        std::sort(merged_list.begin(), merged_list.end(), [&](const QString &a, const QString &b) {
            int ia = ALL_PROVINCES.indexOf(a); if (ia < 0) ia = 999;
            int ib = ALL_PROVINCES.indexOf(b); if (ib < 0) ib = 999;
            return ia < ib;
        });
        for (const auto &p : merged_list) {
            if (excl_display_provs.contains(p)) {
                preview_lines << QString("%1 （已排除⚠️）").arg(p);
            } else {
                preview_lines << p;
            }
        }
        a_preview_edit->setPlainText(preview_lines.join("\n"));

        // 刷新 exclude_combo：只列勾选分区合集中有的省（且未被当前加黑的）
        a_exclude_combo->blockSignals(true);
        a_exclude_combo->clear();
        for (const auto &p : merged_list) {
            a_exclude_combo->addItem(p, p);
        }
        a_exclude_combo->blockSignals(false);
    };

    std::function<void()> rebuild_a_excl_tags;
    rebuild_a_excl_tags = [&]() {
        QLayoutItem *child;
        while ((child = a_excl_tags_layout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }

        QStringList keys = excl_tplg_provs.values();
        std::sort(keys.begin(), keys.end());
        for (const auto &key : keys) {
            auto parts = key.split("::");
            QString prov_show;
            if (parts.size() >= 3) prov_show = parts[2];
            else prov_show = key;
            auto *btn = new QPushButton(QString("%1 ✕").arg(prov_show));
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(
                "QPushButton{background:#409eff;color:white;border-radius:12px;padding:3px 12px;border:none;font-size:12px;}"
                "QPushButton:hover{background:#66b1ff;}"
            );
            QObject::connect(btn, &QPushButton::clicked, &dlg, [&, key]() {
                excl_tplg_provs.remove(key);
                rebuild_a_excl_tags();
                rebuild_a_preview_and_excl_combo();
            });
            a_excl_tags_layout->addWidget(btn);
        }
        a_excl_tags_layout->addStretch();
    };

    auto save_b_current_zone_to_mem = [&]() {
        if (rb_b_global->isChecked()) return;
        QString cur_zc;
        if (b_zone_list->currentItem()) cur_zc = b_zone_list->currentItem()->data(Qt::UserRole).toString();
        if (cur_zc.isEmpty()) return;
        QStringList provs;
        for (int i = 0; i < b_selected_list->count(); i++) {
            provs << b_selected_list->item(i)->text();
        }
        mem_b_zones[cur_zc] = provs;
    };

    auto load_b_zone_to_shuttle = [&](const QString &zc) {
        b_selected_list->clear();
        QSet<QString> selected;
        if (mem_b_zones.contains(zc)) {
            for (const auto &p : mem_b_zones[zc]) selected.insert(p);
        }
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            it->setHidden(selected.contains(it->text()));
        }
        QStringList provs = selected.values();
        std::sort(provs.begin(), provs.end(), [&](const QString &a, const QString &b) {
            int ia = ALL_PROVINCES.indexOf(a); if (ia < 0) ia = 999;
            int ib = ALL_PROVINCES.indexOf(b); if (ib < 0) ib = 999;
            return ia < ib;
        });
        for (const auto &p : provs) b_selected_list->addItem(p);
    };

    auto refresh_b_avail_by_filter = [&]() {
        QString kw = b_avail_filter->text().trimmed();
        QSet<QString> selected;
        if (rb_b_global->isChecked()) {
            for (int i = 0; i < b_selected_list->count(); i++) {
                selected.insert(b_selected_list->item(i)->text());
            }
        } else {
            QString cur_zc;
            if (b_zone_list->currentItem()) cur_zc = b_zone_list->currentItem()->data(Qt::UserRole).toString();
            if (!cur_zc.isEmpty() && mem_b_zones.contains(cur_zc)) {
                for (const auto &p : mem_b_zones[cur_zc]) selected.insert(p);
            }
        }
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            bool hide_selected = selected.contains(it->text());
            bool hide_kw = !kw.isEmpty() && !it->text().contains(kw, Qt::CaseInsensitive);
            it->setHidden(hide_selected || hide_kw);
        }
    };

    auto update_plan_b_global_vs_zones = [&]() {
        bool use_zones = rb_b_zones->isChecked();
        b_zone_col->setVisible(use_zones);
        b_zone_btn_bar->setEnabled(use_zones);
        b_zone_list->setEnabled(use_zones);
    };

    auto save_b_global_to_mem = [&]() {
        if (!rb_b_global->isChecked()) return;
        QStringList provs;
        for (int i = 0; i < b_selected_list->count(); i++) {
            provs << b_selected_list->item(i)->text();
        }
        mem_b_zones["__global__"] = provs;
    };

    auto load_b_global_from_mem = [&]() {
        b_selected_list->clear();
        QSet<QString> selected;
        if (mem_b_zones.contains("__global__")) {
            for (const auto &p : mem_b_zones["__global__"]) selected.insert(p);
        }
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            it->setHidden(selected.contains(it->text()));
        }
        QStringList provs = selected.values();
        std::sort(provs.begin(), provs.end(), [&](const QString &a, const QString &b) {
            int ia = ALL_PROVINCES.indexOf(a); if (ia < 0) ia = 999;
            int ib = ALL_PROVINCES.indexOf(b); if (ib < 0) ib = 999;
            return ia < ib;
        });
        for (const auto &p : provs) b_selected_list->addItem(p);
        refresh_b_avail_by_filter();
    };

    auto switch_ab_plan = [&]() {
        bool a_on = rb_plan_a->isChecked();
        plan_a_widget->setEnabled(a_on);
        plan_b_widget->setEnabled(!a_on);
        if (!a_on) save_b_current_zone_to_mem();
    };
    switch_ab_plan();

    // ============= 数据加载（编辑模式）=============
    QString avg_tpl_id_to_edit;
    int saved_reuse_zone_groups = 1;
    if (!is_add && lajz_table_->currentRow() >= 0) {
        avg_tpl_id_to_edit = lajz_table_->item(lajz_table_->currentRow(), 1)->text();
        auto m = repo.GetAvgWeightTemplate(avg_tpl_id_to_edit);
        ed_avg_tpl_id->setText(m["avg_tpl_id"].toString());
        ed_avg_tpl_id->setReadOnly(true);
        ed_name->setText(m["name"].toString());
        ed_contract_no->setText(m["contract_no"].toString());

        QString tpl_id_val = m["template_id"].toString();
        if (tpl_id_val.isEmpty() || tpl_id_val == "*") {
            template_combo->setCurrentIndex(0);
        } else {
            int ti = template_combo->findData(tpl_id_val);
            if (ti >= 0) {
                template_combo->setCurrentIndex(ti);
            } else {
                template_combo->addItem(QString("%1(已不存在⚠️)").arg(tpl_id_val), tpl_id_val);
                template_combo->setCurrentIndex(template_combo->count() - 1);
            }
        }

        sp_version->setValue(m["version"].toInt());
        sp_min_tickets->setValue(m["min_tickets"].toInt());
        sp_base_avg_kg->setValue(m["base_avg_kg"].toDouble());
        sp_avg_pool_max_kg->setValue(m["avg_pool_max_kg"].toDouble());
        sp_avg_fee_cap_kg->setValue(m["avg_fee_cap_kg"].toDouble());
        sp_base_fee->setValue(m["base_fee"].toDouble());
        sp_step_kg->setValue(m["step_kg"].toDouble());
        sp_step_fee->setValue(m["step_fee"].toDouble());

        QString ef = m["effective_from"].toString();
        if (!ef.isEmpty()) de_from->setDate(QDate::fromString(ef, Qt::ISODate));
        QString et = m["effective_to"].toString();
        if (!et.isEmpty()) de_to->setDate(QDate::fromString(et, Qt::ISODate));

        int ocm = m["over_cap_mode"].toInt();
        int idx_ocm = cb_over_cap->findData(ocm);
        if (idx_ocm >= 0) cb_over_cap->setCurrentIndex(idx_ocm);

        saved_reuse_zone_groups = m["reuse_zone_groups"].toInt();
        chk_active->setChecked(m["is_active"].toBool());

        // 方案A：分区勾选 + 排除省黑名单回灌（含按当前 template_combo 做 key 归一化）
        QString cur_tpl_for_lajz = template_combo->currentData().toString();
        bool cur_is_wildcard = cur_tpl_for_lajz.isEmpty();
        QMap<QString, QStringList> avail_keys_by_tpl;   // 内存加速：tpl_id -> [key1,key2...]
        QMap<QString, QStringList> avail_gcode_by_tpl;  // tpl_id -> [gcode1,gcode2...]
        for (const auto &tv : templates_with_zones) {
            auto t = tv.toMap();
            QString tid = t["template_id"].toString();
            auto groups = t["groups"].toList();
            for (const auto &gv : groups) {
                auto g = gv.toMap();
                QString gc = g["group_code"].toString();
                avail_keys_by_tpl[tid] << QString("%1::%2").arg(tid, gc);
                avail_gcode_by_tpl[tid] << gc;
            }
        }
        auto normalized_key = [&](const QString &db_tpl, const QString &gcode) -> QString {
            QString key = QString("%1::%2").arg(db_tpl, gcode);
            if (cur_is_wildcard) {
                // wildcard 模式：找到 UI 里任意模板中存在该 group_code 的任一匹配
                for (const auto &tid : avail_keys_by_tpl.keys()) {
                    QString try_key = QString("%1::%2").arg(tid, gcode);
                    if (avail_keys_by_tpl[tid].contains(try_key)) return try_key;
                }
                return key;
            }
            // specific 模式：强制用当前选择的模板 ID 重建 key（只要该模板里真的有这个 group_code）
            if (avail_gcode_by_tpl.value(cur_tpl_for_lajz).contains(gcode)) {
                return QString("%1::%2").arg(cur_tpl_for_lajz, gcode);
            }
            // 找不到匹配：返回原 key（会在 rebuild 里不显示，但内存保留以防用户切回 wildcard 再保存）
            return key;
        };
        auto normalized_excl_key = [&](const QString &db_tpl, const QString &gcode,
                                       const QString &prov) -> QString {
            QStringList candidate_tids;
            if (cur_is_wildcard) {
                // wildcard: 所有含该 gcode 的模板，挑第一个命中的
                for (const auto &tid : avail_keys_by_tpl.keys()) {
                    if (avail_gcode_by_tpl.value(tid).contains(gcode)) {
                        candidate_tids << tid;
                    }
                }
            } else {
                if (avail_gcode_by_tpl.value(cur_tpl_for_lajz).contains(gcode)) {
                    candidate_tids << cur_tpl_for_lajz;
                }
            }
            // 进一步校验 prov 是否属于 该 tid+gcode 下的 province
            for (const auto &tid : candidate_tids) {
                auto tm = find_tpl_map(tid);
                auto groups = tm["groups"].toList();
                for (const auto &gv : groups) {
                    auto g = gv.toMap();
                    if (g["group_code"].toString() == gcode) {
                        auto provs = g["provinces"].toStringList();
                        if (provs.contains(prov)) return QString("%1::%2::%3").arg(tid, gcode, prov);
                    }
                }
            }
            // 完全找不到则原样返回（内存保留，防止用户改 combo 后出现差异）
            return QString("%1::%2::%3").arg(db_tpl, gcode, prov);
        };

        auto tgl = repo.GetAvgWeightTplGroups(avg_tpl_id_to_edit);
        for (const auto &g : tgl) {
            auto gm = g.toMap();
            sel_tpl_groups.insert(normalized_key(gm["template_id"].toString(),
                                                  gm["group_code"].toString()));
        }
        auto excl = repo.GetAvgWeightExcludes(avg_tpl_id_to_edit);
        for (const auto &e : excl) {
            auto em = e.toMap();
            excl_tplg_provs.insert(normalized_excl_key(
                em["template_id"].toString(),
                em["group_code"].toString(),
                em["province"].toString()));
        }
        auto zones = repo.GetAvgWeightZones(avg_tpl_id_to_edit);
        mem_b_zones.clear();
        b_zone_next_idx = 0;
        for (const auto &z : zones) {
            auto zm = z.toMap();
            QString zc = zm["zone_code"].toString();
            mem_b_zones[zc] = zm["provinces"].toStringList();
            if (zc.startsWith("z") && zc.mid(1).toInt() > b_zone_next_idx) {
                b_zone_next_idx = zc.mid(1).toInt();
            }
        }
    }

    if (saved_reuse_zone_groups == 1) {
        rb_plan_a->setChecked(true);
    } else {
        rb_plan_b->setChecked(true);
    }
    switch_ab_plan();

    // 方案B：初始化分区列表/模式
    {
        QStringList zone_codes = mem_b_zones.keys();
        int count_real_zones = 0;
        for (const auto &zc : zone_codes) {
            if (zc != "__global__") count_real_zones++;
        }
        if (count_real_zones >= 2) {
            rb_b_zones->setChecked(true);
        } else {
            rb_b_global->setChecked(true);
            if (!mem_b_zones.contains("__global__")) {
                QStringList all_global;
                for (const auto &zc : zone_codes) {
                    for (const auto &p : mem_b_zones[zc]) {
                        if (!all_global.contains(p)) all_global << p;
                    }
                }
                mem_b_zones["__global__"] = all_global;
            }
        }
        update_plan_b_global_vs_zones();

        b_zone_list->clear();
        for (const auto &zc : zone_codes) {
            if (zc == "__global__") continue;
            int cnt = mem_b_zones[zc].size();
            QString label = QString("%1(%2省)").arg(zc).arg(cnt);
            auto *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, zc);
            b_zone_list->addItem(item);
        }

        if (rb_b_zones->isChecked()) {
            if (b_zone_list->count() == 0) {
                b_zone_next_idx = 1;
                QString zc = QString("z%1").arg(b_zone_next_idx++);
                mem_b_zones[zc] = QStringList();
                auto *item = new QListWidgetItem(QString("%1(0省)").arg(zc));
                item->setData(Qt::UserRole, zc);
                b_zone_list->addItem(item);
                b_zone_list->setCurrentRow(0);
            } else {
                b_zone_list->setCurrentRow(0);
            }
            save_b_current_zone_to_mem();
            load_b_zone_to_shuttle(b_zone_list->currentItem()->data(Qt::UserRole).toString());
        } else {
            load_b_global_from_mem();
        }
    }

    rebuild_a_checkboxes();
    rebuild_a_preview_and_excl_combo();
    rebuild_a_excl_tags();

    // ============= 连接信号 =============
    QObject::connect(template_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int) {
        rebuild_a_checkboxes();
        rebuild_a_preview_and_excl_combo();
    });

    QObject::connect(btn_add_exclude, &QPushButton::clicked, &dlg, [&]() {
        QString prov = a_exclude_combo->currentData().toString();
        if (prov.isEmpty()) return;
        for (const auto &key : sel_tpl_groups) {
            auto parts = key.split("::");
            if (parts.size() < 2) continue;
            QString tid = parts[0];
            QString gcode = parts[1];
            auto tm = find_tpl_map(tid);
            if (tm.isEmpty()) continue;
            auto groups = tm["groups"].toList();
            for (const auto &gv : groups) {
                auto g = gv.toMap();
                if (g["group_code"].toString() == gcode) {
                    auto provs = g["provinces"].toStringList();
                    if (provs.contains(prov)) {
                        excl_tplg_provs.insert(QString("%1::%2::%3").arg(tid, gcode, prov));
                    }
                    break;
                }
            }
        }
        rebuild_a_excl_tags();
        rebuild_a_preview_and_excl_combo();
    });

    QObject::connect(rb_plan_a, &QRadioButton::toggled, &dlg, [&](bool) { switch_ab_plan(); });
    QObject::connect(rb_plan_b, &QRadioButton::toggled, &dlg, [&](bool) { switch_ab_plan(); });

    QObject::connect(rb_b_global, &QRadioButton::toggled, &dlg, [&](bool checked) {
        if (checked) {
            save_b_current_zone_to_mem();
            update_plan_b_global_vs_zones();
            load_b_global_from_mem();
        }
    });
    QObject::connect(rb_b_zones, &QRadioButton::toggled, &dlg, [&](bool checked) {
        if (checked) {
            save_b_global_to_mem();
            update_plan_b_global_vs_zones();
            if (b_zone_list->count() == 0) {
                b_zone_next_idx = 1;
                QString zc = QString("z%1").arg(b_zone_next_idx++);
                mem_b_zones[zc] = QStringList();
                auto *item = new QListWidgetItem(QString("%1(0省)").arg(zc));
                item->setData(Qt::UserRole, zc);
                b_zone_list->addItem(item);
            }
            b_zone_list->setCurrentRow(0);
            load_b_zone_to_shuttle(b_zone_list->currentItem()->data(Qt::UserRole).toString());
        }
    });

    QObject::connect(b_avail_filter, &QLineEdit::textChanged, &dlg, [&](const QString &) { refresh_b_avail_by_filter(); });

    QObject::connect(btn_move_right, &QPushButton::clicked, &dlg, [&]() {
        QStringList to_move;
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            if (it->isSelected() && !it->isHidden()) to_move << it->text();
        }
        if (to_move.isEmpty()) {
            for (int i = 0; i < b_avail_list->count(); i++) {
                auto *it = b_avail_list->item(i);
                if (!it->isHidden()) to_move << it->text();
            }
        }
        QSet<QString> existing;
        for (int i = 0; i < b_selected_list->count(); i++) existing.insert(b_selected_list->item(i)->text());
        for (const auto &p : to_move) {
            if (existing.contains(p)) continue;
            b_selected_list->addItem(p);
            for (int j = 0; j < b_avail_list->count(); j++) {
                if (b_avail_list->item(j)->text() == p) { b_avail_list->item(j)->setHidden(true); break; }
            }
        }
    });
    QObject::connect(btn_move_left, &QPushButton::clicked, &dlg, [&]() {
        QStringList to_move;
        for (auto *it : b_selected_list->selectedItems()) to_move << it->text();
        if (to_move.isEmpty()) {
            for (int i = 0; i < b_selected_list->count(); i++) to_move << b_selected_list->item(i)->text();
        }
        for (const auto &p : to_move) {
            for (int j = 0; j < b_selected_list->count(); j++) {
                if (b_selected_list->item(j)->text() == p) {
                    delete b_selected_list->takeItem(j); break;
                }
            }
            for (int j = 0; j < b_avail_list->count(); j++) {
                if (b_avail_list->item(j)->text() == p) { b_avail_list->item(j)->setHidden(false); break; }
            }
        }
        refresh_b_avail_by_filter();
    });

    QObject::connect(btn_b_avail_all, &QPushButton::clicked, &dlg, [&]() {
        QSet<QString> existing;
        for (int i = 0; i < b_selected_list->count(); i++) existing.insert(b_selected_list->item(i)->text());
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            if (it->isHidden()) continue;
            QString p = it->text();
            if (existing.contains(p)) continue;
            b_selected_list->addItem(p);
            it->setHidden(true);
        }
    });
    QObject::connect(btn_b_avail_inv, &QPushButton::clicked, &dlg, [&]() {
        QStringList all_visible;
        QSet<QString> visible_selected;
        for (int i = 0; i < b_avail_list->count(); i++) {
            auto *it = b_avail_list->item(i);
            if (it->isHidden()) continue;
            all_visible << it->text();
            if (it->isSelected()) visible_selected.insert(it->text());
        }
        QSet<QString> existing;
        for (int i = 0; i < b_selected_list->count(); i++) existing.insert(b_selected_list->item(i)->text());
        for (const auto &p : all_visible) {
            if (visible_selected.contains(p)) continue;
            if (existing.contains(p)) continue;
            b_selected_list->addItem(p);
            for (int j = 0; j < b_avail_list->count(); j++) {
                if (b_avail_list->item(j)->text() == p) { b_avail_list->item(j)->setHidden(true); break; }
            }
        }
        for (const auto &p : visible_selected) {
            for (int j = 0; j < b_selected_list->count(); j++) {
                if (b_selected_list->item(j)->text() == p) { delete b_selected_list->takeItem(j); break; }
            }
            for (int j = 0; j < b_avail_list->count(); j++) {
                if (b_avail_list->item(j)->text() == p) { b_avail_list->item(j)->setHidden(false); break; }
            }
        }
        refresh_b_avail_by_filter();
    });
    QObject::connect(btn_b_sel_clear, &QPushButton::clicked, &dlg, [&]() {
        QStringList all;
        for (int i = 0; i < b_selected_list->count(); i++) all << b_selected_list->item(i)->text();
        for (const auto &p : all) {
            for (int j = 0; j < b_avail_list->count(); j++) {
                if (b_avail_list->item(j)->text() == p) { b_avail_list->item(j)->setHidden(false); break; }
            }
        }
        b_selected_list->clear();
    });

    QObject::connect(b_zone_list, &QListWidget::currentItemChanged, &dlg, [&](QListWidgetItem *cur, QListWidgetItem *prev) {
        Q_UNUSED(prev);
        save_b_current_zone_to_mem();
        if (cur) {
            QString zc = cur->data(Qt::UserRole).toString();
            load_b_zone_to_shuttle(zc);
        }
    });

    QObject::connect(btn_b_zone_add, &QPushButton::clicked, &dlg, [&]() {
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, "新建分区", "请输入分区名称（如：江浙沪池）:",
            QLineEdit::Normal, QString("分区%1").arg(b_zone_next_idx + 1), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        save_b_current_zone_to_mem();
        b_zone_next_idx++;
        QString zc = QString("z%1").arg(b_zone_next_idx);
        mem_b_zones[zc] = QStringList();
        auto *item = new QListWidgetItem(QString("%1(0省)").arg(name));
        item->setData(Qt::UserRole, zc);
        item->setData(Qt::UserRole + 1, name);
        b_zone_list->addItem(item);
        b_zone_list->setCurrentItem(item);
    });
    QObject::connect(btn_b_zone_ren, &QPushButton::clicked, &dlg, [&]() {
        auto *cur = b_zone_list->currentItem();
        if (!cur) { QMessageBox::warning(&dlg, "提示", "请先选择一个分区"); return; }
        QString old_name;
        if (cur->data(Qt::UserRole + 1).isValid()) old_name = cur->data(Qt::UserRole + 1).toString();
        else old_name = cur->data(Qt::UserRole).toString();
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, "重命名分区", "新名称:", QLineEdit::Normal, old_name, &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        QString zc = cur->data(Qt::UserRole).toString();
        int cnt = mem_b_zones[zc].size();
        cur->setText(QString("%1(%2省)").arg(name).arg(cnt));
        cur->setData(Qt::UserRole + 1, name);
    });
    QObject::connect(btn_b_zone_del, &QPushButton::clicked, &dlg, [&]() {
        auto *cur = b_zone_list->currentItem();
        if (!cur) { QMessageBox::warning(&dlg, "提示", "请先选择一个分区"); return; }
        if (b_zone_list->count() <= 1) { QMessageBox::warning(&dlg, "提示", "至少需要保留一个分区"); return; }
        auto ret = QMessageBox::question(&dlg, "确认删除", "确定删除该分区（连同省份）吗？",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
        QString zc = cur->data(Qt::UserRole).toString();
        mem_b_zones.remove(zc);
        delete cur;
        if (b_zone_list->currentItem()) {
            load_b_zone_to_shuttle(b_zone_list->currentItem()->data(Qt::UserRole).toString());
        }
    });

    // ============= 表单组装 =============
    form->addRow("合同ID(avg_tpl_id):", ed_avg_tpl_id);
    form->addRow("合同名称(name):", ed_name);
    form->addRow("合同编号(contract_no):", ed_contract_no);
    form->addRow("版本(version):", sp_version);
    form->addRow("绑定模板(template_id):", template_combo);
    form->addRow("生效起(effective_from):", de_from);
    form->addRow("生效至(effective_to):", de_to);
    form->addRow("基准均重kg(base_avg_kg):", sp_base_avg_kg);
    form->addRow("进池上限kg(avg_pool_max_kg):", sp_avg_pool_max_kg);
    form->addRow("封顶kg(avg_fee_cap_kg):", sp_avg_fee_cap_kg);
    form->addRow("基础价(base_fee):", sp_base_fee);
    form->addRow("步长kg(step_kg):", sp_step_kg);
    form->addRow("每步加价(step_fee):", sp_step_fee);
    form->addRow("最少票数(min_tickets):", sp_min_tickets);
    form->addRow("超上限模式(over_cap_mode):", cb_over_cap);

    auto *gb_match_wrap = new QVBoxLayout();
    gb_match_wrap->setSpacing(10);
    gb_match_wrap->addWidget(binding_box);
    gb_match_wrap->addWidget(gb_match_mode);
    form->addRow(gb_match_wrap);

    form->addRow("", chk_active);

    layout->addStretch();

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);
    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        QString avg_tpl_id = ed_avg_tpl_id->text().trimmed();
        if (avg_tpl_id.isEmpty()) {
            QMessageBox::warning(this, "提示", "合同ID不能为空");
            return;
        }
        if (ed_name->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "提示", "合同名称不能为空");
            return;
        }

        QVariantMap tpl;
        tpl["avg_tpl_id"] = avg_tpl_id;
        tpl["name"] = ed_name->text().trimmed();
        tpl["contract_no"] = ed_contract_no->text().trimmed();
        tpl["template_id"] = template_combo->currentData().toString();
        tpl["version"] = sp_version->value();
        tpl["effective_from"] = de_from->date().toString(Qt::ISODate);
        tpl["effective_to"] = de_to->date().toString(Qt::ISODate);
        tpl["base_avg_kg"] = sp_base_avg_kg->value();
        tpl["avg_pool_max_kg"] = sp_avg_pool_max_kg->value();
        tpl["avg_fee_cap_kg"] = sp_avg_fee_cap_kg->value();
        tpl["base_fee"] = sp_base_fee->value();
        tpl["step_kg"] = sp_step_kg->value();
        tpl["step_fee"] = sp_step_fee->value();
        tpl["min_tickets"] = sp_min_tickets->value();
        tpl["over_cap_mode"] = cb_over_cap->currentData().toInt();

        bool use_plan_a = rb_plan_a->isChecked();
        tpl["reuse_zone_groups"] = use_plan_a ? 1 : 0;
        tpl["is_active"] = chk_active->isChecked();

        bool ok = repo.SaveAvgWeightTemplate(tpl);

        // ====== 分步骤保存，把每一步返回值单独记录 + 失败时直接在弹窗里显示哪一步失败 ======
        QString err_detail;
        auto record_fail = [&](const QString &step_name, const QString &extra = QString()) {
            err_detail += QString("\n❌ 步骤 [").append(step_name).append("] 失败");
            if (!extra.isEmpty()) err_detail.append("：").append(extra);
            err_detail.append("\n");
        };

        if (ok && use_plan_a) {
            int expected_groups = 0, expected_excl = 0;
            {
                QVariantList groups_vl;
                for (const auto &key : sel_tpl_groups) {
                    auto parts = key.split("::");
                    if (parts.size() < 2) continue;
                    QVariantMap g;
                    g["template_id"] = parts[0];
                    g["group_code"] = parts[1];
                    groups_vl << g;
                }
                expected_groups = groups_vl.size();
                qCritical() << "[LajzSave-DIAG] 方案A SetAvgWeightTplGroups： groups_vl.size ="
                            << groups_vl.size() << "（sel_tpl_groups =" << sel_tpl_groups.values() << "）";
                QString qry_err;
                bool ok_g = repo.SetAvgWeightTplGroups(avg_tpl_id, groups_vl, &qry_err);
                if (!ok_g) {
                    QString db_err = !qry_err.isEmpty() ? qry_err : (
                        repo.Database().lastError().isValid()
                            ? repo.Database().lastError().text()
                            : QStringLiteral("(无Qt数据库错误，查看控制台[SetAvgWeightTplGroups]详细日志)"));
                    record_fail("SetAvgWeightTplGroups",
                                QString("拟写入%1组，内存key=%2，SQLite错误=[%3]")
                                    .arg(groups_vl.size())
                                    .arg(sel_tpl_groups.values().join(","))
                                    .arg(db_err));
                    ok = false;
                }
            }
            {
                QVariantList excl_vl;
                for (const auto &ek : excl_tplg_provs) {
                    auto parts = ek.split("::");
                    if (parts.size() < 3) continue;
                    QVariantMap e;
                    e["template_id"] = parts[0];
                    e["group_code"] = parts[1];
                    e["province"] = parts[2];
                    excl_vl << e;
                }
                expected_excl = excl_vl.size();
                qCritical() << "[LajzSave-DIAG] 方案A SetAvgWeightExcludes： excl_vl.size ="
                            << excl_vl.size() << "（excl_tplg_provs =" << excl_tplg_provs.values() << "）";
                QString qry_err;
                bool ok_e = repo.SetAvgWeightExcludes(avg_tpl_id, excl_vl, &qry_err);
                if (!ok_e) {
                    QString db_err = !qry_err.isEmpty() ? qry_err : (
                        repo.Database().lastError().isValid()
                            ? repo.Database().lastError().text()
                            : QStringLiteral("(无Qt数据库错误，查看控制台[SetAvgWeightExcludes]详细日志)"));
                    record_fail("SetAvgWeightExcludes",
                                QString("拟写入%1个排除省，SQLite错误=[%2]")
                                    .arg(excl_vl.size()).arg(db_err));
                    ok = false;
                }
            }
            QString qry_err_z;
            bool ok_z = repo.SetAvgWeightZones(avg_tpl_id, QVariantList(), &qry_err_z);
            if (!ok_z) {
                QString db_err = !qry_err_z.isEmpty() ? qry_err_z : (
                    repo.Database().lastError().isValid()
                        ? repo.Database().lastError().text()
                        : QStringLiteral("(无Qt数据库错误)"));
                record_fail("SetAvgWeightZones(清空方案B)", db_err);
                ok = false;
            }

            auto tgl_check = repo.GetAvgWeightTplGroups(avg_tpl_id);
            auto excl_check = repo.GetAvgWeightExcludes(avg_tpl_id);
            qCritical() << "[LajzSave-DIAG] 保存后 GetAvgWeightTplGroups 实际 DB size =" << tgl_check.size()
                        << "（预期=" << expected_groups << "）；GetAvgWeightExcludes 实际 DB size =" << excl_check.size()
                        << "（预期=" << expected_excl << "）";
            if (tgl_check.size() != expected_groups) {
                record_fail("方案A-回读校验分区组", QString("预期%1组，DB实得%2组").arg(expected_groups).arg(tgl_check.size()));
                ok = false;
            }
            if (excl_check.size() != expected_excl) {
                record_fail("方案A-回读校验排除省", QString("预期%1个，DB实得%2个").arg(expected_excl).arg(excl_check.size()));
                ok = false;
            }
        } else if (ok) {
            if (rb_b_global->isChecked()) save_b_global_to_mem();
            else save_b_current_zone_to_mem();

            QVariantList zones_vl;
            if (rb_b_global->isChecked()) {
                QVariantMap z;
                z["zone_code"] = "__global__";
                z["provinces"] = mem_b_zones.value("__global__");
                zones_vl << z;
            } else {
                for (int i = 0; i < b_zone_list->count(); i++) {
                    auto *itm = b_zone_list->item(i);
                    QString zc = itm->data(Qt::UserRole).toString();
                    QVariantMap z;
                    z["zone_code"] = zc;
                    z["provinces"] = mem_b_zones.value(zc);
                    zones_vl << z;
                }
            }
            int expected_zones = zones_vl.size();
            qCritical() << "[LajzSave-DIAG] 方案B SetAvgWeightZones： zones_vl.size ="
                        << zones_vl.size();
            for (int i = 0; i < zones_vl.size(); i++) {
                auto z = zones_vl[i].toMap();
                qCritical() << "    [" << i << "] zone=" << z["zone_code"]
                            << "，provinces count=" << z["provinces"].toStringList().size()
                            << "，provinces =" << z["provinces"].toStringList().join(",");
            }
            QString qry_err_z;
            bool ok_z = repo.SetAvgWeightZones(avg_tpl_id, zones_vl, &qry_err_z);
            if (!ok_z) {
                QString db_err = !qry_err_z.isEmpty() ? qry_err_z : (
                    repo.Database().lastError().isValid()
                        ? repo.Database().lastError().text()
                        : QStringLiteral("(查看控制台[SetAvgWeightZones]详细日志)"));
                record_fail("SetAvgWeightZones",
                            QString("拟写入%1个自定义分区，SQLite错误=[%2]")
                                .arg(zones_vl.size()).arg(db_err));
                ok = false;
            }
            QString qry_err_g;
            bool ok_g = repo.SetAvgWeightTplGroups(avg_tpl_id, QVariantList(), &qry_err_g);
            if (!ok_g) {
                QString db_err = !qry_err_g.isEmpty() ? qry_err_g :
                    (repo.Database().lastError().isValid() ? repo.Database().lastError().text() : QString());
                record_fail("SetAvgWeightTplGroups(清空方案A)", db_err);
                ok = false;
            }
            QString qry_err_e;
            bool ok_e = repo.SetAvgWeightExcludes(avg_tpl_id, QVariantList(), &qry_err_e);
            if (!ok_e) {
                QString db_err = !qry_err_e.isEmpty() ? qry_err_e :
                    (repo.Database().lastError().isValid() ? repo.Database().lastError().text() : QString());
                record_fail("SetAvgWeightExcludes(清空方案A)", db_err);
                ok = false;
            }

            auto zones_check = repo.GetAvgWeightZones(avg_tpl_id);
            qCritical() << "[LajzSave-DIAG] 保存后 GetAvgWeightZones 实际 DB zones size ="
                        << zones_check.size() << "（预期=" << expected_zones << "）";
            for (int i = 0; i < zones_check.size(); i++) {
                auto z = zones_check[i].toMap();
                qCritical() << "    [" << i << "] zone=" << z["zone_code"]
                            << "，provinces count=" << z["provinces"].toStringList().size()
                            << "，provinces =" << z["provinces"].toStringList().join(",");
            }
            if (zones_check.size() != expected_zones) {
                record_fail("方案B-回读校验自定义分区", QString("预期%1个分区，DB实得%2个").arg(expected_zones).arg(zones_check.size()));
                ok = false;
            }
        } else {
            QString db_err = repo.Database().lastError().isValid()
                               ? repo.Database().lastError().text()
                               : QStringLiteral("(查看控制台[SaveAvgWeightTemplate]详细日志)");
            record_fail("SaveAvgWeightTemplate(主表写入)",
                        QString("SQLite错误=[%1]，请再查看控制台里 [SaveAvgWeightTemplate] 开头的 SQLite lastError() 详细错误")
                            .arg(db_err));
        }

        LoadLajzTable();
        if (ok) {
            QMessageBox::information(this, "成功", is_add ? "合同已添加" : "合同已更新");
        } else {
            QString msg = "保存失败，具体错误步骤如下：\n————————————\n";
            msg += err_detail;
            msg += "\n————————————\n"
                   "🔍 排查步骤：\n"
                   " ① 请查看控制台（终端）里以 [SaveAvgWeightTemplate] / [SetAvgWeightTplGroups] / \n"
                   "     [SetAvgWeightExcludes] / [SetAvgWeightZones] 开头的行；\n"
                   " ② 把这些 SQLite lastError() 报错文本复制出来，发送给开发者。";
            QMessageBox::warning(this, "保存失败（已记录哪一步出错）", msg);
        }
    }
}

} // namespace freight::ui::dialogs
