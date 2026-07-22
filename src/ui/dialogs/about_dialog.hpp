#pragma once
#include <QDialog>
#include <QLabel>

namespace freight::ui::dialogs {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override;

private:
    void SetupUI();
    QLabel *lbl_logo_ = nullptr;
    QLabel *lbl_app_name_ = nullptr;
    QLabel *lbl_version_ = nullptr;
    QLabel *lbl_company_ = nullptr;
    QLabel *lbl_website_ = nullptr;
    QLabel *lbl_phone_ = nullptr;
};

} // namespace freight::ui::dialogs
