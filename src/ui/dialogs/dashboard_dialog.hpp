#pragma once
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QDateEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QProgressBar>
#include <QTableWidget>
#include <QComboBox>

class QChartView;

#include "services/dashboard_service.hpp"

namespace freight::ui::dialogs {

class DashboardDialog : public QDialog {
    Q_OBJECT
public:
    explicit DashboardDialog(QWidget *parent = nullptr);
    ~DashboardDialog() override;

private slots:
    void OnRefresh();
    void OnExportPDF();
    void OnDateRangeChanged();
    void OnTargetChanged();

private:
    void SetupUI();
    void LoadData();
    void UpdateSummaryCards(const services::DashboardSummary &s);
    void BuildTrendChart(const QList<services::DailyTrendPoint> &points);
    void BuildProvinceChart(const QList<services::ProvinceStat> &provs);
    void BuildCustomerChart(const QList<services::CustomerStat> &custs);
    void BuildCourierChart(const QMap<QString, double> &mix);
    void BuildFeeBreakdownChart(const QMap<QString, double> &bd);
    void BuildProfitTable(const QList<services::RouteProfitStat> &routes);

    QFrame* CreateStatCard(const QString &title, const QString &value,
                           const QString &subtitle, const QString &color,
                           const QString &icon_char);

    services::DashboardService *svc_ = nullptr;
    QDateEdit *edt_from_ = nullptr;
    QDateEdit *edt_to_ = nullptr;
    QLineEdit *edt_target_ = nullptr;

    QLabel *lbl_total_rev_ = nullptr;
    QLabel *lbl_total_orders_ = nullptr;
    QLabel *lbl_avg_fee_ = nullptr;
    QLabel *lbl_growth_ = nullptr;
    QProgressBar *target_progress_ = nullptr;
    QLabel *lbl_progress_text_ = nullptr;

    QChartView *trend_view_ = nullptr;
    QChartView *province_view_ = nullptr;
    QChartView *customer_view_ = nullptr;
    QChartView *courier_view_ = nullptr;
    QChartView *fee_view_ = nullptr;
    QTableWidget *profit_table_ = nullptr;

    QPushButton *btn_refresh_ = nullptr;
    QPushButton *btn_pdf_ = nullptr;
    QPushButton *btn_close_ = nullptr;
};

} // namespace freight::ui::dialogs
