#include "ui/dialogs/courier_template_edit_dialog.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QLabel>
#include <QItemSelectionModel>

namespace freight::ui::dialogs {

CourierTemplateEditDialog::CourierTemplateEditDialog(const core::TemplateFingerprint &existing,
                                                     bool is_builtin, QWidget *parent)
    : QDialog(parent), orig_(existing), is_builtin_(is_builtin) {
    is_new_ = existing.template_id.isEmpty();
    setWindowTitle(is_new_ ? QStringLiteral("新增自定义快递模板 · 功能7")
                           : QStringLiteral("编辑模板：%1 %2").arg(existing.display_name)
                                 .arg(is_builtin ? QStringLiteral("(内置，保存后生成自定义覆盖版)")
                                                 : QStringLiteral("(自定义)")));
    setMinimumSize(780, 680);
    SetupUI();
    LoadFromFingerprint();
}

CourierTemplateEditDialog::~CourierTemplateEditDialog() = default;

core::TemplateFingerprint CourierTemplateEditDialog::GetResult() const { return result_; }

void CourierTemplateEditDialog::SetupUI() {
    auto *ml = new QVBoxLayout(this);
    ml->setContentsMargins(20, 20, 20, 20);
    ml->setSpacing(16);

    if (is_builtin_ && !is_new_) {
        lbl_builtin_ = new QLabel(QString::fromUtf8(
            "\u2139\ufe0f  这是系统内置模板，修改后会生成一份自定义覆盖版本，\n"
            "内置原模板保留，识别时优先使用您的自定义版本。相同模板ID的自定义优先于内置。"));
        lbl_builtin_->setStyleSheet(QStringLiteral(R"QSS(
QLabel { padding: 12px 16px; border: 1px solid #faecd8; background: #fdf6ec;
    color: #e6a23c; border-radius: 8px; font-size: 13px; line-height: 1.7; }
        )QSS"));
        lbl_builtin_->setWordWrap(true);
        ml->addWidget(lbl_builtin_);
    }

    const char *inputStyle = R"QSS(
QLineEdit, QTextEdit, QTableWidget {
    padding: 8px 12px; border: 1px solid #dcdfe6; border-radius: 8px;
    background: white; font-size: 13px; color: #303133; min-height: 24px;
}
QLineEdit:focus, QTextEdit:focus, QTableWidget:focus { border-color: #409eff; }
QLineEdit:disabled { background: #f5f7fa; color: #c0c4cc; }
    )QSS";

    auto *gb_info = new QGroupBox(QStringLiteral("\U0001f4cc 模板基础信息"));
    gb_info->setStyleSheet(QStringLiteral(R"QSS(
QGroupBox { border: 1px solid #e4e7ed; border-radius: 10px; margin-top: 14px;
    padding: 18px; background: white; font-weight: 600; color: #303133; }
QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; }
    )QSS"));
    auto *fl = new QFormLayout(gb_info);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fl->setHorizontalSpacing(16);
    fl->setVerticalSpacing(14);

    edt_id_ = new QLineEdit();
    edt_id_->setPlaceholderText(QStringLiteral("英文+下划线唯一ID，如：your_company_v1"));
    edt_id_->setStyleSheet(inputStyle);
    if (is_builtin_) edt_id_->setEnabled(false);
    fl->addRow(QStringLiteral("模板ID *"), edt_id_);

    edt_name_ = new QLineEdit();
    edt_name_->setPlaceholderText(QStringLiteral("显示名称，如：中通-XX客户专用"));
    edt_name_->setStyleSheet(inputStyle);
    fl->addRow(QStringLiteral("显示名称 *"), edt_name_);

    edt_courier_ = new QLineEdit();
    edt_courier_->setPlaceholderText(QStringLiteral("快递公司简称，如：中通 / 圆通 / 极兔"));
    edt_courier_->setStyleSheet(inputStyle);
    fl->addRow(QStringLiteral("快递公司 *"), edt_courier_);

    edt_keywords_ = new QTextEdit();
    edt_keywords_->setPlaceholderText(QString::fromUtf8(
        "用顿号（、）或逗号分隔，用于匹配此模板：\n"
        "例如：中通、ZTO、XX客户专用、客户单号、XX网点"));
    edt_keywords_->setStyleSheet(inputStyle);
    edt_keywords_->setFixedHeight(80);
    auto *kw_help = new QLabel(QString::fromUtf8(
        "\U0001f4a1 匹配度 = 列名关键词命中(60%) + 前3行内容特征(40%)；关键词越多越精准"));
    kw_help->setStyleSheet(QStringLiteral("color: #909399; font-size: 12px;"));
    auto *kw_vb = new QVBoxLayout();
    kw_vb->setSpacing(4);
    kw_vb->addWidget(edt_keywords_);
    kw_vb->addWidget(kw_help);
    fl->addRow(QStringLiteral("识别关键词 *"), kw_vb);
    ml->addWidget(gb_info);

    auto *gb_map = new QGroupBox(QStringLiteral("\U0001f517 原始列名 → 标准列 映射"));
    gb_map->setStyleSheet(gb_info->styleSheet());
    auto *map_vb = new QVBoxLayout(gb_map);
    map_vb->setSpacing(10);

    auto *map_top = new QHBoxLayout();
    auto *help = new QLabel(QString::fromUtf8(
        "左列填Excel里的原始列名，右列填对应的系统标准列。\n"
        "命中的列越多，识别准确率越高。至少需要包含省份+重量。"));
    help->setStyleSheet(QStringLiteral("color: #606266; font-size: 13px; line-height: 1.6;"));
    help->setWordWrap(true);
    map_top->addWidget(help, 1);

    btn_add_mapping_ = new QPushButton(QStringLiteral("\u2795 加一行"));
    btn_del_mapping_ = new QPushButton(QStringLiteral("\u2796 删选中"));
    const char *btnStyle = R"QSS(
QPushButton { padding: 7px 16px; border-radius: 7px; border: 1px solid #dcdfe6;
    background: white; color: #606266; font-size: 12px; font-weight: 500; cursor: pointer; }
QPushButton:hover { border-color: #409eff; color: #409eff; background: #f0f7ff; }
    )QSS";
    btn_add_mapping_->setStyleSheet(btnStyle);
    btn_del_mapping_->setStyleSheet(btnStyle);
    btn_add_mapping_->setCursor(Qt::PointingHandCursor);
    btn_del_mapping_->setCursor(Qt::PointingHandCursor);
    connect(btn_add_mapping_, &QPushButton::clicked, this, &CourierTemplateEditDialog::OnAddMapping);
    connect(btn_del_mapping_, &QPushButton::clicked, this, &CourierTemplateEditDialog::OnRemoveMapping);
    map_top->addWidget(btn_add_mapping_);
    map_top->addWidget(btn_del_mapping_);
    map_vb->addLayout(map_top);

    tbl_mapping_ = new QTableWidget(0, 2);
    tbl_mapping_->setHorizontalHeaderLabels({QStringLiteral("Excel原始列名（文件里真实的列头）"),
                                              QStringLiteral("→ 系统标准列")});
    tbl_mapping_->horizontalHeader()->setStretchLastSection(true);
    tbl_mapping_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tbl_mapping_->setAlternatingRowColors(true);
    tbl_mapping_->setStyleSheet(inputStyle);
    tbl_mapping_->verticalHeader()->setVisible(false);
    tbl_mapping_->verticalHeader()->setDefaultSectionSize(36);
    map_vb->addWidget(tbl_mapping_, 1);

    auto *tips = new QLabel(QString::fromUtf8(
        "常用标准列：order_id(运单号) · dest_province(目的省份) · dest_city(目的城市) · "
        "weight(实际重量) · vol_weight(体积重量) · customer_id(客户编号) · "
        "dest_area(区县) · length/width/height(长宽高)"));
    tips->setStyleSheet(QStringLiteral("color: #909399; font-size: 11px; padding: 4px 2px;"));
    tips->setWordWrap(true);
    map_vb->addWidget(tips);
    ml->addWidget(gb_map, 1);

    auto *btn_bl = new QHBoxLayout();
    btn_bl->addStretch();
    const char *okCancel = R"QSS(
QPushButton { padding: 10px 30px; border-radius: 8px; border: 1px solid #dcdfe6;
    background: white; color: #606266; font-size: 14px; font-weight: 500; cursor: pointer; }
QPushButton:hover { border-color: #409eff; color: #409eff; }
QPushButton#primary { background: #409eff; color: white; border-color: #409eff; }
QPushButton#primary:hover { background: #66b1ff; color: white; }
    )QSS";
    btn_cancel_ = new QPushButton(QStringLiteral("取消"));
    btn_cancel_->setCursor(Qt::PointingHandCursor);
    btn_cancel_->setStyleSheet(okCancel);
    connect(btn_cancel_, &QPushButton::clicked, this, &QDialog::reject);

    btn_ok_ = new QPushButton(QStringLiteral("\U0001f4be 保存模板"));
    btn_ok_->setCursor(Qt::PointingHandCursor);
    btn_ok_->setObjectName(QStringLiteral("primary"));
    btn_ok_->setStyleSheet(okCancel);
    connect(btn_ok_, &QPushButton::clicked, this, &CourierTemplateEditDialog::OnOk);

    btn_bl->addWidget(btn_cancel_);
    btn_bl->addWidget(btn_ok_);
    ml->addLayout(btn_bl);

    setStyleSheet(QStringLiteral("QDialog { background-color: #f5f7fa; }"));
}

void CourierTemplateEditDialog::LoadFromFingerprint() {
    edt_id_->setText(orig_.template_id);
    edt_name_->setText(orig_.display_name);
    edt_courier_->setText(orig_.courier_name);
    edt_keywords_->setText(orig_.required_keywords.join(QStringLiteral("、")));

    QSignalBlocker b(tbl_mapping_);
    auto addRow = [&](const QString &from, const QString &to) {
        int r = tbl_mapping_->rowCount();
        tbl_mapping_->insertRow(r);
        tbl_mapping_->setItem(r, 0, new QTableWidgetItem(from));
        tbl_mapping_->setItem(r, 1, new QTableWidgetItem(to));
    };

    if (orig_.column_mapping.isEmpty()) {
        addRow({}, {});
        addRow({}, {});
        addRow({}, {});
        addRow({}, {});
    } else {
        for (auto it = orig_.column_mapping.begin(); it != orig_.column_mapping.end(); ++it) {
            addRow(it.key(), it.value());
        }
        addRow({}, {});
    }

    if (is_new_) {
        edt_id_->setText(QStringLiteral("custom_") +
                         QDateTime::currentDateTime().toString(QStringLiteral("MMdd_hhmmss")));
    }
}

void CourierTemplateEditDialog::OnAddMapping() {
    int r = tbl_mapping_->rowCount();
    tbl_mapping_->insertRow(r);
    tbl_mapping_->setItem(r, 0, new QTableWidgetItem({}));
    tbl_mapping_->setItem(r, 1, new QTableWidgetItem({}));
    tbl_mapping_->setCurrentCell(r, 0);
}

void CourierTemplateEditDialog::OnRemoveMapping() {
    QList<int> rows;
    auto *sm = tbl_mapping_->selectionModel();
    if (sm) {
        for (const auto &idx : sm->selectedRows()) {
            if (!rows.contains(idx.row())) rows << idx.row();
        }
    }
    for (const auto &idx : tbl_mapping_->selectedItems()) {
        if (!rows.contains(idx->row())) rows << idx->row();
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) tbl_mapping_->removeRow(r);
    if (tbl_mapping_->rowCount() == 0) OnAddMapping();
}

void CourierTemplateEditDialog::OnOk() {
    result_ = orig_;
    result_.template_id = edt_id_->text().trimmed();
    result_.display_name = edt_name_->text().trimmed();
    result_.courier_name = edt_courier_->text().trimmed();

    QString kw_raw = edt_keywords_->toPlainText().trimmed();
    kw_raw.replace(QChar(0xff0c), QChar(0x3001)).replace(',', QChar(0x3001))
        .replace(';', QChar(0x3001)).replace(QChar(0xff1b), QChar(0x3001))
        .replace('\n', QChar(0x3001));
    QStringList kws;
    for (const auto &k : kw_raw.split(QChar(0x3001), Qt::SkipEmptyParts)) {
        QString kk = k.trimmed();
        if (!kk.isEmpty() && !kws.contains(kk)) kws << kk;
    }
    result_.required_keywords = kws;

    QMap<QString, QString> mp;
    for (int i = 0; i < tbl_mapping_->rowCount(); ++i) {
        auto *from = tbl_mapping_->item(i, 0);
        auto *to = tbl_mapping_->item(i, 1);
        QString f = from ? from->text().trimmed() : QString();
        QString t = to ? to->text().trimmed() : QString();
        if (!f.isEmpty() && !t.isEmpty()) mp[f] = t;
    }
    result_.column_mapping = mp;
    result_.enabled = true;
    result_.is_builtin = false;

    if (result_.template_id.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("模板ID 不能为空"));
        edt_id_->setFocus(); return;
    }
    if (result_.display_name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("显示名称 不能为空"));
        edt_name_->setFocus(); return;
    }
    if (result_.courier_name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("快递公司 不能为空"));
        edt_courier_->setFocus(); return;
    }
    if (kws.size() < 1) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请至少填 1 个识别关键词"));
        edt_keywords_->setFocus(); return;
    }
    if (mp.size() < 2) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("至少需要 2 组列映射，建议包含【省份】+【重量】"));
        return;
    }
    accept();
}

} // namespace freight::ui::dialogs
