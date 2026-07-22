#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QStackedWidget>

namespace freight::ui::dialogs {

class BatchCalcDialog : public QDialog {
    Q_OBJECT

public:
    explicit BatchCalcDialog(QWidget *parent = nullptr);
    ~BatchCalcDialog() override;

private slots:
    void OnSelectInput();
    void OnSelectOutput();
    void OnStartCalc();
    void OnExport();
    void OnBackToSetup();

private:
    void SetupUI();
    void LoadPreviewData();

    QStackedWidget *stack_ = nullptr;
    QWidget *setup_page_ = nullptr;
    QWidget *preview_page_ = nullptr;

    QLineEdit *edt_input_ = nullptr;
    QLineEdit *edt_output_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QLabel *lbl_status_ = nullptr;
    QPushButton *btn_start_ = nullptr;
    QPushButton *btn_close_ = nullptr;

    QTableWidget *preview_table_ = nullptr;
    QLabel *lbl_result_summary_ = nullptr;
    QPushButton *btn_export_ = nullptr;
    QPushButton *btn_back_ = nullptr;

    QString output_path_;
    QString result_table_;
};

} // namespace freight::ui::dialogs
