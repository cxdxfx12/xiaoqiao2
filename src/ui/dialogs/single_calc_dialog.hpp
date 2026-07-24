#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace freight::ui::dialogs {

class SingleCalcDialog : public QDialog {
    Q_OBJECT

public:
    explicit SingleCalcDialog(QWidget *parent = nullptr);
    ~SingleCalcDialog() override;

private slots:
    void OnCalc();
    void OnClear();

private:
    void SetupUI();

    QLineEdit *edt_order_id_ = nullptr;
    QComboBox *cbo_province_ = nullptr;
    QLineEdit *edt_city_ = nullptr;
    QLineEdit *edt_customer_ = nullptr;
    QDoubleSpinBox *spn_weight_ = nullptr;
    QDoubleSpinBox *spn_vol_weight_ = nullptr;
    QComboBox *cbo_template_ = nullptr;

    QLabel *lbl_result_charge_weight_ = nullptr;
    QLabel *lbl_result_base_fee_ = nullptr;
    QLabel *lbl_result_fuel_ = nullptr;
    QLabel *lbl_result_remote_ = nullptr;
    QLabel *lbl_result_strategy_ = nullptr;
    QLabel *lbl_result_total_ = nullptr;
    QLabel *lbl_result_status_ = nullptr;

    QPushButton *btn_calc_ = nullptr;
    QPushButton *btn_clear_ = nullptr;
    QPushButton *btn_close_ = nullptr;
};

} // namespace freight::ui::dialogs
