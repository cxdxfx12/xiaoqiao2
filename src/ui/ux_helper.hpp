#pragma once
#include <QObject>
#include <QTableWidget>
#include <QWidget>
#include <QAction>
#include <QLineEdit>
#include <QLabel>
#include <QColor>
#include <QTimer>
#include <QPoint>
#include <QEvent>

namespace freight::ui {

class TableCopyContext : public QObject {
    Q_OBJECT
public:
    explicit TableCopyContext(QTableWidget *table, QObject *parent = nullptr);

private slots:
    void OnCustomContextMenu(const QPoint &pos);
    void OnCopyCell();
    void OnCopyRow();
    void OnCopyColumn();
    void OnCopyAll();
    void OnCopyAmountWithCurrency();
    void OnLongPressTimer();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString FormatAmount(const QString &raw);
    bool IsAmountColumn(int col);

    QTableWidget *table_ = nullptr;
    QAction *act_copy_cell_ = nullptr;
    QAction *act_copy_row_ = nullptr;
    QAction *act_copy_col_ = nullptr;
    QAction *act_copy_all_ = nullptr;
    QAction *act_copy_yuan_ = nullptr;

    QTimer *long_press_timer_ = nullptr;
    QPoint long_press_pos_;
    bool long_press_fired_ = false;
    int long_press_row_ = -1;
    int long_press_col_ = -1;
};

class TableSearchBox : public QObject {
    Q_OBJECT
public:
    explicit TableSearchBox(QTableWidget *table, QWidget *host, QObject *parent = nullptr);
    void Show();

private slots:
    void OnSearchText(const QString &text);
    void OnNext();
    void OnPrev();
    void OnClose();

private:
    void HighlightMatches(const QString &text);
    void GoToMatch(int direction);

    QTableWidget *table_ = nullptr;
    QWidget *host_ = nullptr;
    QWidget *search_panel_ = nullptr;
    QLineEdit *edt_search_ = nullptr;
    QLabel *lbl_count_ = nullptr;
    QList<QPair<int, int>> matches_;
    int current_match_idx_ = -1;
};

class UxHelper : public QObject {
    Q_OBJECT
public:
    static UxHelper& Instance();

    static void InstallCopyMenu(QTableWidget *table);
    static TableSearchBox* InstallSearch(QTableWidget *table, QWidget *host);
    static void InstallAutoResizeOnDblClick(QTableWidget *table);
    static void InstallFreezeFirstRowToggle(QTableWidget *table);

    static QColor GetFeeColor(double fee, double low, double high);
    static void ApplyFeeColorToTable(QTableWidget *table, int fee_col_idx,
                                     double low = 5.0, double high = 20.0);
    static void ApplyFeeColorToAllAmountColumns(QTableWidget *table,
                                                 double low = 5.0, double high = 20.0);

    static QString FormatCurrency(double amount);

private:
    explicit UxHelper(QObject *parent = nullptr);
    ~UxHelper() override = default;
};

} // namespace freight::ui
