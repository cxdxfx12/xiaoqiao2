#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QStackedWidget>
#include <QFutureWatcher>
#include <QTimer>
#include <QString>
#include <cstdint>

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
    void OnCalcFinished();
    void OnProgressPulse();

private:
    struct CalcContext {
        QString normalized_table;
        QString output_table;
        QString output;
        QString result_table;
        bool success = false;
        QString error_title;
        QString error_msg;
        int64_t total_rows = 0;
        double total_fee = 0.0;
        int64_t elapsed_ms = 0;
    };

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

    QFutureWatcher<CalcContext> *watcher_ = nullptr;
    QTimer *progress_pulse_ = nullptr;
};

} // namespace freight::ui::dialogs
