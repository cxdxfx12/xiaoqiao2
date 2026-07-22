#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>

namespace freight::ui::dialogs {

class SystemSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit SystemSettingDialog(QWidget *parent = nullptr);
    ~SystemSettingDialog() override;

private slots:
    void OnAutoPerformanceToggled(bool checked);
    void OnAccepted();

private:
    void SetupUI();
    void LoadSettings();
    void SaveSettings();

    QTabWidget *tab_widget_ = nullptr;
    QCheckBox *chk_auto_perf_ = nullptr;
    QSpinBox *spn_mem_ = nullptr;
    QSpinBox *spn_thread_ = nullptr;
    QLabel *lbl_sys_info_ = nullptr;
};

} // namespace freight::ui::dialogs
