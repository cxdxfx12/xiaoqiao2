#pragma once
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPropertyAnimation>
#include <QTimer>

namespace freight::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnSingleCalc();
    void OnBatchCalc();
    void OnCompare();
    void OnHistory();
    void OnRuleSetting();
    void OnCustomerSetting();
    void OnSystemSetting();
    void OnAbout();
    void OnAdTimer();

private:
    void SetupUI();
    void SetupStyles();
    void SetupAdBanner();

    QWidget *central_widget_ = nullptr;
    QVBoxLayout *main_layout_ = nullptr;

    QFrame *ad_banner_ = nullptr;
    QLabel *ad_label_ = nullptr;
    QTimer *ad_timer_ = nullptr;
    int ad_index_ = 0;
    QStringList ad_texts_;

    QFrame *card_area_ = nullptr;
    QGridLayout *card_layout_ = nullptr;

    QPushButton *btn_single_calc_ = nullptr;
    QPushButton *btn_batch_calc_ = nullptr;
    QPushButton *btn_compare_ = nullptr;
    QPushButton *btn_history_ = nullptr;

    QFrame *setting_area_ = nullptr;
    QHBoxLayout *setting_layout_ = nullptr;
    QPushButton *btn_rule_setting_ = nullptr;
    QPushButton *btn_customer_setting_ = nullptr;
    QPushButton *btn_system_setting_ = nullptr;
    QPushButton *btn_about_ = nullptr;

    QLabel *footer_label_ = nullptr;
};

} // namespace freight::ui
