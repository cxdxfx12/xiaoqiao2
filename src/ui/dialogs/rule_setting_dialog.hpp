#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>

namespace freight::ui::dialogs {

class RuleSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit RuleSettingDialog(QWidget *parent = nullptr);
    ~RuleSettingDialog() override;

    void OpenMappingTab();
    void OpenAvgWeightTab();

private slots:
    void OnFuelItemClicked(int row, int col);
    void OnRemoteItemClicked(int row, int col);
    void OnResetMappingKeywords();
    void OnApplyMappingKeywords();
    void OnEditMappingRow(int row);
    void OnResetMappingRow(int row);
    void OnQuickAddKeyword();
    void OnFuelFilterChanged(int idx);
    void OnRemoteFilterChanged(int idx);
    void OnSurchargeFilterChanged(int idx);
    void OnLajzItemClicked(int row, int col);
    void OnLajzAdd();
    void OnLajzEdit();
    void OnLajzDel();
    void OnLajzToggle(bool active);

private:
    void SetupUI();
    void LoadData();
    void ShowSurchargeDialog(bool is_add);
    void ShowFuelDialog(bool is_add);
    void ShowRemoteDialog(bool is_add);
    void ShowLajzDialog(bool is_add);

    void SetupMappingTab(QWidget *tab);
    void LoadMappingTable();
    void SetupLajzTab(QWidget *tab);
    void LoadLajzTable();

    QTabWidget *tab_widget_ = nullptr;
    QTableWidget *tpl_table_ = nullptr;
    QTableWidget *surcharge_table_ = nullptr;
    QTableWidget *fuel_table_ = nullptr;
    QTableWidget *remote_table_ = nullptr;
    QTableWidget *mapping_table_ = nullptr;
    QTableWidget *lajz_table_ = nullptr;
    QPushButton *btn_close_ = nullptr;
    QPushButton *btn_apply_ = nullptr;

    QComboBox *cb_fuel_filter_ = nullptr;
    QComboBox *cb_remote_filter_ = nullptr;
    QComboBox *cb_surcharge_filter_ = nullptr;

    QPushButton *btn_mapping_reset_ = nullptr;
    QPushButton *btn_mapping_apply_ = nullptr;
    QPushButton *btn_mapping_quick_add_ = nullptr;
    QComboBox *cb_mapping_quick_std_ = nullptr;
    QLineEdit *ed_mapping_quick_kw_ = nullptr;

    int mapping_tab_idx_ = -1;
    int lajz_tab_idx_ = -1;
};

} // namespace freight::ui::dialogs
