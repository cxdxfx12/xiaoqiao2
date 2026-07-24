#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QDateEdit>
#include <QLabel>
#include <QPushButton>

namespace freight::ui { class TableSearchBox; }

namespace freight::ui::dialogs {

class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);
    ~HistoryDialog() override;

private slots:
    void OnSearch();
    void OnDeleteSelected();
    void OnCleanup();
    void OnOpenFile();
    void OnExport();
    void OnRecalcSelected();

private:
    void SetupUI();
    void LoadHistory();

    QLineEdit *edt_search_ = nullptr;
    QDateEdit *date_from_ = nullptr;
    QDateEdit *date_to_ = nullptr;
    QPushButton *btn_search_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *lbl_stats_ = nullptr;
    QPushButton *btn_open_ = nullptr;
    QPushButton *btn_export_ = nullptr;
    QPushButton *btn_delete_ = nullptr;
    QPushButton *btn_clean_ = nullptr;
    QPushButton *btn_recalc_ = nullptr;
    QPushButton *btn_close_ = nullptr;

    TableSearchBox *search_box_ = nullptr;
};

} // namespace freight::ui::dialogs
