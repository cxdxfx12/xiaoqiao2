#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include "core/app_config.hpp"

namespace freight::ui::dialogs {

class CourierTemplateEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit CourierTemplateEditDialog(const core::TemplateFingerprint &existing = core::TemplateFingerprint(),
                                       bool is_builtin = false, QWidget *parent = nullptr);
    ~CourierTemplateEditDialog() override;

    core::TemplateFingerprint GetResult() const;

private slots:
    void OnAddMapping();
    void OnRemoveMapping();
    void OnOk();

private:
    void SetupUI();
    void LoadFromFingerprint();

    bool is_new_ = true;
    bool is_builtin_ = false;
    core::TemplateFingerprint orig_;
    core::TemplateFingerprint result_;

    QLineEdit *edt_id_ = nullptr;
    QLineEdit *edt_name_ = nullptr;
    QLineEdit *edt_courier_ = nullptr;
    QTextEdit *edt_keywords_ = nullptr;
    QTableWidget *tbl_mapping_ = nullptr;
    QLabel *lbl_builtin_ = nullptr;
    QPushButton *btn_add_mapping_ = nullptr;
    QPushButton *btn_del_mapping_ = nullptr;
    QPushButton *btn_ok_ = nullptr;
    QPushButton *btn_cancel_ = nullptr;
};

} // namespace freight::ui::dialogs
