#include "ui/dialogs/diff_report_dialog.hpp"
#include "ui/icon_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>

namespace freight::ui::dialogs {

DiffReportDialog::DiffReportDialog(const QString &title,
                                   const DiffSummary &summary,
                                   const QList<QVariantMap> &diff_details,
                                   QWidget *parent)
    : QDialog(parent), summary_(summary), diff_details_(diff_details), title_(title) {
    SetupUI();
    BuildDiffTable(diff_details);
}

QFrame* DiffReportDialog::BuildStatItem(const QString &label, const QString &value,
                                         const QString &color, const QString &char_icon) {
    auto *f = new QFrame();
    f->setStyleSheet(R"QSS(
QFrame { background: white; border-radius: 8px; border: 1px solid #ebeef5; }
    )QSS");
    auto *l = new QHBoxLayout(f);
    l->setContentsMargins(12, 10, 12, 10);
    l->setSpacing(10);
    auto *ic = new QLabel(char_icon);
    ic->setAlignment(Qt::AlignCenter);
    ic->setStyleSheet(QString(
        "font-size: 16px; min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; "
        "background: %1; color: white; border-radius: 10px; font-weight: 600;"
    ).arg(color));
    auto *vb = new QVBoxLayout();
    vb->setSpacing(0);
    auto *lv = new QLabel(label);
    lv->setStyleSheet("color: #909399; font-size: 11px;");
    auto *vv = new QLabel(value);
    vv->setStyleSheet(QString("color: %1; font-size: 16px; font-weight: 700;").arg(color));
    vb->addWidget(lv);
    vb->addWidget(vv);
    l->addWidget(ic);
    l->addLayout(vb, 1);
    return f;
}

void DiffReportDialog::SetupUI() {
    auto &icons = IconManager::Instance();
    setWindowTitle(QString("差异报告 - %1").arg(title_));
    setWindowIcon(icons.ActionIcon("compare"));
    resize(920, 640);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(14);

    auto *header = new QHBoxLayout();
    auto *ic = new QLabel();
    ic->setPixmap(icons.ActionIcon("compare").pixmap(32, 32));
    auto *vbh = new QVBoxLayout();
    vbh->setSpacing(2);
    lbl_title_ = new QLabel(QString("重算差异报告：%1").arg(title_));
    lbl_title_->setStyleSheet("font-size: 18px; font-weight: 600; color: #303133;");
    auto *sub = new QLabel(
        summary_.diff_count == 0 ? "✅ 两次计算结果完全一致！"
                                 : QString("⚠️ 发现 %1 处差异（%2 单新增 / %3 单缺失）")
                                    .arg(summary_.diff_count).arg(summary_.new_count).arg(summary_.missing_count));
    sub->setStyleSheet(summary_.diff_count == 0 ? "color: #67c23a; font-size: 13px;" : "color: #e6a23c; font-size: 13px;");
    vbh->addWidget(lbl_title_);
    vbh->addWidget(sub);
    header->addWidget(ic);
    header->addSpacing(10);
    header->addLayout(vbh, 1);
    main_layout->addLayout(header);

    stat_card_ = new QFrame();
    stat_card_->setStyleSheet(R"QSS(
QFrame { background: white; border-radius: 10px; border: 1px solid #ebeef5; }
    )QSS");
    auto *sl = new QGridLayout(stat_card_);
    sl->setContentsMargins(16, 14, 16, 14);
    sl->setHorizontalSpacing(12);
    sl->setVerticalSpacing(10);

    sl->addWidget(BuildStatItem("总单数", QString::number(summary_.total_rows), "#409eff", "#"), 0, 0);
    sl->addWidget(BuildStatItem("一致单", QString::number(summary_.same_count), "#67c23a", "✓"), 0, 1);
    sl->addWidget(BuildStatItem("差异单", QString::number(summary_.diff_count), "#e6a23c", "≠"), 0, 2);
    sl->addWidget(BuildStatItem("新增单", QString::number(summary_.new_count), "#909399", "+"), 0, 3);

    auto sign = [](double v){ return v >= 0 ? "+" : ""; };
    sl->addWidget(BuildStatItem("旧结果合计", QString("¥%1").arg(summary_.total_fee_old, 0, 'f', 2), "#909399", "A"), 1, 0);
    sl->addWidget(BuildStatItem("新结果合计", QString("¥%1").arg(summary_.total_fee_new, 0, 'f', 2), "#409eff", "B"), 1, 1);
    sl->addWidget(BuildStatItem("差额", QString("%1¥%2").arg(sign(summary_.total_fee_diff)).arg(summary_.total_fee_diff, 0, 'f', 2),
                               summary_.total_fee_diff >= 0 ? "#67c23a" : "#f56c6c", "Δ"), 1, 2);

    auto prog_wrap = new QFrame();
    prog_wrap->setStyleSheet("background: transparent;");
    auto *pl = new QVBoxLayout(prog_wrap);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(4);
    auto *pll = new QLabel("差异影响占比");
    pll->setStyleSheet("color: #909399; font-size: 11px;");
    diff_impact_ = new QProgressBar();
    diff_impact_->setRange(0, 100);
    double impact = summary_.total_fee_old > 0 ? qAbs(summary_.total_fee_diff) / summary_.total_fee_old * 100 : 0;
    diff_impact_->setValue(static_cast<int>(impact));
    diff_impact_->setStyleSheet(R"QSS(
QProgressBar { border: 1px solid #e4e7ed; border-radius: 8px; background: #f5f7fa; height: 22px; text-align: center; font-size: 11px; color: #606266; }
QProgressBar::chunk { background-color: #e6a23c; border-radius: 8px; }
    )QSS");
    diff_impact_->setFormat(QString("¥%1 (%2%)").arg(qAbs(summary_.total_fee_diff), 0, 'f', 0).arg(impact, 0, 'f', 1));
    pl->addWidget(pll);
    pl->addWidget(diff_impact_);
    sl->addWidget(prog_wrap, 1, 3);

    main_layout->addWidget(stat_card_);

    auto *tbl_gb = new QGroupBox("差异明细（前 100 条）");
    tbl_gb->setStyleSheet(R"QSS(
QGroupBox { border: 1px solid #ebeef5; border-radius: 10px; background: white; margin-top: 0; padding-top: 20px; font-weight: 600; color: #303133; font-size: 13px; }
QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; }
    )QSS");
    auto *tl = new QVBoxLayout(tbl_gb);
    tl->setContentsMargins(16, 20, 16, 16);

    diff_table_ = new QTableWidget();
    diff_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diff_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diff_table_->verticalHeader()->setVisible(false);
    diff_table_->horizontalHeader()->setStretchLastSection(true);
    diff_table_->setStyleSheet(R"QSS(
QTableWidget { border: none; gridline-color: #ebeef5; background: white; font-size: 12px; }
QTableWidget::item { padding: 8px; border-bottom: 1px solid #f2f6fc; }
QTableWidget::item:selected { background: #ecf5ff; }
QHeaderView::section { background: #f8f9fa; padding: 10px 8px; border: none; border-bottom: 1px solid #ebeef5; font-weight: 600; color: #606266; font-size: 12px; }
    )QSS");
    tl->addWidget(diff_table_);
    main_layout->addWidget(tbl_gb, 1);

    auto *bl = new QHBoxLayout();
    bl->addStretch();
    btn_export_ = new QPushButton("📄 导出CSV");
    btn_export_->setCursor(Qt::PointingHandCursor);
    btn_export_->setStyleSheet(R"QSS(
QPushButton { padding: 9px 24px; background: #409eff; color: white; border: none; border-radius: 8px; font-size: 13px; font-weight: 500; }
QPushButton:hover { background: #66b1ff; }
    )QSS");
    btn_close_ = new QPushButton("关闭");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(R"QSS(
QPushButton { padding: 9px 24px; background: white; border: 1px solid #dcdfe6; border-radius: 8px; font-size: 13px; color: #606266; }
QPushButton:hover { border-color: #409eff; color: #409eff; }
    )QSS");
    bl->addWidget(btn_export_);
    bl->addWidget(btn_close_);
    main_layout->addLayout(bl);

    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_export_, &QPushButton::clicked, this, &DiffReportDialog::OnExportCSV);

    setStyleSheet(R"QSS(
QDialog {
    background: linear-gradient(180deg, #f0f5ff 0%, #f5f7fa 100%);
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}
    )QSS");
}

void DiffReportDialog::BuildDiffTable(const QList<QVariantMap> &details) {
    diff_table_->setColumnCount(8);
    diff_table_->setHorizontalHeaderLabels({
        "类型", "订单号/标识", "旧运费(¥)", "新运费(¥)", "差额(¥)",
        "旧省份", "新省份", "旧重量→新重量"
    });
    diff_table_->setRowCount(details.size());

    for (int r = 0; r < details.size(); ++r) {
        const auto &m = details[r];
        auto setC = [&](int c, const QString &t, const QString &col = "#606266", bool bold = false) {
            auto *it = new QTableWidgetItem(t);
            it->setTextAlignment(Qt::AlignCenter);
            it->setForeground(QBrush(QColor(col)));
            if (bold) { QFont f = it->font(); f.setBold(true); it->setFont(f); }
            diff_table_->setItem(r, c, it);
        };
        QString type = m.value("type").toString();
        QString type_display = "==";
        QString type_color = "#909399";
        if (type == "diff") { type_display = "≠ 差异"; type_color = "#e6a23c"; }
        else if (type == "new") { type_display = "+ 新增"; type_color = "#409eff"; }
        else if (type == "missing") { type_display = "- 缺失"; type_color = "#f56c6c"; }
        else { type_display = "✓ 一致"; type_color = "#67c23a"; }
        setC(0, type_display, type_color, true);
        setC(1, m.value("id").toString(), "#303133", true);
        setC(2, m.value("old_fee").toString());
        setC(3, m.value("new_fee").toString());
        double diff = m.value("diff_fee").toDouble();
        setC(4, QString("%1%2").arg(diff >= 0 ? "+" : "").arg(diff, 0, 'f', 2),
            diff == 0 ? "#909399" : (diff > 0 ? "#67c23a" : "#f56c6c"), true);
        setC(5, m.value("old_province").toString());
        setC(6, m.value("new_province").toString());
        setC(7, QString("%1 → %2").arg(m.value("old_weight").toString()).arg(m.value("new_weight").toString()));
    }
    diff_table_->resizeColumnsToContents();
    diff_table_->horizontalHeader()->setStretchLastSection(true);
}

void DiffReportDialog::OnExportCSV() {
    QString file = QFileDialog::getSaveFileName(this, "导出差异报告CSV",
        QString("差异报告_%1_%2.csv")
            .arg(title_).arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "CSV文件 (*.csv)");
    if (file.isEmpty()) return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "失败", "无法写入文件");
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "\uFEFF";
    QStringList headers = {"类型","订单号","旧运费","新运费","差额","旧省份","新省份","旧重量","新重量"};
    out << headers.join(",") << "\n";
    for (const auto &m : diff_details_) {
        out << m.value("type").toString() << ","
            << m.value("id").toString() << ","
            << m.value("old_fee").toString() << ","
            << m.value("new_fee").toString() << ","
            << m.value("diff_fee").toString() << ","
            << m.value("old_province").toString() << ","
            << m.value("new_province").toString() << ","
            << m.value("old_weight").toString() << ","
            << m.value("new_weight").toString() << "\n";
    }
    QMessageBox::information(this, "成功", "已导出：\n" + file);
}

} // namespace freight::ui::dialogs
