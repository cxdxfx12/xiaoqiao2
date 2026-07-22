#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>

namespace freight::ui::dialogs {

class CompareDialog : public QDialog {
    Q_OBJECT

public:
    explicit CompareDialog(QWidget *parent = nullptr);
    ~CompareDialog() override;

private slots:
    void OnCompare();

private:
    void SetupUI();

    QComboBox *cbo_province_ = nullptr;
    QDoubleSpinBox *spn_weight_ = nullptr;
    QDoubleSpinBox *spn_vol_weight_ = nullptr;
    QPushButton *btn_compare_ = nullptr;
    QPushButton *btn_close_ = nullptr;
    QTableWidget *table_ = nullptr;
};

} // namespace freight::ui::dialogs
