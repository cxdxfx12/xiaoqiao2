#pragma once
#include <QDialog>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QProgressBar>
#include <QMap>

namespace freight::ui::dialogs {

struct FeeBreakdownData {
    QString order_id;
    QString dest_province;
    QString dest_city;
    double weight = 0.0;
    double vol_weight = 0.0;
    double charge_weight = 0.0;
    QString weight_tier;
    double base_freight = 0.0;
    double fuel_surcharge = 0.0;
    double remote_surcharge = 0.0;
    double customer_discount = 0.0;
    double other_fee = 0.0;
    double total_fee = 0.0;
    QString template_name;
    QString template_id;
    QString customer_id;
    bool ok = false;
    QString error_msg;
};

class FeeBreakdownDialog : public QDialog {
    Q_OBJECT
public:
    explicit FeeBreakdownDialog(const FeeBreakdownData &data, QWidget *parent = nullptr);

private:
    void SetupUI();
    QFrame* CreateFeeItem(const QString &label, double value, bool positive = true, bool highlight = false);

    FeeBreakdownData data_;

    QVBoxLayout *main_layout_ = nullptr;
    QLabel *lbl_title_ = nullptr;
    QLabel *lbl_subtitle_ = nullptr;
    QFrame *card_info_ = nullptr;
    QFrame *card_breakdown_ = nullptr;
    QFrame *card_total_ = nullptr;
    QProgressBar *weight_bar_ = nullptr;
    QPushButton *btn_close_ = nullptr;
};

} // namespace freight::ui::dialogs
