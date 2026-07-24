#include "ui/dialogs/courier_template_manager_dialog.hpp"
#include "ui/dialogs/courier_template_edit_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QSignalBlocker>

namespace freight::ui::dialogs {

CourierTemplateManagerDialog::CourierTemplateManagerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("快递模板管理 · 功能7"));
    setMinimumSize(960, 620);
    SetupUI();
    ReloadTable();
}

CourierTemplateManagerDialog::~CourierTemplateManagerDialog() = default;

void CourierTemplateManagerDialog::SetupUI() {
    auto &cfg = core::AppConfig::Instance();
    Q_UNUSED(cfg);
    Q_UNUSED(IconManager::Instance());
    auto *ml = new QVBoxLayout(this);
    ml->setContentsMargins(20, 20, 20, 20);
    ml->setSpacing(14);

    auto *global_box = new QGroupBox(QStringLiteral("整体识别设置"));
    global_box->setStyleSheet(QStringLiteral(R"QSS(
QGroupBox { border: 1px solid #e4e7ed; border-radius: 10px; margin-top: 14px;
    padding: 18px; background: white; font-weight: 600; color: #303133; }
QGroupBox::title { subcontrol-origin: margin; left: 18px; padding: 0 8px; }
    )QSS"));
    auto *gbl = new QHBoxLayout(global_box);
    gbl->setContentsMargins(6, 6, 6, 6);

    chk_global_ = new QCheckBox(QStringLiteral("\u2705 启用快递模板自动识别（总开关）"));
    chk_global_->setChecked(core::AppConfig::Instance().GetTemplateAutoDetectGlobal());
    chk_global_->setCursor(Qt::PointingHandCursor);
    chk_global_->setStyleSheet(
        QStringLiteral("QCheckBox { font-size: 14px; color: #303133; spacing: 8px; }"));
    connect(chk_global_, &QCheckBox::toggled, this, &CourierTemplateManagerDialog::OnToggleGlobal);

    lbl_summary_ = new QLabel();
    lbl_summary_->setStyleSheet(QStringLiteral("color: #909399; font-size: 12px;"));
    gbl->addWidget(chk_global_);
    gbl->addStretch();
    gbl->addWidget(lbl_summary_);
    ml->addWidget(global_box);

    auto *tb = new QHBoxLayout();
    tb->setSpacing(10);

    const char *btnStyle = R"QSS(
QPushButton { padding: 9px 18px; border-radius: 8px; border: 1px solid #dcdfe6; background: white;
    color: #606266; font-size: 13px; font-weight: 500; cursor: pointer; }
QPushButton:hover { border-color: #409eff; color: #409eff; background: #f0f7ff; }
QPushButton#primary { background: #409eff; color: white; border-color: #409eff; }
QPushButton#primary:hover { background: #66b1ff; color: white; }
QPushButton#danger { color: #f56c6c; border-color: #fbc4c4; }
QPushButton#danger:hover { background: #fef0f0; color: #f56c6c; border-color: #f56c6c; }
QPushButton:disabled { color: #c0c4cc; background: #f5f7fa; border-color: #e4e7ed; }
    )QSS";

    btn_add_ = new QPushButton(QStringLiteral("\u2795 新增自定义模板"));
    btn_add_->setCursor(Qt::PointingHandCursor);
    btn_add_->setObjectName(QStringLiteral("primary"));
    btn_add_->setStyleSheet(btnStyle);
    connect(btn_add_, &QPushButton::clicked, this, &CourierTemplateManagerDialog::OnAddTemplate);

    btn_edit_ = new QPushButton(QStringLiteral("\u270f\ufe0f 编辑模板"));
    btn_edit_->setCursor(Qt::PointingHandCursor);
    btn_edit_->setStyleSheet(btnStyle);
    connect(btn_edit_, &QPushButton::clicked, this, &CourierTemplateManagerDialog::OnEditTemplate);

    btn_del_ = new QPushButton(QStringLiteral("\U0001f5d1 删除"));
    btn_del_->setCursor(Qt::PointingHandCursor);
    btn_del_->setObjectName(QStringLiteral("danger"));
    btn_del_->setStyleSheet(btnStyle);
    connect(btn_del_, &QPushButton::clicked, this, &CourierTemplateManagerDialog::OnDeleteTemplate);

    btn_enable_all_ = new QPushButton(QStringLiteral("全部启用"));
    btn_enable_all_->setCursor(Qt::PointingHandCursor);
    btn_enable_all_->setStyleSheet(btnStyle);
    connect(btn_enable_all_, &QPushButton::clicked, this, &CourierTemplateManagerDialog::OnEnableAll);

    btn_disable_all_ = new QPushButton(QStringLiteral("全部禁用"));
    btn_disable_all_->setCursor(Qt::PointingHandCursor);
    btn_disable_all_->setStyleSheet(btnStyle);
    connect(btn_disable_all_, &QPushButton::clicked, this, &CourierTemplateManagerDialog::OnDisableAll);

    btn_close_ = new QPushButton(QStringLiteral("关闭"));
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(btnStyle);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);

    tb->addWidget(btn_add_);
    tb->addWidget(btn_edit_);
    tb->addWidget(btn_del_);
    tb->addSpacing(8);
    tb->addWidget(btn_enable_all_);
    tb->addWidget(btn_disable_all_);
    tb->addStretch();
    tb->addWidget(btn_close_);
    ml->addLayout(tb);

    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({
        QStringLiteral("启用"), QStringLiteral("模板名称"), QStringLiteral("快递公司"),
        QStringLiteral("类型"), QStringLiteral("识别关键词"),
        QStringLiteral("列映射数"), QStringLiteral("操作")
    });
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    table_->setStyleSheet(QStringLiteral(R"QSS(
QTableWidget { border: 1px solid #ebeef5; border-radius: 8px; gridline-color: #ebeef5; background: white; }
QTableWidget::item { padding: 8px; }
QHeaderView::section { background: #f5f7fa; padding: 10px 8px; border: none;
    border-right: 1px solid #ebeef5; border-bottom: 1px solid #ebeef5; font-weight: 600; color: #606266; }
    )QSS"));
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int, int col) {
        if (col == 0) return;
        OnEditTemplate();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != 0) return;
        QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty()) return;
        bool en = item->checkState() == Qt::Checked;
        core::AppConfig::Instance().SetTemplateEnabled(id, en);
        emit TemplatesChanged();
        lbl_summary_->setText(QStringLiteral("已保存：%1 %2")
            .arg(item->data(Qt::UserRole + 1).toString())
            .arg(en ? QStringLiteral("\u2705 启用") : QStringLiteral("\u26d4 禁用")));
    });
    ml->addWidget(table_, 1);

    setStyleSheet(QStringLiteral("QDialog { background-color: #f5f7fa; }"));
}

void CourierTemplateManagerDialog::ReloadTable() {
    auto &cfg = core::AppConfig::Instance();
    const QSignalBlocker blocker(table_);
    auto all = cfg.GetAllTemplateFingerprints(false);
    table_->setRowCount(all.size());
    int builtin_cnt = 0, custom_cnt = 0, enabled_cnt = 0;

    for (int i = 0; i < all.size(); ++i) {
        const auto &t = all[i];
        if (t.is_builtin) builtin_cnt++; else custom_cnt++;
        if (cfg.IsTemplateEnabled(t.template_id)) enabled_cnt++;

        auto *chk_item = new QTableWidgetItem();
        chk_item->setCheckState(cfg.IsTemplateEnabled(t.template_id) ? Qt::Checked : Qt::Unchecked);
        chk_item->setData(Qt::UserRole, t.template_id);
        chk_item->setData(Qt::UserRole + 1, t.display_name);
        chk_item->setTextAlignment(Qt::AlignCenter);
        chk_item->setFlags(chk_item->flags() | Qt::ItemIsUserCheckable);
        table_->setItem(i, 0, chk_item);

        auto *name = new QTableWidgetItem(t.display_name);
        name->setData(Qt::UserRole, t.template_id);
        name->setFont(QFont(QString(), -1, QFont::DemiBold));
        table_->setItem(i, 1, name);

        auto *courier = new QTableWidgetItem(t.courier_name);
        courier->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 2, courier);

        QString type_str = t.is_builtin
            ? QStringLiteral("\U0001f539 内置")
            : QStringLiteral("\U0001f7e2 自定义");
        auto *type = new QTableWidgetItem(type_str);
        type->setTextAlignment(Qt::AlignCenter);
        type->setForeground(t.is_builtin ? QColor("#909399") : QColor("#67c23a"));
        table_->setItem(i, 3, type);

        auto *kw = new QTableWidgetItem(t.required_keywords.join(
            QString::fromUtf8("\u3001")));
        kw->setForeground(QColor("#606266"));
        table_->setItem(i, 4, kw);

        auto *mc = new QTableWidgetItem(QStringLiteral("%1 列").arg(t.column_mapping.size()));
        mc->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 5, mc);

        auto *op = new QTableWidgetItem(QStringLiteral("双击行→编辑   ‖   勾选启用列→立即生效"));
        op->setForeground(QColor("#909399"));
        op->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 6, op);

        if (!cfg.IsTemplateEnabled(t.template_id)) {
            for (int c = 0; c < 7; ++c) {
                auto *it = table_->item(i, c);
                if (it) {
                    QColor fc = it->foreground().color();
                    fc.setAlpha(110);
                    it->setForeground(fc);
                }
            }
        }
    }
    lbl_summary_->setText(QStringLiteral("共 %1 个模板：内置 %2 · 自定义 %3 · 已启用 %4")
        .arg(all.size()).arg(builtin_cnt).arg(custom_cnt).arg(enabled_cnt));
}

core::TemplateFingerprint CourierTemplateManagerDialog::CurrentSelection() const {
    int row = table_->currentRow();
    if (row < 0) return {};
    auto *name = table_->item(row, 1);
    if (!name) return {};
    QString id = name->data(Qt::UserRole).toString();
    auto all = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    for (const auto &t : all) if (t.template_id == id) return t;
    return {};
}

void CourierTemplateManagerDialog::OnToggleGlobal(bool checked) {
    core::AppConfig::Instance().SetTemplateAutoDetectGlobal(checked);
    emit TemplatesChanged();
    lbl_summary_->setText(checked
        ? QStringLiteral("\u2705 自动识别已启用")
        : QStringLiteral("\u26d4 自动识别已关闭，将走标准智能匹配"));
}

void CourierTemplateManagerDialog::OnEnableAll() {
    auto all = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    for (const auto &t : all) core::AppConfig::Instance().SetTemplateEnabled(t.template_id, true);
    ReloadTable();
    emit TemplatesChanged();
}

void CourierTemplateManagerDialog::OnDisableAll() {
    auto ret = QMessageBox::question(this, QStringLiteral("确认"),
        QStringLiteral("确定要禁用所有模板吗？\n\n禁用后将不会匹配任何快递模板，走标准智能列匹配。"));
    if (ret != QMessageBox::Yes) return;
    auto all = core::AppConfig::Instance().GetAllTemplateFingerprints(false);
    for (const auto &t : all) core::AppConfig::Instance().SetTemplateEnabled(t.template_id, false);
    ReloadTable();
    emit TemplatesChanged();
}

void CourierTemplateManagerDialog::OnAddTemplate() {
    CourierTemplateEditDialog dlg(core::TemplateFingerprint(), false, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto r = dlg.GetResult();
        if (r.template_id.isEmpty() || r.display_name.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("模板ID和名称不可为空"));
            return;
        }
        r.is_builtin = false;
        r.enabled = true;
        core::AppConfig::Instance().AddCustomTemplateFingerprint(r);
        core::AppConfig::Instance().SetTemplateEnabled(r.template_id, true);
        ReloadTable();
        emit TemplatesChanged();
    }
}

void CourierTemplateManagerDialog::OnEditTemplate() {
    auto cur = CurrentSelection();
    if (cur.template_id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择要编辑的模板行"));
        return;
    }
    CourierTemplateEditDialog dlg(cur, cur.is_builtin, this);
    if (dlg.exec() == QDialog::Accepted) {
        auto r = dlg.GetResult();
        r.is_builtin = cur.is_builtin;
        if (cur.is_builtin) {
            auto &cfg = core::AppConfig::Instance();
            bool was_en = cfg.IsTemplateEnabled(cur.template_id);
            cfg.RemoveCustomTemplateFingerprint(cur.template_id);
            cfg.AddCustomTemplateFingerprint(r);
            cfg.SetTemplateEnabled(r.template_id, was_en);
        } else {
            core::AppConfig::Instance().UpdateCustomTemplateFingerprint(r);
        }
        ReloadTable();
        emit TemplatesChanged();
    }
}

void CourierTemplateManagerDialog::OnDeleteTemplate() {
    auto cur = CurrentSelection();
    if (cur.template_id.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择要删除的模板行"));
        return;
    }
    if (cur.is_builtin) {
        QMessageBox::warning(this, QStringLiteral("提示"),
            QString::fromUtf8("\uff3b%1\uff3d是系统内置模板，不可以删除哦。\n\n您可以：\n"
                              "\u2460 取消勾选【启用】列禁用它\n"
                              "\u2461 编辑修改后，将生成自定义覆盖版本")
                .arg(cur.display_name));
        return;
    }
    auto ret = QMessageBox::question(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除自定义模板［%1］吗？\n删除后不可恢复。")
            .arg(cur.display_name));
    if (ret != QMessageBox::Yes) return;
    core::AppConfig::Instance().RemoveCustomTemplateFingerprint(cur.template_id);
    ReloadTable();
    emit TemplatesChanged();
}

} // namespace freight::ui::dialogs
