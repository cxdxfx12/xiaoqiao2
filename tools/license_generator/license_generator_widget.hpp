#pragma once
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QSpinBox>

namespace freight::tools {

class LicenseGeneratorWidget : public QWidget {
    Q_OBJECT

public:
    explicit LicenseGeneratorWidget(QWidget *parent = nullptr);
    ~LicenseGeneratorWidget() override;

private slots:
    void OnGenerate();
    void OnCopy();
    void OnValidate();
    void OnTypeChanged(int index);

private:
    void SetupUI();

    QLineEdit *edit_machine_ = nullptr;
    QComboBox *combo_type_ = nullptr;
    QSpinBox *spin_days_ = nullptr;
    QDateTimeEdit *edit_expire_ = nullptr;
    QLineEdit *edit_secret_ = nullptr;
    QPushButton *btn_generate_ = nullptr;
    QPushButton *btn_copy_ = nullptr;
    QTextEdit *text_result_ = nullptr;
    QLabel *lbl_validate_ = nullptr;
};

} // namespace freight::tools
