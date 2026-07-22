#pragma once
#include <QDialog>
#include <QTabWidget>

namespace freight::ui::dialogs {

class SystemSettingDialog : public QDialog {
    Q_OBJECT

public:
    explicit SystemSettingDialog(QWidget *parent = nullptr);
    ~SystemSettingDialog() override;

private:
    void SetupUI();
    QTabWidget *tab_widget_ = nullptr;
};

} // namespace freight::ui::dialogs
