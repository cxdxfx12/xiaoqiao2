#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>

namespace freight::ui::dialogs {

class RuleSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit RuleSettingDialog(QWidget *parent = nullptr);
    ~RuleSettingDialog() override;

private slots:
    void OnFuelItemClicked(int row, int col);
    void OnRemoteItemClicked(int row, int col);

private:
    void SetupUI();
    void LoadData();
    void ShowSurchargeDialog(bool is_add);
    void ShowFuelDialog(bool is_add);
    void ShowRemoteDialog(bool is_add);

    QTabWidget *tab_widget_ = nullptr;
    QTableWidget *tpl_table_ = nullptr;
    QTableWidget *surcharge_table_ = nullptr;
    QTableWidget *fuel_table_ = nullptr;
    QTableWidget *remote_table_ = nullptr;
    QPushButton *btn_close_ = nullptr;
    QPushButton *btn_apply_ = nullptr;
};

} // namespace freight::ui::dialogs
