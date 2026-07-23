#include "ux_helper.hpp"
#include "icon_manager.hpp"
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMenu>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeySequence>
#include <QShortcut>
#include <QTableWidgetItem>
#include <QBrush>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QToolTip>

namespace freight::ui {

// ==================== TableCopyContext ====================

TableCopyContext::TableCopyContext(QTableWidget *table, QObject *parent)
    : QObject(parent), table_(table) {
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->installEventFilter(this);
    connect(table_, &QTableWidget::customContextMenuRequested,
            this, &TableCopyContext::OnCustomContextMenu);

    act_copy_cell_ = new QAction("复制单元格", this);
    act_copy_row_ = new QAction("复制整行", this);
    act_copy_col_ = new QAction("复制整列", this);
    act_copy_all_ = new QAction("复制全部（TSV格式）", this);
    act_copy_yuan_ = new QAction("复制 ¥ 金额格式", this);
    act_copy_yuan_->setToolTip("长按单元格0.8秒或右键菜单使用");

    long_press_timer_ = new QTimer(this);
    long_press_timer_->setSingleShot(true);
    long_press_timer_->setInterval(800);
    connect(long_press_timer_, &QTimer::timeout, this, &TableCopyContext::OnLongPressTimer);
}

bool TableCopyContext::eventFilter(QObject *watched, QEvent *event) {
    if (watched != table_->viewport()) return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QModelIndex idx = table_->indexAt(me->pos());
            if (idx.isValid()) {
                long_press_pos_ = me->pos();
                long_press_row_ = idx.row();
                long_press_col_ = idx.column();
                long_press_fired_ = false;
                long_press_timer_->start();
            }
        }
        break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        if (long_press_timer_->isActive()) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseMove &&
                (me->pos() - long_press_pos_).manhattanLength() > 10) {
                long_press_timer_->stop();
            }
            if (event->type() == QEvent::MouseButtonRelease) {
                long_press_timer_->stop();
            }
        }
        break;
    default: break;
    }
    return QObject::eventFilter(watched, event);
}

void TableCopyContext::OnLongPressTimer() {
    if (long_press_row_ < 0 || long_press_col_ < 0) return;
    auto *item = table_->item(long_press_row_, long_press_col_);
    if (!item) return;

    QString text = item->text();
    if (IsAmountColumn(long_press_col_) || text.contains(QRegularExpression(R"(^\d+(\.\d+)?$)"))) {
        text = FormatAmount(text);
    }
    QApplication::clipboard()->setText(text);
    long_press_fired_ = true;

    QString tip = QString("已复制: %1").arg(text);
    QToolTip::showText(QCursor::pos(), tip, table_);
    QTimer::singleShot(1000, [](){ QToolTip::hideText(); });
}

void TableCopyContext::OnCustomContextMenu(const QPoint &pos) {
    auto *item = table_->itemAt(pos);
    if (!item) return;

    QMenu menu(table_);
    menu.addAction(act_copy_cell_);
    menu.addAction(act_copy_row_);
    menu.addAction(act_copy_col_);
    menu.addSeparator();
    menu.addAction(act_copy_yuan_);
    menu.addSeparator();
    menu.addAction(act_copy_all_);

    act_copy_yuan_->setEnabled(IsAmountColumn(item->column()) ||
        item->text().contains(QRegularExpression(R"(^\d+(\.\d+)?$)")));

    QAction *selected = menu.exec(table_->viewport()->mapToGlobal(pos));
    if (selected == act_copy_cell_) OnCopyCell();
    else if (selected == act_copy_row_) OnCopyRow();
    else if (selected == act_copy_col_) OnCopyColumn();
    else if (selected == act_copy_all_) OnCopyAll();
    else if (selected == act_copy_yuan_) OnCopyAmountWithCurrency();
}

void TableCopyContext::OnCopyCell() {
    QList<QTableWidgetItem*> items = table_->selectedItems();
    if (items.isEmpty()) return;
    QString text = items.first()->text();
    QApplication::clipboard()->setText(text);
}

void TableCopyContext::OnCopyRow() {
    int row = table_->currentRow();
    if (row < 0) return;
    QStringList cells;
    for (int c = 0; c < table_->columnCount(); ++c) {
        auto *it = table_->item(row, c);
        cells << (it ? it->text() : "");
    }
    QApplication::clipboard()->setText(cells.join("\t"));
}

void TableCopyContext::OnCopyColumn() {
    int col = table_->currentColumn();
    if (col < 0) return;
    QStringList cells;
    for (int r = 0; r < table_->rowCount(); ++r) {
        auto *it = table_->item(r, col);
        cells << (it ? it->text() : "");
    }
    QApplication::clipboard()->setText(cells.join("\n"));
}

void TableCopyContext::OnCopyAll() {
    QStringList rows;
    QStringList headers;
    for (int c = 0; c < table_->columnCount(); ++c) {
        headers << (table_->horizontalHeaderItem(c) ? table_->horizontalHeaderItem(c)->text()
                    : QString("列%1").arg(c + 1));
    }
    rows << headers.join("\t");
    for (int r = 0; r < table_->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < table_->columnCount(); ++c) {
            auto *it = table_->item(r, c);
            cells << (it ? it->text() : "");
        }
        rows << cells.join("\t");
    }
    QApplication::clipboard()->setText(rows.join("\n"));
}

void TableCopyContext::OnCopyAmountWithCurrency() {
    int row = table_->currentRow();
    int col = table_->currentColumn();
    if (row < 0 || col < 0) return;
    auto *it = table_->item(row, col);
    if (!it) return;
    QApplication::clipboard()->setText(FormatAmount(it->text()));
}

QString TableCopyContext::FormatAmount(const QString &raw) {
    bool ok = false;
    double v = raw.toDouble(&ok);
    if (!ok) return raw;
    return QString("¥ %1").arg(v, 0, 'f', 2);
}

bool TableCopyContext::IsAmountColumn(int col) {
    if (!table_->horizontalHeaderItem(col)) return false;
    QString name = table_->horizontalHeaderItem(col)->text();
    static QStringList keywords = {"运费", "金额", "费用", "fee", "price", "总运费",
                                    "基础运费", "附加费", "加价"};
    for (const auto &k : keywords) {
        if (name.contains(k, Qt::CaseInsensitive)) return true;
    }
    return false;
}

// ==================== TableSearchBox ====================

TableSearchBox::TableSearchBox(QTableWidget *table, QWidget *host, QObject *parent)
    : QObject(parent), table_(table), host_(host) {

    search_panel_ = new QWidget(host_);
    search_panel_->setVisible(false);
    search_panel_->setStyleSheet(R"QSS(
QWidget {
    background: white;
    border: 1px solid #dcdfe6;
    border-radius: 8px;
}
QLineEdit {
    padding: 6px 10px;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
    min-width: 200px;
}
QLineEdit:focus { border-color: #409eff; }
QLabel { border: none; color: #909399; font-size: 12px; }
QPushButton {
    padding: 5px 12px;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
    background: white;
    color: #606266;
    font-size: 12px;
}
QPushButton:hover { border-color: #409eff; color: #409eff; }
    )QSS");

    auto *layout = new QHBoxLayout(search_panel_);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    edt_search_ = new QLineEdit();
    edt_search_->setPlaceholderText("🔍 搜索运单号/省份/金额... (Enter 下一个, Shift+Enter 上一个, Esc 关闭)");
    layout->addWidget(edt_search_, 1);

    lbl_count_ = new QLabel("0 / 0");
    layout->addWidget(lbl_count_);

    auto *btn_prev = new QPushButton("▲ 上一个");
    auto *btn_next = new QPushButton("下一个 ▼");
    auto *btn_close = new QPushButton("✕");
    btn_close->setToolTip("关闭 (Esc)");
    layout->addWidget(btn_prev);
    layout->addWidget(btn_next);
    layout->addWidget(btn_close);

    connect(edt_search_, &QLineEdit::textChanged, this, &TableSearchBox::OnSearchText);
    connect(edt_search_, &QLineEdit::returnPressed, this, [this]() {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier) OnPrev();
        else OnNext();
    });
    connect(btn_next, &QPushButton::clicked, this, &TableSearchBox::OnNext);
    connect(btn_prev, &QPushButton::clicked, this, &TableSearchBox::OnPrev);
    connect(btn_close, &QPushButton::clicked, this, &TableSearchBox::OnClose);

    auto *esc_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), search_panel_);
    connect(esc_shortcut, &QShortcut::activated, this, &TableSearchBox::OnClose);

    search_panel_->resize(560, 44);
}

void TableSearchBox::Show() {
    search_panel_->move(host_->width() - search_panel_->width() - 16, 10);
    search_panel_->raise();
    search_panel_->show();
    edt_search_->setFocus();
    edt_search_->selectAll();
    if (!edt_search_->text().isEmpty()) OnSearchText(edt_search_->text());
}

void TableSearchBox::OnSearchText(const QString &text) {
    HighlightMatches(text);
}

void TableSearchBox::HighlightMatches(const QString &text) {
    matches_.clear();
    current_match_idx_ = -1;

    for (int r = 0; r < table_->rowCount(); ++r) {
        for (int c = 0; c < table_->columnCount(); ++c) {
            auto *item = table_->item(r, c);
            if (!item) continue;
            QBrush original = item->background();
            if (item->data(Qt::UserRole).isValid()) {
                QColor saved = item->data(Qt::UserRole).value<QColor>();
                item->setBackground(saved);
            } else if (original.style() != Qt::NoBrush) {
                item->setData(Qt::UserRole, original.color());
            }
        }
    }

    if (text.isEmpty()) {
        lbl_count_->setText("0 / 0");
        return;
    }

    QColor hi = QColor(255, 235, 59, 160);
    for (int r = 0; r < table_->rowCount(); ++r) {
        for (int c = 0; c < table_->columnCount(); ++c) {
            auto *item = table_->item(r, c);
            if (!item) continue;
            if (item->text().contains(text, Qt::CaseInsensitive)) {
                matches_ << qMakePair(r, c);
                item->setBackground(hi);
            }
        }
    }

    if (!matches_.isEmpty()) {
        current_match_idx_ = 0;
        GoToMatch(0);
    }
    lbl_count_->setText(QString("%1 / %2")
        .arg(current_match_idx_ + 1).arg(matches_.size()));
}

void TableSearchBox::GoToMatch(int direction) {
    if (matches_.isEmpty()) return;
    if (current_match_idx_ >= 0) {
        auto [r, c] = matches_[current_match_idx_];
        auto *old = table_->item(r, c);
        if (old) old->setBackground(QColor(255, 235, 59, 160));
    }
    if (direction == 0) current_match_idx_ = 0;
    else current_match_idx_ = (current_match_idx_ + direction + matches_.size()) % matches_.size();

    auto [r, c] = matches_[current_match_idx_];
    auto *item = table_->item(r, c);
    if (item) item->setBackground(QColor(255, 152, 0, 180));
    table_->setCurrentCell(r, c);
    table_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    lbl_count_->setText(QString("%1 / %2")
        .arg(current_match_idx_ + 1).arg(matches_.size()));
}

void TableSearchBox::OnNext() { GoToMatch(1); }
void TableSearchBox::OnPrev() { GoToMatch(-1); }
void TableSearchBox::OnClose() {
    HighlightMatches("");
    search_panel_->hide();
    table_->setFocus();
}

// ==================== UxHelper ====================

UxHelper::UxHelper(QObject *parent) : QObject(parent) {}

UxHelper& UxHelper::Instance() {
    static UxHelper inst;
    return inst;
}

void UxHelper::InstallCopyMenu(QTableWidget *table) {
    new TableCopyContext(table, table);
}

TableSearchBox* UxHelper::InstallSearch(QTableWidget *table, QWidget *host) {
    auto *sb = new TableSearchBox(table, host, host);
    auto *find_shortcut = new QShortcut(QKeySequence::Find, host);
    QObject::connect(find_shortcut, &QShortcut::activated, sb, &TableSearchBox::Show);
    return sb;
}

void UxHelper::InstallAutoResizeOnDblClick(QTableWidget *table) {
    auto *hheader = table->horizontalHeader();
    if (!hheader) return;
    hheader->setSectionsMovable(true);
    QObject::connect(hheader, &QHeaderView::sectionDoubleClicked,
        [table](int logicalIndex) {
            if (logicalIndex >= 0) {
                table->resizeColumnToContents(logicalIndex);
                int w = table->columnWidth(logicalIndex);
                table->setColumnWidth(logicalIndex, qMax(w + 16, 80));
            }
        });
    auto *vheader = table->verticalHeader();
    if (vheader) {
        QObject::connect(vheader, &QHeaderView::sectionDoubleClicked,
            [table](int logicalIndex) {
                if (logicalIndex >= 0) table->resizeRowToContents(logicalIndex);
            });
    }
}

void UxHelper::InstallFreezeFirstRowToggle(QTableWidget *table) {
    auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), table);
    QObject::connect(shortcut, &QShortcut::activated, [table]() {
        static QMap<QTableWidget*, bool> frozen_map;
        bool &frozen = frozen_map[table];
        frozen = !frozen;
        if (frozen) {
            table->verticalHeader()->setMinimumSectionSize(0);
        }
        if (table->horizontalHeader()) {
            table->horizontalHeader()->setStretchLastSection(!frozen);
        }
    });
}

QColor UxHelper::GetFeeColor(double fee, double low, double high) {
    if (fee <= 0) return QColor("#f56c6c");
    if (fee < low) return QColor("#67c23a");
    if (fee < high) return QColor("#e6a23c");
    return QColor("#f56c6c");
}

void UxHelper::ApplyFeeColorToTable(QTableWidget *table, int fee_col_idx, double low, double high) {
    if (fee_col_idx < 0 || fee_col_idx >= table->columnCount()) return;
    for (int r = 0; r < table->rowCount(); ++r) {
        auto *item = table->item(r, fee_col_idx);
        if (!item) continue;
        bool ok = false;
        double v = item->text().toDouble(&ok);
        if (!ok) continue;
        QColor c = GetFeeColor(v, low, high);
        item->setForeground(c);
        QFont f = item->font();
        f.setBold(v >= high);
        item->setFont(f);
    }
}

void UxHelper::ApplyFeeColorToAllAmountColumns(QTableWidget *table, double low, double high) {
    static QStringList keywords = {"运费", "金额", "费用", "fee", "price", "附加费", "加价"};
    for (int c = 0; c < table->columnCount(); ++c) {
        auto *hitem = table->horizontalHeaderItem(c);
        if (!hitem) continue;
        bool hit = false;
        for (const auto &k : keywords) {
            if (hitem->text().contains(k, Qt::CaseInsensitive)) { hit = true; break; }
        }
        if (hit) ApplyFeeColorToTable(table, c, low, high);
    }
}

QString UxHelper::FormatCurrency(double amount) {
    return QString("¥ %1").arg(amount, 0, 'f', 2);
}

} // namespace freight::ui
