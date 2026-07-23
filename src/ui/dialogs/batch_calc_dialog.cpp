#include "ui/dialogs/batch_calc_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "ui/dialogs/header_mapping_dialog.hpp"
#include "services/calc_service.hpp"
#include "services/history_service.hpp"
#include "db/duckdb_manager.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QHeaderView>
#include <QFileInfo>
#include <QDebug>
#include <QLabel>
#include <QElapsedTimer>
#include <QtConcurrent>

namespace freight::ui::dialogs {

BatchCalcDialog::BatchCalcDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
    watcher_ = new QFutureWatcher<CalcContext>(this);
    connect(watcher_, &QFutureWatcher<CalcContext>::finished, this, &BatchCalcDialog::OnCalcFinished);
    progress_pulse_ = new QTimer(this);
    progress_pulse_->setInterval(500);
    connect(progress_pulse_, &QTimer::timeout, this, &BatchCalcDialog::OnProgressPulse);
}

BatchCalcDialog::~BatchCalcDialog() {
    if (progress_pulse_ && progress_pulse_->isActive()) {
        progress_pulse_->stop();
    }
    if (watcher_ && watcher_->isRunning()) {
        watcher_->waitForFinished();
    }
}

void BatchCalcDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("批量运费计算");
    setWindowIcon(icons.CardIcon("calc_batch"));
    resize(950, 650);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(16);

    stack_ = new QStackedWidget();
    stack_->setStyleSheet("QStackedWidget { background: transparent; }");

    // ========== 第1页：设置页 ==========
    setup_page_ = new QWidget();
    auto *setup_layout = new QVBoxLayout(setup_page_);
    setup_layout->setContentsMargins(0, 0, 0, 0);
    setup_layout->setSpacing(16);

    auto *header_layout = new QHBoxLayout();
    auto *icon_label = new QLabel();
    icon_label->setPixmap(icons.ActionIcon("calculate").pixmap(32, 32));
    auto *title_label = new QLabel("批量运费计算");
    title_label->setStyleSheet("font-size: 18px; font-weight: 600; color: #303133;");
    header_layout->addWidget(icon_label);
    header_layout->addWidget(title_label);
    header_layout->addStretch();
    setup_layout->addLayout(header_layout);

    auto *input_group = new QGroupBox();
    input_group->setStyleSheet(R"QSS(
QGroupBox {
    border: 1px solid #e4e7ed;
    border-radius: 10px;
    margin-top: 0;
    padding-top: 20px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 16px;
    padding: 0 8px;
    font-weight: 500;
    font-size: 14px;
    color: #303133;
}
    )QSS");

    auto *form_layout = new QFormLayout(input_group);
    form_layout->setContentsMargins(24, 20, 24, 20);
    form_layout->setSpacing(16);

    auto *input_row = new QHBoxLayout();
    input_row->setSpacing(12);
    edt_input_ = new QLineEdit();
    edt_input_->setPlaceholderText("选择要计算的 Excel / CSV 文件");
    edt_input_->setStyleSheet(R"QSS(
QLineEdit {
    padding: 10px 14px;
    border: 1px solid #dcdfe6;
    border-radius: 8px;
    background: white;
    font-size: 13px;
    min-height: 40px;
}
QLineEdit:focus {
    border-color: #409eff;
    outline: none;
}
QLineEdit::placeholder {
    color: #c0c4cc;
}
    )QSS");
    auto *btn_browse_in = new QPushButton("浏览文件");
    btn_browse_in->setCursor(Qt::PointingHandCursor);
    btn_browse_in->setStyleSheet(R"QSS(
QPushButton {
    padding: 10px 24px;
    background: #409eff;
    color: white;
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover {
    background: #66b1ff;
}
    )QSS");
    connect(btn_browse_in, &QPushButton::clicked, this, &BatchCalcDialog::OnSelectInput);
    input_row->addWidget(edt_input_, 1);
    input_row->addWidget(btn_browse_in);
    form_layout->addRow("输入文件", input_row);

    auto *output_row = new QHBoxLayout();
    output_row->setSpacing(12);
    edt_output_ = new QLineEdit();
    edt_output_->setPlaceholderText("选择结果保存路径");
    edt_output_->setStyleSheet(edt_input_->styleSheet());
    auto *btn_browse_out = new QPushButton("浏览路径");
    btn_browse_out->setCursor(Qt::PointingHandCursor);
    btn_browse_out->setStyleSheet(btn_browse_in->styleSheet());
    connect(btn_browse_out, &QPushButton::clicked, this, &BatchCalcDialog::OnSelectOutput);
    output_row->addWidget(edt_output_, 1);
    output_row->addWidget(btn_browse_out);
    form_layout->addRow("输出文件", output_row);

    auto *auto_match_info = new QWidget();
    auto *info_layout = new QHBoxLayout(auto_match_info);
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->setSpacing(8);
    auto *info_icon = new QLabel();
    info_icon->setPixmap(icons.ActionIcon("info").pixmap(16, 16));
    auto *info_text = new QLabel("系统将根据客户ID自动匹配对应的运费模板，客户规则优先级高于全局规则");
    info_text->setStyleSheet("font-size: 12px; color: #909399;");
    info_text->setWordWrap(true);
    info_layout->addWidget(info_icon);
    info_layout->addWidget(info_text);
    form_layout->addRow("", info_layout);

    setup_layout->addWidget(input_group);

    auto *progress_group = new QGroupBox("计算进度");
    progress_group->setStyleSheet(input_group->styleSheet());
    auto *progress_layout = new QVBoxLayout(progress_group);
    progress_layout->setContentsMargins(24, 20, 24, 20);
    progress_layout->setSpacing(12);

    progress_ = new QProgressBar();
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setStyleSheet(R"QSS(
QProgressBar {
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    background: white;
    height: 24px;
    text-align: center;
    font-size: 12px;
    color: #606266;
}
QProgressBar::chunk {
    background-color: #409eff;
    border-radius: 8px;
}
    )QSS");
    progress_layout->addWidget(progress_);

    lbl_status_ = new QLabel("等待开始...");
    lbl_status_->setStyleSheet("color: #909399; font-size: 13px;");
    progress_layout->addWidget(lbl_status_);

    setup_layout->addWidget(progress_group);

    setup_layout->addStretch();

    auto *setup_btn_layout = new QHBoxLayout();
    setup_btn_layout->setSpacing(12);
    setup_btn_layout->addStretch();

    btn_close_ = new QPushButton("关闭");
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(R"QSS(
QPushButton {
    padding: 10px 28px;
    border-radius: 8px;
    border: 1px solid #dcdfe6;
    background: white;
    color: #606266;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover {
    border-color: #409eff;
    color: #409eff;
}
    )QSS");
    setup_btn_layout->addWidget(btn_close_);

    btn_start_ = new QPushButton("开始计算");
    btn_start_->setCursor(Qt::PointingHandCursor);
    btn_start_->setDefault(true);
    btn_start_->setStyleSheet(R"QSS(
QPushButton {
    padding: 12px 36px;
    border-radius: 8px;
    border: 1px solid #dcdfe6;
    background: white;
    color: #606266;
    font-size: 14px;
    font-weight: 500;
}
QPushButton:hover {
    border-color: #409eff;
    color: #409eff;
}
QPushButton:disabled {
    color: #c0c4cc;
    border-color: #e4e7ed;
}
    )QSS");
    setup_btn_layout->addWidget(btn_start_);

    setup_layout->addLayout(setup_btn_layout);

    stack_->addWidget(setup_page_);

    // ========== 第2页：预览页 ==========
    preview_page_ = new QWidget();
    auto *preview_layout = new QVBoxLayout(preview_page_);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(16);

    auto *preview_top = new QHBoxLayout();
    preview_top->setSpacing(12);

    lbl_result_summary_ = new QLabel("计算完成");
    lbl_result_summary_->setStyleSheet("font-weight: 600; font-size: 15px; color: #303133;");
    preview_top->addWidget(lbl_result_summary_);
    preview_top->addStretch();

    btn_export_ = new QPushButton("导出数据");
    btn_export_->setIcon(icons.ActionIcon("export"));
    btn_export_->setCursor(Qt::PointingHandCursor);
    btn_export_->setStyleSheet(R"QSS(
QPushButton {
    padding: 9px 22px;
    background: #409eff;
    color: white;
    border: none;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover {
    background: #66b1ff;
}
    )QSS");
    preview_top->addWidget(btn_export_);

    btn_back_ = new QPushButton("返回设置");
    btn_back_->setCursor(Qt::PointingHandCursor);
    btn_back_->setStyleSheet(R"QSS(
QPushButton {
    padding: 9px 20px;
    border-radius: 8px;
    border: 1px solid #dcdfe6;
    background: white;
    color: #606266;
    font-size: 13px;
}
QPushButton:hover {
    border-color: #409eff;
    color: #409eff;
}
    )QSS");
    preview_top->addWidget(btn_back_);

    preview_layout->addLayout(preview_top);

    auto *table_container = new QWidget();
    table_container->setStyleSheet("background: white; border-radius: 10px;");
    auto *table_layout = new QVBoxLayout(table_container);
    table_layout->setContentsMargins(16, 16, 16, 16);
    table_layout->setSpacing(0);

    preview_table_ = new QTableWidget();
    preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preview_table_->setStyleSheet(R"QSS(
QTableWidget {
    border: none;
    gridline-color: #ebeef5;
    background: white;
    font-size: 12px;
}
QTableWidget::item {
    padding: 8px;
    border-bottom: 1px solid #f2f6fc;
}
QTableWidget::item:last-child {
    border-bottom: none;
}
QTableWidget::item:selected {
    background: #ecf5ff;
}
QHeaderView::section {
    background: #f8f9fa;
    padding: 10px 8px;
    border: none;
    border-bottom: 1px solid #ebeef5;
    font-weight: 600;
    font-size: 12px;
    color: #606266;
}
QHeaderView::section:last-child {
    border-right: none;
}
    )QSS");
    preview_table_->horizontalHeader()->setStretchLastSection(true);
    preview_table_->verticalHeader()->setVisible(false);
    table_layout->addWidget(preview_table_);
    preview_layout->addWidget(table_container, 1);

    stack_->addWidget(preview_page_);

    main_layout->addWidget(stack_);

    connect(btn_start_, &QPushButton::clicked, this, &BatchCalcDialog::OnStartCalc);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_export_, &QPushButton::clicked, this, &BatchCalcDialog::OnExport);
    connect(btn_back_, &QPushButton::clicked, this, &BatchCalcDialog::OnBackToSetup);

    setStyleSheet(R"QSS(
QDialog {
    background: linear-gradient(180deg, #f0f5ff 0%, #f5f7fa 100%);
}
QWidget {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}
    )QSS");
}

void BatchCalcDialog::OnSelectInput() {
    QString file = QFileDialog::getOpenFileName(this, "选择输入文件", "",
        "Excel文件 (*.xlsx *.xls);;CSV文件 (*.csv);;所有文件 (*.*)");
    if (!file.isEmpty()) {
        edt_input_->setText(file);
        if (edt_output_->text().isEmpty()) {
            QString out = file;
            out.replace(".xlsx", "_结果.xlsx");
            out.replace(".csv", "_结果.csv");
            edt_output_->setText(out);
        }
    }
}

void BatchCalcDialog::OnSelectOutput() {
    QString file = QFileDialog::getSaveFileName(this, "保存结果", "",
        "Excel文件 (*.xlsx);;CSV文件 (*.csv);;Parquet文件 (*.parquet)");
    if (!file.isEmpty()) {
        edt_output_->setText(file);
    }
}

void BatchCalcDialog::OnStartCalc() {
    QString input = edt_input_->text();
    QString output = edt_output_->text();

    if (input.isEmpty() || output.isEmpty()) {
        QMessageBox::warning(this, "提示", "请选择输入和输出文件");
        return;
    }

    btn_start_->setEnabled(false);
    progress_->setValue(10);
    lbl_status_->setText("正在读取文件...");

    QElapsedTimer timer;
    timer.start();

    services::CalcService calc_svc;

    // 1. 导入文件到 DuckDB（需要 SQL 创建表，放主线程）
    auto &dbm = db::DuckDBManager::Instance();
    QString input_table = "_input_tmp";
    QString output_table = "_output_tmp";

    if (!dbm.ImportFromFile(input_table, input)) {
        progress_->setValue(0);
        lbl_status_->setText("文件导入失败");
        QMessageBox::critical(this, "失败", "文件导入失败，请检查文件格式");
        btn_start_->setEnabled(true);
        return;
    }

    progress_->setValue(30);
    lbl_status_->setText("正在分析表头...");

    // 2. 获取实际列名并自动映射
    QStringList actual_cols = calc_svc.GetTableColumns(input_table);
    qDebug() << "Imported columns:" << actual_cols;
    QMap<QString, QString> mapping = calc_svc.AutoMapColumns(actual_cols);
    qDebug() << "Auto mapping:" << mapping;

    // 3. 检查必填列，必要时弹表头映射对话框（UI 交互必须主线程）
    bool need_mapping = !mapping.contains("dest_province") || !mapping.contains("weight");

    if (need_mapping) {
        QStringList preview_headers = calc_svc.GetPreviewHeaders(input_table);
        QList<QStringList> preview_rows = calc_svc.GetPreviewRows(input_table, 5);

        HeaderMappingDialog dlg(actual_cols, mapping, preview_headers, preview_rows, this);
        if (dlg.exec() == QDialog::Accepted) {
            mapping = dlg.GetMapping();
            if (dlg.IsRememberRequested()) {
                services::CalcService::RememberMapping(mapping, actual_cols);
            }
        } else {
            progress_->setValue(0);
            lbl_status_->setText("已取消");
            btn_start_->setEnabled(true);
            return;
        }
    }

    progress_->setValue(50);
    lbl_status_->setText("正在归一化数据...");

    // 4. 创建归一化表（快速查询，可以放主线程）
    QString normalized_table = calc_svc.CreateNormalizedTable(input_table, mapping);
    if (normalized_table.isEmpty()) {
        progress_->setValue(0);
        lbl_status_->setText("归一化失败");
        QMessageBox::critical(this, "失败", "表头映射失败");
        btn_start_->setEnabled(true);
        return;
    }

    progress_->setValue(55);
    lbl_status_->setText("正在计算运费（后台线程执行，UI 不会卡顿）...");
    progress_pulse_->start();

    // 5-7. 计算 + 导出 + 保存历史：放到后台线程异步执行，避免大文件卡死 UI
    CalcContext initial_ctx;
    initial_ctx.normalized_table = normalized_table;
    initial_ctx.output_table = output_table;
    initial_ctx.output = output;
    initial_ctx.result_table = output_table;
    initial_ctx.elapsed_ms = 0;  // 后台重新计时

    QFuture<CalcContext> future = QtConcurrent::run(
        [input, normalized_table, output_table, output]() -> CalcContext {
            CalcContext ctx;
            ctx.normalized_table = normalized_table;
            ctx.output_table = output_table;
            ctx.output = output;
            ctx.result_table = output_table;

            QElapsedTimer t;
            t.start();

            try {
                services::CalcService svc;
                auto &db = db::DuckDBManager::Instance();

                // 5. 执行计算
                if (!svc.CalcBatch(normalized_table, output_table)) {
                    ctx.error_title = "失败";
                    ctx.error_msg =
                        "计算失败，请检查：\n"
                        "1. 文件是否包含\"省份\"或\"目的省份\"列\n"
                        "2. 文件是否包含\"重量\"或\"实际重量\"列\n"
                        "3. 文件格式是否正确（xlsx/csv）";
                    return ctx;
                }

                // 6. 导出结果
                if (!db.ExportToFile(output_table, output)) {
                    ctx.error_title = "失败";
                    ctx.error_msg = "结果导出失败";
                    return ctx;
                }

                // 7. 保存历史（后台线程，HistoryService 用独立 SQLite 连接）
                try {
                    auto &cfg = core::AppConfig::Instance();
                    services::HistoryService history_svc(cfg.GetHistoryDbPath());
                    history_svc.Init();

                    auto con = db.CreateConnection();
                    auto cnt_res = con.Query(
                        QString("SELECT COUNT(*) FROM %1").arg(output_table).toStdString());
                    ctx.total_rows = cnt_res->GetValue(0, 0).GetValue<int64_t>();

                    try {
                        auto fee_res = con.Query(
                            QString("SELECT SUM(总运费) FROM %1").arg(output_table).toStdString());
                        if (fee_res->RowCount() > 0) {
                            ctx.total_fee = fee_res->GetValue(0, 0).GetValue<double>();
                        }
                    } catch (...) {
                        try {
                            auto fee_res = con.Query(
                                QString("SELECT SUM(total_fee) FROM %1").arg(output_table).toStdString());
                            if (fee_res->RowCount() > 0) {
                                ctx.total_fee = fee_res->GetValue(0, 0).GetValue<double>();
                            }
                        } catch (...) {}
                    }

                    QFileInfo fi(input);
                    QVariantMap record;
                    record["task_name"] = fi.completeBaseName();
                    record["input_file"] = input;
                    record["output_file"] = output;
                    record["total_rows"] = static_cast<int>(ctx.total_rows);
                    record["total_fee"] = ctx.total_fee;
                    record["duration_ms"] = static_cast<int>(t.elapsed());
                    record["status"] = 1;
                    history_svc.AddHistory(record);
                } catch (const std::exception &e) {
                    qWarning() << "Save history failed (background):" << e.what();
                }

                ctx.success = true;
            } catch (const std::exception &e) {
                ctx.error_title = "失败";
                ctx.error_msg = QString("计算异常：%1").arg(e.what());
            } catch (...) {
                ctx.error_title = "失败";
                ctx.error_msg = "计算发生未知异常";
            }

            ctx.elapsed_ms = t.elapsed();
            return ctx;
        });

    watcher_->setFuture(future);
}

void BatchCalcDialog::OnProgressPulse() {
    int v = progress_->value();
    if (v < 98) {
        progress_->setValue(v + 1);
    }
}

void BatchCalcDialog::OnCalcFinished() {
    progress_pulse_->stop();
    CalcContext ctx = watcher_->result();

    if (!ctx.success) {
        progress_->setValue(0);
        lbl_status_->setText("计算失败");
        QString title = ctx.error_title.isEmpty() ? QStringLiteral("失败") : ctx.error_title;
        QString msg = ctx.error_msg.isEmpty() ? QStringLiteral("未知错误") : ctx.error_msg;
        QMessageBox::critical(this, title, msg);
        btn_start_->setEnabled(true);
        return;
    }

    progress_->setValue(100);
    if (ctx.elapsed_ms > 0) {
        lbl_status_->setText(QString("计算完成！耗时 %1 s，共 %2 条，合计 %3 元")
            .arg(ctx.elapsed_ms / 1000.0, 0, 'f', 1)
            .arg(ctx.total_rows)
            .arg(ctx.total_fee, 0, 'f', 2));
    } else {
        lbl_status_->setText("计算完成！");
    }

    output_path_ = ctx.output;
    result_table_ = ctx.output_table;
    LoadPreviewData();
    stack_->setCurrentIndex(1);
    btn_start_->setEnabled(true);
}

void BatchCalcDialog::LoadPreviewData() {
    try {
        auto &dbm = db::DuckDBManager::Instance();
        auto con = dbm.CreateConnection();

        auto cnt_res = con.Query(QString("SELECT COUNT(*) FROM %1").arg(result_table_).toStdString());
        int64_t total = cnt_res->GetValue(0, 0).GetValue<int64_t>();

        auto res = con.Query(
            QString("SELECT * FROM %1 LIMIT 100").arg(result_table_).toStdString());

        int col_count = res->ColumnCount();
        int row_count = res->RowCount();

        preview_table_->clear();
        preview_table_->setColumnCount(col_count);
        preview_table_->setRowCount(row_count);

        QStringList headers = {
            "订单号", "客户编号", "目的省份", "目的城市",
            "实际重量(KG)", "体积重量(KG)", "计费重量(KG)",
            "基础运费", "燃油附加费", "地区加价", "其他附加费",
            "总运费", "币种"
        };
        if (col_count > headers.size()) {
            for (int i = headers.size(); i < col_count; i++) {
                headers << QString("列%1").arg(i + 1);
            }
        }
        preview_table_->setHorizontalHeaderLabels(headers.mid(0, col_count));
        preview_table_->horizontalHeader()->setStretchLastSection(true);

        for (int r = 0; r < row_count; r++) {
            for (int c = 0; c < col_count; c++) {
                auto val = res->GetValue(c, r);
                QString str;
                try {
                    str = QString::fromStdString(val.ToString());
                } catch (...) {
                    str = "";
                }
                auto *item = new QTableWidgetItem(str);
                item->setTextAlignment(Qt::AlignCenter);
                preview_table_->setItem(r, c, item);
            }
        }

        if (total > 100) {
            lbl_result_summary_->setText(QString("计算完成，共 %1 条记录（仅显示前100条预览）").arg(total));
        } else {
            lbl_result_summary_->setText(QString("计算完成，共 %1 条记录").arg(total));
        }
    } catch (const std::exception &e) {
        qCritical() << "Load preview failed:" << e.what();
    }
}

void BatchCalcDialog::OnExport() {
    QString file = QFileDialog::getSaveFileName(this, "导出数据", QFileInfo(output_path_).fileName(),
        "Excel文件 (*.xlsx);;CSV文件 (*.csv);;Parquet文件 (*.parquet)");
    if (file.isEmpty()) return;

    try {
        auto &dbm = db::DuckDBManager::Instance();
        if (dbm.ExportToFile(result_table_, file)) {
            output_path_ = file;
            QMessageBox::information(this, "成功", "数据已导出到：" + file);
        } else {
            QMessageBox::warning(this, "失败", "导出失败");
        }
    } catch (const std::exception &e) {
        QMessageBox::critical(this, "错误", e.what());
    }
}

void BatchCalcDialog::OnBackToSetup() {
    stack_->setCurrentIndex(0);
    btn_start_->setEnabled(true);
    progress_->setValue(0);
    lbl_status_->setText("等待开始...");
}

} // namespace freight::ui::dialogs
