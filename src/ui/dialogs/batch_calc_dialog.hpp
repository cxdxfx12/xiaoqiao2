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
#include <QComboBox>
#include <QCheckBox>
#include <QMap>
#include <QVariantMap>

namespace freight::ui {
    class TableSearchBox;
}

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
    void OnDetectTemplate();
    void OnSelectInputAutoOutput(const QString &file);
    void OnPreviewCellDoubleClicked(int row, int col);
    void OnShowDiff();
    void OnManageTemplates();
    void OnTemplatesChangedSyncState();

private:
    struct CalcContext {
        QString normalized_table;
        QString output_table;
        QString output;
        QString result_table;
        QString input_path;
        bool success = false;
        QString error_title;
        QString error_msg;
        int64_t total_rows = 0;
        double total_fee = 0.0;
        int64_t elapsed_ms = 0;
        double previous_total_fee = 0.0;
        qint64 previous_rows = 0;
        int diff_sample_count = 0;
        QMap<QString, QPair<int, double>> current_fee_map;
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
    QComboBox *cbo_recent_ = nullptr;
    QCheckBox *chk_detect_template_ = nullptr;
    QCheckBox *chk_show_diff_ = nullptr;
    QLabel *lbl_template_info_ = nullptr;

    QTableWidget *preview_table_ = nullptr;
    QLabel *lbl_result_summary_ = nullptr;
    QPushButton *btn_export_ = nullptr;
    QPushButton *btn_back_ = nullptr;

    QString output_path_;
    QString result_table_;
    QString detected_template_id_;
    QMap<QString, QString> detected_template_mapping_;
    double last_total_fee_ = 0.0;
    qint64 last_rows_ = 0;
    CalcContext last_calc_context_;
    TableSearchBox *TableSearchBox_ = nullptr;

    QFutureWatcher<CalcContext> *watcher_ = nullptr;
    QTimer *progress_pulse_ = nullptr;
};

} // namespace freight::ui::dialogs
