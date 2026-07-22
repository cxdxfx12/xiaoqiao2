#pragma once
#include <QDialog>
#include <QListWidget>
#include <QTableWidget>

namespace freight::ui::dialogs {

class CustomerSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit CustomerSettingDialog(QWidget *parent = nullptr);
    ~CustomerSettingDialog() override;

private slots:
    void OnCustomerSelected(QListWidgetItem *current, QListWidgetItem *previous);
    void OnPricingCellChanged(int row, int col);

private:
    void SetupUI();
    void LoadCustomerList();
    void LoadCustomerPricing(const QString &cust_id);
    void OnSaveCustomerPricing();
    void ShowCustomerDialog(bool is_add);
    void OnBatchImport();

    QListWidget *customer_list_ = nullptr;
    QTableWidget *pricing_table_ = nullptr;
    QPushButton *btn_save_pricing_ = nullptr;
    bool loading_data_ = false;
    QString current_cust_id_;
};

} // namespace freight::ui::dialogs
