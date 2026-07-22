#include "ui/dialogs/history_dialog.hpp"
#include "ui/icon_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDateEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QDate>

namespace freight::ui::dialogs {

HistoryDialog::HistoryDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
}

HistoryDialog::~HistoryDialog() = default;

void HistoryDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("历史记录");
    setWindowIcon(icons.CardIcon("history"));
    resize(850, 550);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    // 搜索栏
    auto *search_layout = new QHBoxLayout();
    auto *edt_search = new QLineEdit();
    edt_search->setPlaceholderText("搜索任务名称...");
    auto *date_from = new QDateEdit();
    date_from->setCalendarPopup(true);
    date_from->setDate(QDate::currentDate().addMonths(-1));
    auto *date_to = new QDateEdit();
    date_to->setCalendarPopup(true);
    date_to->setDate(QDate::currentDate());
    auto *btn_search = new QPushButton("搜索");
    btn_search->setIcon(icons.ActionIcon("search"));

    search_layout->addWidget(new QLabel("关键词："));
    search_layout->addWidget(edt_search, 1);
    search_layout->addWidget(new QLabel("日期："));
    search_layout->addWidget(date_from);
    search_layout->addWidget(new QLabel("至"));
    search_layout->addWidget(date_to);
    search_layout->addWidget(btn_search);
    main_layout->addLayout(search_layout);

    // 表格
    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({"任务名称", "时间", "行数", "运费总额", "耗时", "状态", "操作"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    main_layout->addWidget(table_, 1);

    // 统计
    auto *stats = new QLabel("共 0 条记录 | 成功 0 条 | 失败 0 条");
    stats->setStyleSheet("color: #909399; font-size: 12px;");
    main_layout->addWidget(stats);

    // 按钮
    auto *btn_layout = new QHBoxLayout();
    auto *btn_open = new QPushButton(" 打开结果文件");
    btn_open->setIcon(icons.ActionIcon("export"));
    auto *btn_export = new QPushButton(" 导出Excel");
    btn_export->setIcon(icons.ActionIcon("export"));
    auto *btn_delete = new QPushButton(" 删除选中");
    btn_delete->setIcon(icons.ActionIcon("delete"));
    auto *btn_clean = new QPushButton(" 清理历史");
    btn_layout->addWidget(btn_open);
    btn_layout->addWidget(btn_export);
    btn_layout->addWidget(btn_delete);
    btn_layout->addWidget(btn_clean);
    btn_layout->addStretch();

    auto *btn_close = new QPushButton(" 关闭");
    btn_close->setObjectName("normalBtn");
    btn_close->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_close);
    main_layout->addLayout(btn_layout);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);

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

} // namespace freight::ui::dialogs
