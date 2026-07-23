#include "ui/dialogs/history_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "services/history_service.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QDate>
#include <QMessageBox>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QDebug>

namespace freight::ui::dialogs {

HistoryDialog::HistoryDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
    LoadHistory();
}

HistoryDialog::~HistoryDialog() = default;

void HistoryDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("历史记录");
    setWindowIcon(icons.CardIcon("history"));
    resize(900, 600);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    // 搜索栏
    auto *search_layout = new QHBoxLayout();
    edt_search_ = new QLineEdit();
    edt_search_->setPlaceholderText("搜索任务名称...");
    date_from_ = new QDateEdit();
    date_from_->setCalendarPopup(true);
    date_from_->setDate(QDate::currentDate().addMonths(-1));
    date_to_ = new QDateEdit();
    date_to_->setCalendarPopup(true);
    date_to_->setDate(QDate::currentDate());
    btn_search_ = new QPushButton(" 搜索");
    btn_search_->setIcon(icons.ActionIcon("search"));
    btn_search_->setCursor(Qt::PointingHandCursor);

    search_layout->addWidget(new QLabel("关键词："));
    search_layout->addWidget(edt_search_, 1);
    search_layout->addWidget(new QLabel("日期："));
    search_layout->addWidget(date_from_);
    search_layout->addWidget(new QLabel("至"));
    search_layout->addWidget(date_to_);
    search_layout->addWidget(btn_search_);
    main_layout->addLayout(search_layout);

    // 表格
    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({"任务名称", "时间", "行数", "运费总额", "耗时", "状态", "操作"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setColumnWidth(0, 200);
    table_->setColumnWidth(1, 160);
    table_->setColumnWidth(2, 100);
    table_->setColumnWidth(3, 120);
    table_->setColumnWidth(4, 80);
    table_->setColumnWidth(5, 80);
    main_layout->addWidget(table_, 1);

    // 统计
    lbl_stats_ = new QLabel("共 0 条记录 | 成功 0 条 | 失败 0 条");
    lbl_stats_->setStyleSheet("color: #909399; font-size: 12px;");
    main_layout->addWidget(lbl_stats_);

    // 按钮
    auto *btn_layout = new QHBoxLayout();
    btn_open_ = new QPushButton(" 打开结果文件");
    btn_open_->setIcon(icons.ActionIcon("export"));
    btn_open_->setCursor(Qt::PointingHandCursor);
    btn_export_ = new QPushButton(" 导出Excel");
    btn_export_->setIcon(icons.ActionIcon("export"));
    btn_export_->setCursor(Qt::PointingHandCursor);
    btn_delete_ = new QPushButton(" 删除选中");
    btn_delete_->setIcon(icons.ActionIcon("delete"));
    btn_delete_->setCursor(Qt::PointingHandCursor);
    btn_clean_ = new QPushButton(" 清理历史");
    btn_clean_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_open_);
    btn_layout->addWidget(btn_export_);
    btn_layout->addWidget(btn_delete_);
    btn_layout->addWidget(btn_clean_);
    btn_layout->addStretch();

    btn_close_ = new QPushButton(" 关闭");
    btn_close_->setObjectName("normalBtn");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_close_);
    main_layout->addLayout(btn_layout);

    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_search_, &QPushButton::clicked, this, &HistoryDialog::OnSearch);
    connect(edt_search_, &QLineEdit::returnPressed, this, &HistoryDialog::OnSearch);
    connect(btn_delete_, &QPushButton::clicked, this, &HistoryDialog::OnDeleteSelected);
    connect(btn_clean_, &QPushButton::clicked, this, &HistoryDialog::OnCleanup);
    connect(btn_open_, &QPushButton::clicked, this, &HistoryDialog::OnOpenFile);
    connect(btn_export_, &QPushButton::clicked, this, &HistoryDialog::OnExport);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0) return;
        QString output_file = table_->item(row, 0)->data(Qt::UserRole).toString();
        if (!output_file.isEmpty() && QFileInfo::exists(output_file)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(output_file).absolutePath()));
        }
    });

    setStyleSheet(R"QSS(
QDialog { background-color: #f5f7fa; }
QLineEdit, QDateEdit {
    padding: 6px 10px;
    border: 1px solid #dcdfe6;
    border-radius: 4px;
    background: white;
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

void HistoryDialog::LoadHistory() {
    auto &cfg = core::AppConfig::Instance();
    services::HistoryService history_svc(cfg.GetHistoryDbPath());
    history_svc.Init();

    QString keyword = edt_search_->text().trimmed();
    QDate from = date_from_->date();
    QDate to = date_to_->date();

    QVariantList records = history_svc.QueryHistory(1, 100, keyword, from, to);

    table_->setRowCount(records.size());

    int success_count = 0;
    int fail_count = 0;

    for (int i = 0; i < records.size(); i++) {
        QVariantMap r = records[i].toMap();
        int status = r["status"].toInt();

        if (status == 1) success_count++;
        else fail_count++;

        auto *name_item = new QTableWidgetItem(r["task_name"].toString());
        name_item->setData(Qt::UserRole, r["output_file"].toString());
        name_item->setData(Qt::UserRole + 1, r["id"].toLongLong());
        table_->setItem(i, 0, name_item);

        table_->setItem(i, 1, new QTableWidgetItem(r["created_at"].toString()));
        table_->setItem(i, 2, new QTableWidgetItem(QString::number(r["total_rows"].toInt())));

        auto *fee_item = new QTableWidgetItem("¥ " + QString::number(r["total_fee"].toDouble(), 'f', 2));
        fee_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 3, fee_item);

        int duration_ms = r["duration_ms"].toInt();
        QString duration_str;
        if (duration_ms < 1000) {
            duration_str = QString("%1ms").arg(duration_ms);
        } else if (duration_ms < 60000) {
            duration_str = QString("%1s").arg(duration_ms / 1000.0, 0, 'f', 1);
        } else {
            duration_str = QString("%1m%2s").arg(duration_ms / 60000).arg((duration_ms % 60000) / 1000);
        }
        table_->setItem(i, 4, new QTableWidgetItem(duration_str));

        QTableWidgetItem *status_item;
        if (status == 1) {
            status_item = new QTableWidgetItem("✅ 成功");
            status_item->setForeground(QColor("#67c23a"));
        } else {
            status_item = new QTableWidgetItem("❌ 失败");
            status_item->setForeground(QColor("#f56c6c"));
        }
        status_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 5, status_item);

        table_->setItem(i, 6, new QTableWidgetItem("双击打开文件夹"));
        table_->item(i, 6)->setForeground(QColor("#909399"));
        table_->item(i, 6)->setTextAlignment(Qt::AlignCenter);
    }

    lbl_stats_->setText(QString("共 %1 条记录 | 成功 %2 条 | 失败 %3 条")
        .arg(records.size()).arg(success_count).arg(fail_count));
}

void HistoryDialog::OnSearch() {
    LoadHistory();
}

void HistoryDialog::OnDeleteSelected() {
    QList<QTableWidgetItem *> selected = table_->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的记录");
        return;
    }

    QSet<int> rows;
    for (auto *item : selected) {
        rows.insert(item->row());
    }

    auto ret = QMessageBox::question(this, "确认删除",
        QString("确定要删除选中的 %1 条历史记录吗？").arg(rows.size()));
    if (ret != QMessageBox::Yes) return;

    auto &cfg = core::AppConfig::Instance();
    services::HistoryService history_svc(cfg.GetHistoryDbPath());
    history_svc.Init();

    QList<qint64> ids;
    for (int row : rows) {
        qint64 id = table_->item(row, 0)->data(Qt::UserRole + 1).toLongLong();
        if (id > 0) {
            ids << id;
        }
    }

    history_svc.DeleteHistoryList(ids);
    LoadHistory();
    QMessageBox::information(this, "提示", "已删除选中记录");
}

void HistoryDialog::OnCleanup() {
    auto ret = QMessageBox::question(this, "清理历史",
        "确定要清理 90 天之前的历史记录吗？\n此操作不可恢复。");
    if (ret != QMessageBox::Yes) return;

    auto &cfg = core::AppConfig::Instance();
    services::HistoryService history_svc(cfg.GetHistoryDbPath());
    history_svc.Init();

    int count = history_svc.CleanupOldData(90);
    LoadHistory();
    QMessageBox::information(this, "提示", QString("已清理 %1 条历史记录").arg(count));
}

void HistoryDialog::OnOpenFile() {
    int row = table_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选择一条记录");
        return;
    }

    QString output_file = table_->item(row, 0)->data(Qt::UserRole).toString();
    if (output_file.isEmpty() || !QFileInfo::exists(output_file)) {
        QMessageBox::warning(this, "提示", "结果文件不存在");
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(output_file));
}

void HistoryDialog::OnExport() {
    int row = table_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选择一条记录");
        return;
    }

    QString output_file = table_->item(row, 0)->data(Qt::UserRole).toString();
    if (output_file.isEmpty() || !QFileInfo::exists(output_file)) {
        QMessageBox::warning(this, "提示", "结果文件不存在");
        return;
    }

    QString save_path = QFileDialog::getSaveFileName(this, "导出结果",
        QFileInfo(output_file).fileName(),
        "Excel文件 (*.xlsx);;CSV文件 (*.csv);;所有文件 (*.*)");
    if (save_path.isEmpty()) return;

    QFile::copy(output_file, save_path);
    QMessageBox::information(this, "成功", "文件已导出到：" + save_path);
}

} // namespace freight::ui::dialogs
