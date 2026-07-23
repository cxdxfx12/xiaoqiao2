#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include "core/app_config.hpp"

namespace freight::ui::dialogs {

class CourierTemplateManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit CourierTemplateManagerDialog(QWidget *parent = nullptr);
    ~CourierTemplateManagerDialog() override;

signals:
    void TemplatesChanged();

private slots:
    void OnAddTemplate();
    void OnEditTemplate();
    void OnDeleteTemplate();
    void OnToggleGlobal(bool checked);
    void OnEnableAll();
    void OnDisableAll();

private:
    void SetupUI();
    void ReloadTable();
    core::TemplateFingerprint CurrentSelection() const;

    QCheckBox *chk_global_ = nullptr;
    QLabel *lbl_summary_ = nullptr;
    QTableWidget *table_ = nullptr;
    QPushButton *btn_add_ = nullptr;
    QPushButton *btn_edit_ = nullptr;
    QPushButton *btn_del_ = nullptr;
    QPushButton *btn_enable_all_ = nullptr;
    QPushButton *btn_disable_all_ = nullptr;
    QPushButton *btn_close_ = nullptr;
};

} // namespace freight::ui::dialogs
