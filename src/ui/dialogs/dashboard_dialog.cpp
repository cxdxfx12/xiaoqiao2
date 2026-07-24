#include "ui/dialogs/dashboard_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDateEdit>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QCalendarWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPainter>
#include <QPageLayout>
#include <QPixmap>
#include <QApplication>
#include <QScreen>
#include <QScrollArea>
#include <QGraphicsDropShadowEffect>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegend>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QHBarModelMapper>

namespace freight::ui::dialogs {

static QColor palette_color(int i) {
    static QList<QColor> palette = {
        QColor("#409eff"), QColor("#67c23a"), QColor("#e6a23c"), QColor("#f56c6c"),
        QColor("#909399"), QColor("#9b59b6"), QColor("#16a085"), QColor("#e74c3c"),
        QColor("#3498db"), QColor("#2ecc71")
    };
    return palette[i % palette.size()];
}

DashboardDialog::DashboardDialog(QWidget *parent) : QDialog(parent) {
    auto &cfg = core::AppConfig::Instance();
    svc_ = new services::DashboardService(cfg.GetHistoryDbPath(), this);
    svc_->Init();
    SetupUI();
    LoadData();
}

DashboardDialog::~DashboardDialog() = default;

QFrame* DashboardDialog::CreateStatCard(const QString &title, const QString &value,
                                         const QString &subtitle, const QString &color,
                                         const QString &icon_char) {
    auto *frame = new QFrame();
    frame->setStyleSheet(QString(R"QSS(
QFrame {
    background: white;
    border-radius: 12px;
    border: 1px solid #ebeef5;
}
    )QSS"));
    auto *shadow = new QGraphicsDropShadowEffect(frame);
    shadow->setBlurRadius(12);
    shadow->setColor(QColor(0, 0, 0, 20));
    shadow->setOffset(0, 4);
    frame->setGraphicsEffect(shadow);

    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(14);

    auto *icon = new QLabel(icon_char);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet(QString(
        "font-size: 28px; min-width: 56px; max-width: 56px; min-height: 56px; max-height: 56px; "
        "background: %1; color: white; border-radius: 14px; font-weight: 600;"
    ).arg(color));
    layout->addWidget(icon);

    auto *vb = new QVBoxLayout();
    vb->setSpacing(2);
    auto *t = new QLabel(title);
    t->setStyleSheet("color: #909399; font-size: 12px;");
    auto *v = new QLabel(value);
    v->setStyleSheet(QString("color: %1; font-size: 22px; font-weight: 700;").arg(color));
    auto *s = new QLabel(subtitle);
    s->setStyleSheet("color: #c0c4cc; font-size: 11px;");
    vb->addWidget(t);
    vb->addWidget(v);
    vb->addWidget(s);
    layout->addLayout(vb, 1);
    return frame;
}

void DashboardDialog::SetupUI() {
    auto &icons = IconManager::Instance();
    auto &cfg = core::AppConfig::Instance();

    setWindowTitle("📊 运营驾驶舱");
    setWindowIcon(icons.GetIcon("dashboard", IconCategory::ACTION, IconSize::SIZE_32));
    resize(1280, 820);
    setModal(false);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto *content = new QWidget();
    auto *main_layout = new QVBoxLayout(content);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(16);
    scroll->setWidget(content);

    // ==== 顶部控制栏 ====
    auto *top_bar = new QFrame();
    top_bar->setStyleSheet(R"QSS(
QFrame { background: white; border-radius: 12px; border: 1px solid #ebeef5; }
    )QSS");
    auto *tb_layout = new QHBoxLayout(top_bar);
    tb_layout->setContentsMargins(18, 12, 18, 12);
    tb_layout->setSpacing(14);

    auto *title_lbl = new QLabel("📊 运营驾驶舱 · 老板一眼看懂");
    title_lbl->setStyleSheet("font-size: 18px; font-weight: 600; color: #303133;");
    tb_layout->addWidget(title_lbl);
    tb_layout->addStretch();

    auto *range_lbl = new QLabel("统计区间:");
    range_lbl->setStyleSheet("font-size: 13px; color: #606266;");
    tb_layout->addWidget(range_lbl);

    edt_from_ = new QDateEdit();
    edt_from_->setCalendarPopup(true);
    edt_from_->setDisplayFormat("yyyy-MM-dd");
    edt_from_->setDate(QDate::currentDate().addDays(-30));
    edt_from_->setStyleSheet(R"QSS(
QDateEdit { padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px; font-size: 13px; min-width: 120px; }
QDateEdit:focus { border-color: #409eff; }
    )QSS");
    tb_layout->addWidget(edt_from_);

    auto *to_lbl = new QLabel("~");
    to_lbl->setStyleSheet("color: #909399;");
    tb_layout->addWidget(to_lbl);

    edt_to_ = new QDateEdit();
    edt_to_->setCalendarPopup(true);
    edt_to_->setDisplayFormat("yyyy-MM-dd");
    edt_to_->setDate(QDate::currentDate());
    edt_to_->setStyleSheet(edt_from_->styleSheet());
    tb_layout->addWidget(edt_to_);

    auto *target_lbl = new QLabel("月度目标:");
    target_lbl->setStyleSheet("font-size: 13px; color: #606266;");
    tb_layout->addSpacing(8);
    tb_layout->addWidget(target_lbl);

    edt_target_ = new QLineEdit();
    edt_target_->setValidator(new QDoubleValidator(0, 99999999, 2, edt_target_));
    edt_target_->setText(QString::number(svc_->GetMonthlyTarget(), 'f', 0));
    edt_target_->setPlaceholderText("¥");
    edt_target_->setStyleSheet(R"QSS(
QLineEdit { padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px; font-size: 13px; min-width: 110px; }
QLineEdit:focus { border-color: #409eff; }
    )QSS");
    tb_layout->addWidget(edt_target_);

    btn_refresh_ = new QPushButton("🔄 刷新");
    btn_refresh_->setCursor(Qt::PointingHandCursor);
    btn_refresh_->setStyleSheet(R"QSS(
QPushButton { padding: 7px 18px; background: #f5f7fa; border: 1px solid #dcdfe6; border-radius: 7px; font-size: 13px; color: #606266; }
QPushButton:hover { border-color: #409eff; color: #409eff; background: #ecf5ff; }
    )QSS");
    tb_layout->addWidget(btn_refresh_);

    btn_pdf_ = new QPushButton("📄 导出PDF");
    btn_pdf_->setCursor(Qt::PointingHandCursor);
    btn_pdf_->setStyleSheet(R"QSS(
QPushButton { padding: 7px 18px; background: #67c23a; color: white; border: none; border-radius: 7px; font-size: 13px; font-weight: 500; }
QPushButton:hover { background: #85ce61; }
    )QSS");
    tb_layout->addWidget(btn_pdf_);

    btn_close_ = new QPushButton("关闭");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(R"QSS(
QPushButton { padding: 7px 20px; background: white; border: 1px solid #dcdfe6; border-radius: 7px; font-size: 13px; color: #606266; }
QPushButton:hover { border-color: #f56c6c; color: #f56c6c; }
    )QSS");
    tb_layout->addWidget(btn_close_);

    main_layout->addWidget(top_bar);

    // ==== 目标进度 ====
    auto *prog_card = new QFrame();
    prog_card->setStyleSheet(top_bar->styleSheet());
    auto *prog_layout = new QVBoxLayout(prog_card);
    prog_layout->setContentsMargins(22, 16, 22, 16);
    prog_layout->setSpacing(8);

    auto *prog_header = new QHBoxLayout();
    auto *prog_lbl = new QLabel("🎯 本月收入目标进度");
    prog_lbl->setStyleSheet("font-size: 14px; font-weight: 600; color: #303133;");
    lbl_progress_text_ = new QLabel();
    lbl_progress_text_->setStyleSheet("font-size: 14px; font-weight: 600; color: #409eff;");
    prog_header->addWidget(prog_lbl);
    prog_header->addStretch();
    prog_header->addWidget(lbl_progress_text_);
    prog_layout->addLayout(prog_header);

    target_progress_ = new QProgressBar();
    target_progress_->setRange(0, 100);
    target_progress_->setValue(0);
    target_progress_->setFixedHeight(20);
    target_progress_->setStyleSheet(R"QSS(
QProgressBar {
    border: 1px solid #ebeef5;
    border-radius: 10px;
    background: #f5f7fa;
    text-align: center;
    font-size: 11px;
    color: #303133;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #667eea, stop:1 #764ba2);
    border-radius: 10px;
}
    )QSS");
    prog_layout->addWidget(target_progress_);

    main_layout->addWidget(prog_card);

    // ==== 4 张统计卡 ====
    auto *card_grid = new QGridLayout();
    card_grid->setSpacing(14);

    lbl_total_rev_ = new QLabel("¥ 0");
    lbl_total_orders_ = new QLabel("0 单");
    lbl_avg_fee_ = new QLabel("¥ 0");
    lbl_growth_ = new QLabel("0.0%");

    card_grid->addWidget(CreateStatCard("总营收", "¥ 0", "本区间累计", "#409eff", "¥"), 0, 0);
    card_grid->addWidget(CreateStatCard("总单量", "0 单", "历史累计订单", "#67c23a", "#"), 0, 1);
    card_grid->addWidget(CreateStatCard("平均运费", "¥ 0", "单均运费", "#e6a23c", "μ"), 0, 2);
    card_grid->addWidget(CreateStatCard("环比增长", "0.0%", "vs 上一周期", "#f56c6c", "↑"), 0, 3);

    auto *wrap_cards = new QWidget();
    wrap_cards->setLayout(card_grid);
    main_layout->addWidget(wrap_cards);

    lbl_total_rev_ = qobject_cast<QLabel*>(card_grid->itemAtPosition(0,0)->widget()->findChildren<QLabel*>().at(1));
    lbl_total_orders_ = qobject_cast<QLabel*>(card_grid->itemAtPosition(0,1)->widget()->findChildren<QLabel*>().at(1));
    lbl_avg_fee_ = qobject_cast<QLabel*>(card_grid->itemAtPosition(0,2)->widget()->findChildren<QLabel*>().at(1));
    lbl_growth_ = qobject_cast<QLabel*>(card_grid->itemAtPosition(0,3)->widget()->findChildren<QLabel*>().at(1));

    // ==== 趋势图 (收入走势) ====
    auto *trend_box = new QGroupBox("📈 每日收入走势");
    trend_box->setStyleSheet(R"QSS(
QGroupBox { border: 1px solid #ebeef5; border-radius: 12px; background: white; margin-top: 0; padding-top: 20px; font-weight: 600; color: #303133; font-size: 14px; }
QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; }
    )QSS");
    auto *tl = new QVBoxLayout(trend_box);
    tl->setContentsMargins(16, 20, 16, 16);
    trend_view_ = new QChartView();
    trend_view_->setRenderHint(QPainter::Antialiasing);
    trend_view_->setMinimumHeight(280);
    trend_view_->setStyleSheet("background: transparent; border: none;");
    tl->addWidget(trend_view_);
    main_layout->addWidget(trend_box);

    // ==== 两列：省份饼图 + 客户柱图 ====
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(14);

    auto *prov_box = new QGroupBox("🗺️ 省份收入占比");
    prov_box->setStyleSheet(trend_box->styleSheet());
    auto *pl = new QVBoxLayout(prov_box);
    pl->setContentsMargins(16, 20, 16, 16);
    province_view_ = new QChartView();
    province_view_->setRenderHint(QPainter::Antialiasing);
    province_view_->setMinimumHeight(280);
    province_view_->setStyleSheet("background: transparent;");
    pl->addWidget(province_view_);
    row2->addWidget(prov_box, 1);

    auto *cust_box = new QGroupBox("👥 TOP 5 客户营收");
    cust_box->setStyleSheet(trend_box->styleSheet());
    auto *cl = new QVBoxLayout(cust_box);
    cl->setContentsMargins(16, 20, 16, 16);
    customer_view_ = new QChartView();
    customer_view_->setRenderHint(QPainter::Antialiasing);
    customer_view_->setMinimumHeight(280);
    customer_view_->setStyleSheet("background: transparent;");
    cl->addWidget(customer_view_);
    row2->addWidget(cust_box, 1);

    auto *row2_wrap = new QWidget();
    row2_wrap->setLayout(row2);
    main_layout->addWidget(row2_wrap);

    // ==== 两列：快递公司占比饼图 + 费用结构 ====
    auto *row3 = new QHBoxLayout();
    row3->setSpacing(14);

    auto *courier_box = new QGroupBox("🚚 快递公司营收占比");
    courier_box->setStyleSheet(trend_box->styleSheet());
    auto *courier_l = new QVBoxLayout(courier_box);
    courier_l->setContentsMargins(16, 20, 16, 16);
    courier_view_ = new QChartView();
    courier_view_->setRenderHint(QPainter::Antialiasing);
    courier_view_->setMinimumHeight(260);
    courier_view_->setStyleSheet("background: transparent;");
    courier_l->addWidget(courier_view_);
    row3->addWidget(courier_box, 1);

    auto *fee_box = new QGroupBox("💰 费用构成分解");
    fee_box->setStyleSheet(trend_box->styleSheet());
    auto *fl = new QVBoxLayout(fee_box);
    fl->setContentsMargins(16, 20, 16, 16);
    fee_view_ = new QChartView();
    fee_view_->setRenderHint(QPainter::Antialiasing);
    fee_view_->setMinimumHeight(260);
    fee_view_->setStyleSheet("background: transparent;");
    fl->addWidget(fee_view_);
    row3->addWidget(fee_box, 1);

    auto *row3_wrap = new QWidget();
    row3_wrap->setLayout(row3);
    main_layout->addWidget(row3_wrap);

    // ==== Top5 利润率线路表 ====
    auto *profit_box = new QGroupBox("💎 TOP 5 利润率线路");
    profit_box->setStyleSheet(trend_box->styleSheet());
    auto *pf_l = new QVBoxLayout(profit_box);
    pf_l->setContentsMargins(16, 20, 16, 16);
    profit_table_ = new QTableWidget();
    profit_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    profit_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    profit_table_->verticalHeader()->setVisible(false);
    profit_table_->horizontalHeader()->setStretchLastSection(true);
    profit_table_->setStyleSheet(R"QSS(
QTableWidget { border: none; gridline-color: #ebeef5; background: white; font-size: 12px; }
QTableWidget::item { padding: 10px; border-bottom: 1px solid #f2f6fc; }
QTableWidget::item:selected { background: #ecf5ff; }
QHeaderView::section { background: #f8f9fa; padding: 10px 8px; border: none; border-bottom: 1px solid #ebeef5; font-weight: 600; color: #606266; font-size: 12px; }
    )QSS");
    profit_table_->setColumnCount(6);
    profit_table_->setHorizontalHeaderLabels({"线路名称","营收(¥)","预估成本(¥)","预估利润(¥)","利润率(%)","订单数"});
    profit_table_->setRowCount(0);
    pf_l->addWidget(profit_table_);
    main_layout->addWidget(profit_box);

    connect(btn_refresh_, &QPushButton::clicked, this, &DashboardDialog::OnRefresh);
    connect(btn_pdf_, &QPushButton::clicked, this, &DashboardDialog::OnExportPDF);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(edt_from_, &QDateEdit::dateChanged, this, &DashboardDialog::OnDateRangeChanged);
    connect(edt_to_, &QDateEdit::dateChanged, this, &DashboardDialog::OnDateRangeChanged);
    connect(edt_target_, &QLineEdit::editingFinished, this, &DashboardDialog::OnTargetChanged);

    setStyleSheet(R"QSS(
QDialog {
    background: #f5f7fa;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}
QScrollArea { background: #f5f7fa; }
    )QSS");

    QScreen *screen = QApplication::primaryScreen();
    QRect geom = screen->availableGeometry();
    move((geom.width() - width()) / 2, (geom.height() - height()) / 2);
}

void DashboardDialog::OnDateRangeChanged() { LoadData(); }
void DashboardDialog::OnTargetChanged() {
    bool ok = false;
    double v = edt_target_->text().toDouble(&ok);
    if (ok && v > 0) {
        svc_->SetMonthlyTarget(v);
        LoadData();
    }
}
void DashboardDialog::OnRefresh() { LoadData(); }

void DashboardDialog::UpdateSummaryCards(const services::DashboardSummary &s) {
    lbl_total_rev_->setText(QString("¥ %1").arg(s.total_revenue, 0, 'f', 2));
    auto *p = lbl_total_rev_->parentWidget();
    Q_UNUSED(p);
    lbl_total_orders_->setText(QString("%1 单").arg(s.total_orders));
    lbl_avg_fee_->setText(QString("¥ %1").arg(s.avg_freight_per_order, 0, 'f', 2));
    QString sign = s.growth_rate >= 0 ? "+" : "";
    lbl_growth_->setText(QString("%1%2%").arg(sign).arg(s.growth_rate, 0, 'f', 1));
    QColor gc = s.growth_rate >= 0 ? QColor("#67c23a") : QColor("#f56c6c");
    lbl_growth_->setStyleSheet(QString("color: %1; font-size: 22px; font-weight: 700;").arg(gc.name()));

    target_progress_->setValue(qMin(100, static_cast<int>(s.target_progress)));
    target_progress_->setFormat(QString("%p% (¥%1 / ¥%2)")
        .arg(QString::number(s.total_revenue, 'f', 0), QString::number(s.target, 'f', 0)));
    lbl_progress_text_->setText(QString("已完成 %1%  ·  还差 ¥ %2")
        .arg(s.target_progress, 0, 'f', 1)
        .arg(qMax(0.0, s.target - s.total_revenue), 0, 'f', 0));
}

void DashboardDialog::BuildTrendChart(const QList<services::DailyTrendPoint> &points) {
    auto *chart = new QChart();
    chart->setTitle("");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundRoundness(0);
    chart->setBackgroundBrush(Qt::white);

    auto *rev_series = new QLineSeries();
    rev_series->setName("收入 (¥)");
    rev_series->setColor(QColor("#409eff"));
    rev_series->setPen(QPen(QColor("#409eff"), 3));

    auto *ord_series = new QLineSeries();
    ord_series->setName("订单数");
    ord_series->setColor(QColor("#67c23a"));
    ord_series->setPen(QPen(QColor("#67c23a"), 2, Qt::DashLine));

    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    QStringList cats;
    double max_rev = 1;
    qint64 max_ord = 1;
    int stride = qMax(1, points.size() / 14);
    for (int i = 0; i < points.size(); ++i) {
        const auto &p = points[i];
        rev_series->append(i, p.revenue);
        ord_series->append(i, p.orders);
        if (i % stride == 0 || i == points.size() - 1) {
            cats << p.date.toString("MM-dd");
        } else {
            cats << "";
        }
        if (p.revenue > max_rev) max_rev = p.revenue;
        if (p.orders > max_ord) max_ord = p.orders;
    }

    chart->addSeries(rev_series);
    chart->addSeries(ord_series);

    axisX->append(cats);
    axisX->setGridLineVisible(false);
    chart->addAxis(axisX, Qt::AlignBottom);
    rev_series->attachAxis(axisX);
    ord_series->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setRange(0, max_rev * 1.15);
    axisY->setLabelFormat("%.0f");
    chart->addAxis(axisY, Qt::AlignLeft);
    rev_series->attachAxis(axisY);

    QValueAxis *axisY2 = new QValueAxis();
    axisY2->setRange(0, max_ord * 1.15);
    axisY2->setLabelFormat("%d");
    chart->addAxis(axisY2, Qt::AlignRight);
    ord_series->attachAxis(axisY2);

    chart->setMargins(QMargins(10, 10, 10, 10));
    trend_view_->setChart(chart);
}

void DashboardDialog::BuildProvinceChart(const QList<services::ProvinceStat> &provs) {
    auto *chart = new QChart();
    chart->setTitle("");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(Qt::white);

    auto *pie = new QPieSeries();
    for (int i = 0; i < provs.size(); ++i) {
        const auto &p = provs[i];
        auto *slice = pie->append(QString("%1 ¥%2 (%3%)")
            .arg(p.province).arg(p.revenue, 0, 'f', 0).arg(p.pct, 0, 'f', 1), p.pct);
        slice->setColor(palette_color(i));
        if (i == 0) { slice->setExploded(); slice->setLabelVisible(true); }
    }
    pie->setHoleSize(0.52);
    pie->setPieSize(0.88);
    chart->addSeries(pie);
    chart->setMargins(QMargins(10, 10, 10, 10));
    province_view_->setChart(chart);
}

void DashboardDialog::BuildCustomerChart(const QList<services::CustomerStat> &custs) {
    auto *chart = new QChart();
    chart->setTitle("");
    chart->legend()->setVisible(false);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(Qt::white);

    auto *set = new QBarSet("客户营收 (¥)");
    QStringList cats;
    for (int i = 0; i < custs.size(); ++i) {
        const auto &c = custs[i];
        *set << c.revenue;
        cats << (c.customer_id.length() > 8 ? c.customer_id.left(8) + "…" : c.customer_id);
    }
    set->setBrush(QBrush(QColor("#409eff")));
    set->setPen(Qt::NoPen);

    auto *bars = new QBarSeries();
    bars->append(set);
    bars->setBarWidth(0.55);
    bars->setLabelsFormat("@value");
    bars->setLabelsVisible(true);
    bars->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);
    chart->addSeries(bars);

    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    axisX->append(cats);
    axisX->setGridLineVisible(false);
    chart->addAxis(axisX, Qt::AlignBottom);
    bars->attachAxis(axisX);

    double maxv = 1;
    for (const auto &c : custs) maxv = qMax(maxv, c.revenue);
    QValueAxis *axisY = new QValueAxis();
    axisY->setRange(0, maxv * 1.2);
    axisY->setLabelFormat("%.0f");
    chart->addAxis(axisY, Qt::AlignLeft);
    bars->attachAxis(axisY);

    chart->setMargins(QMargins(10, 10, 10, 10));
    customer_view_->setChart(chart);
}

void DashboardDialog::BuildCourierChart(const QMap<QString, double> &mix) {
    auto *chart = new QChart();
    chart->setTitle("");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(Qt::white);

    auto *pie = new QPieSeries();
    int idx = 0;
    QList<QPair<QString, double>> items;
    for (auto it = mix.begin(); it != mix.end(); ++it) items << qMakePair(it.key(), it.value());
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b){ return a.second > b.second; });
    for (const auto &kv : items) {
        auto *slice = pie->append(QString("%1 %2%").arg(kv.first).arg(kv.second, 0, 'f', 1), kv.second);
        slice->setColor(palette_color(idx++));
    }
    pie->setHoleSize(0.5);
    pie->setPieSize(0.88);
    chart->addSeries(pie);
    chart->setMargins(QMargins(10, 10, 10, 10));
    courier_view_->setChart(chart);
}

void DashboardDialog::BuildFeeBreakdownChart(const QMap<QString, double> &bd) {
    auto *chart = new QChart();
    chart->setTitle("");
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundBrush(Qt::white);

    auto *stacked = new QStackedBarSeries();
    QStringList cats;
    cats << "整体费用结构";

    int idx = 0;
    QList<QPair<QString, double>> items;
    for (auto it = bd.begin(); it != bd.end(); ++it) items << qMakePair(it.key(), it.value());
    for (const auto &kv : items) {
        auto *set = new QBarSet(kv.first);
        *set << kv.second;
        set->setColor(palette_color(idx++));
        stacked->append(set);
    }
    stacked->setLabelsVisible(true);
    stacked->setLabelsFormat("¥%.0f");
    stacked->setLabelsPosition(QAbstractBarSeries::LabelsCenter);
    chart->addSeries(stacked);

    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    axisX->append(cats);
    chart->addAxis(axisX, Qt::AlignBottom);
    stacked->attachAxis(axisX);

    double total = 1;
    for (const auto &kv : items) total += kv.second;
    QValueAxis *axisY = new QValueAxis();
    axisY->setRange(0, total);
    axisY->setLabelFormat("¥%.0f");
    chart->addAxis(axisY, Qt::AlignLeft);
    stacked->attachAxis(axisY);

    chart->setMargins(QMargins(10, 10, 10, 10));
    fee_view_->setChart(chart);
}

void DashboardDialog::BuildProfitTable(const QList<services::RouteProfitStat> &routes) {
    profit_table_->setRowCount(routes.size());
    for (int r = 0; r < routes.size(); ++r) {
        const auto &rt = routes[r];
        auto setCell = [&](int c, const QString &text, const QString &color = "#606266", bool bold = false) {
            auto *it = new QTableWidgetItem(text);
            it->setTextAlignment(Qt::AlignCenter);
            it->setForeground(QBrush(QColor(color)));
            if (bold) { QFont f = it->font(); f.setBold(true); it->setFont(f); }
            profit_table_->setItem(r, c, it);
        };
        setCell(0, rt.route, "#303133", true);
        setCell(1, QString::number(rt.revenue, 'f', 2));
        setCell(2, QString::number(rt.cost_estimate, 'f', 2));
        QString pc = rt.profit >= 0 ? "#67c23a" : "#f56c6c";
        setCell(3, QString::number(rt.profit, 'f', 2), pc, true);
        setCell(4, QString("%1%").arg(rt.profit_rate, 0, 'f', 1), pc, true);
        setCell(5, QString::number(rt.orders));
    }
    profit_table_->resizeColumnsToContents();
}

void DashboardDialog::LoadData() {
    QDate from = edt_from_->date();
    QDate to = edt_to_->date();
    auto s = svc_->GetSummary(from, to);
    UpdateSummaryCards(s);
    BuildTrendChart(svc_->GetDailyTrend(from, to));
    BuildProvinceChart(svc_->GetTopProvinces(from, to, 8));
    BuildCustomerChart(svc_->GetTopCustomers(from, to, 5));
    BuildCourierChart(svc_->GetCourierMix(from, to));
    BuildFeeBreakdownChart(svc_->GetFeeBreakdown(from, to));
    BuildProfitTable(svc_->GetTopProfitRoutes(from, to, 5));
}

void DashboardDialog::OnExportPDF() {
    QString file = QFileDialog::getSaveFileName(this, "导出老板汇报版PDF",
        QString("运营报告_%1.pdf").arg(QDate::currentDate().toString("yyyyMMdd")),
        "PDF文件 (*.pdf)");
    if (file.isEmpty()) return;

    try {
        auto *screen = QApplication::primaryScreen();
        QPixmap screenshot = screen->grabWindow(this->winId());

        QPdfWriter writer(file);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(QPageLayout::Portrait);
        writer.setResolution(150);
        writer.setTitle(QString("运营驾驶舱报告 %1").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        writer.setCreator(core::AppConfig::Instance().GetAppName());

        QPainter painter(&writer);
        QRectF page_rect = painter.viewport();

        auto &cfg = core::AppConfig::Instance();
        int y = 0;
        painter.setPen(QColor("#303133"));
        painter.setFont(QFont("PingFang SC", 20, QFont::Bold));
        painter.drawText(QRectF(0, y, page_rect.width(), 40), Qt::AlignCenter,
            QString("%1 · 运营驾驶舱报告").arg(cfg.GetAppName()));
        y += 56;

        painter.setFont(QFont("PingFang SC", 10));
        painter.setPen(QColor("#909399"));
        painter.drawText(QRectF(0, y, page_rect.width(), 20), Qt::AlignCenter,
            QString("统计区间: %1 至 %2  ·  生成时间: %3")
                .arg(edt_from_->date().toString("yyyy-MM-dd"))
                .arg(edt_to_->date().toString("yyyy-MM-dd"))
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")));
        y += 40;

        painter.setFont(QFont("PingFang SC", 11, QFont::Bold));
        painter.setPen(QColor("#409eff"));
        painter.drawText(QRectF(40, y, page_rect.width() - 80, 24), "【核心指标】");
        y += 30;

        auto s = svc_->GetSummary(edt_from_->date(), edt_to_->date());
        QStringList metrics = {
            QString("总营收: ¥%1").arg(s.total_revenue, 0, 'f', 2),
            QString("总订单: %1 单").arg(s.total_orders),
            QString("平均运费: ¥%1").arg(s.avg_freight_per_order, 0, 'f', 2),
            QString("环比增长: %1%2%").arg(s.growth_rate >= 0 ? "+" : "").arg(s.growth_rate, 0, 'f', 1),
            QString("月度目标进度: %1%").arg(s.target_progress, 0, 'f', 1)
        };
        painter.setFont(QFont("PingFang SC", 10));
        painter.setPen(QColor("#606266"));
        for (const auto &m : metrics) {
            painter.drawText(QRectF(60, y, page_rect.width() - 120, 22), " • " + m);
            y += 22;
        }
        y += 20;

        double ratio = qMin((page_rect.width() - 80) / (double)screenshot.width(),
                            (page_rect.height() - y - 80) / (double)screenshot.height());
        QSizeF sz(screenshot.width() * ratio, screenshot.height() * ratio);
        QRectF img_rect((page_rect.width() - sz.width()) / 2, y, sz.width(), sz.height());
        painter.drawPixmap(img_rect, screenshot, screenshot.rect());

        QMessageBox::information(this, "导出成功", "运营驾驶舱PDF已生成：\n" + file);
    } catch (const std::exception &e) {
        QMessageBox::critical(this, "导出失败", e.what());
    }
}

} // namespace freight::ui::dialogs
