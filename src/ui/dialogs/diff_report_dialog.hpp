#pragma once
#include <QDialog>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QTableWidget>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

namespace freight::ui::dialogs {

struct DiffSummary {
    qint64 total_rows = 0;
    qint64 same_count = 0;
    qint64 diff_count = 0;
    qint64 new_count = 0;
    qint64 missing_count = 0;
    double total_fee_old = 0;
    double total_fee_new = 0;
    double total_fee_diff = 0;
};

class DiffReportDialog : public QDialog {
    Q_OBJECT
public:
    DiffReportDialog(const QString &title,
                     const DiffSummary &summary,
                     const QList<QVariantMap> &diff_details,
                     QWidget *parent = nullptr);

private slots:
    void OnExportCSV();

private:
    void SetupUI();
    QFrame* BuildStatItem(const QString &label, const QString &value,
                          const QString &color, const QString &char_icon);
    void BuildDiffTable(const QList<QVariantMap> &details);

    DiffSummary summary_;
    QList<QVariantMap> diff_details_;
    QString title_;

    QLabel *lbl_title_ = nullptr;
    QFrame *stat_card_ = nullptr;
    QTableWidget *diff_table_ = nullptr;
    QProgressBar *diff_impact_ = nullptr;
    QPushButton *btn_export_ = nullptr;
    QPushButton *btn_close_ = nullptr;
};

} // namespace freight::ui::dialogs
