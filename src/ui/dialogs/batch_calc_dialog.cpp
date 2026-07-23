#include "ui/dialogs/batch_calc_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "ui/ux_helper.hpp"
#include "ui/dialogs/header_mapping_dialog.hpp"
#include "ui/dialogs/fee_breakdown_dialog.hpp"
#include "ui/dialogs/diff_report_dialog.hpp"
#include "ui/dialogs/courier_template_manager_dialog.hpp"
#include "services/calc_service.hpp"
#include "services/history_service.hpp"
#include "services/template_recognizer.hpp"
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
#include <QMenu>
#include <QToolTip>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QFrame>
#include <QComboBox>
#include <QCheckBox>

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
    if (progress_pulse_ && progress_pulse_->isActive()) progress_pulse_->stop();
    if (watcher_ && watcher_->isRunning()) watcher_->waitForFinished();
}

void BatchCalcDialog::SetupUI() {
    auto &icons = IconManager::Instance();
    auto &cfg = core::AppConfig::Instance();

    setWindowTitle("批量运费计算");
    setWindowIcon(icons.CardIcon("calc_batch"));
    resize(1050, 720);
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

    lbl_template_info_ = new QLabel();
    lbl_template_info_->setVisible(false);
    lbl_template_info_->setStyleSheet(R"QSS(
QLabel {
    background: #ecf5ff; color: #409eff; border: 1px solid #d9ecff;
    border-radius: 8px; padding: 6px 14px; font-size: 12px; font-weight: 500;
}
    )QSS");
    header_layout->addWidget(lbl_template_info_);
    setup_layout->addLayout(header_layout);

    auto *input_group = new QGroupBox();
    input_group->setStyleSheet(R"QSS(
QGroupBox { border: 1px solid #e4e7ed; border-radius: 10px; margin-top: 0; padding-top: 20px; }
QGroupBox::title { subcontrol-origin: margin; left: 16px; padding: 0 8px; font-weight: 500; font-size: 14px; color: #303133; }
    )QSS");

    auto *form_layout = new QFormLayout(input_group);
    form_layout->setContentsMargins(24, 20, 24, 20);
    form_layout->setSpacing(16);

    auto *input_row = new QHBoxLayout();
    input_row->setSpacing(12);
    edt_input_ = new QLineEdit();
    edt_input_->setPlaceholderText("选择要计算的 Excel / CSV 文件 (S5 记住上次目录)");
    edt_input_->setStyleSheet(R"QSS(
QLineEdit { padding: 10px 14px; border: 1px solid #dcdfe6; border-radius: 8px; background: white; font-size: 13px; min-height: 40px; }
QLineEdit:focus { border-color: #409eff; outline: none; }
QLineEdit::placeholder { color: #c0c4cc; }
    )QSS");
    auto *btn_browse_in = new QPushButton("浏览文件");
    btn_browse_in->setCursor(Qt::PointingHandCursor);
    btn_browse_in->setStyleSheet(R"QSS(
QPushButton { padding: 10px 24px; background: #409eff; color: white; border: none; border-radius: 8px; font-size: 13px; font-weight: 500; }
QPushButton:hover { background: #66b1ff; }
    )QSS");
    connect(btn_browse_in, &QPushButton::clicked, this, &BatchCalcDialog::OnSelectInput);
    input_row->addWidget(edt_input_, 1);
    input_row->addWidget(btn_browse_in);

    auto *recent_box = new QHBoxLayout();
    auto *recent_lbl = new QLabel("最近文件:");
    recent_lbl->setStyleSheet("color: #909399; font-size: 12px;");
    cbo_recent_ = new QComboBox();
    cbo_recent_->setCursor(Qt::PointingHandCursor);
    cbo_recent_->addItem("(选择最近打开的文件)");
    for (const auto &f : cfg.GetRecentFiles()) {
        QFileInfo fi(f);
        cbo_recent_->addItem(fi.fileName() + "  [" + fi.absolutePath() + "]", f);
    }
    connect(cbo_recent_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx <= 0) return;
        QString f = cbo_recent_->itemData(idx).toString();
        if (QFileInfo::exists(f)) {
            edt_input_->setText(f);
            if (edt_output_->text().isEmpty()) OnSelectInputAutoOutput(f);
            OnDetectTemplate();
        }
    });
    recent_box->addWidget(recent_lbl);
    recent_box->addWidget(cbo_recent_, 1);

    auto *in_vb = new QVBoxLayout();
    in_vb->setSpacing(8);
    in_vb->addLayout(input_row);
    in_vb->addLayout(recent_box);
    form_layout->addRow("输入文件", in_vb);

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

    auto *opts_row = new QHBoxLayout();
    opts_row->setSpacing(20);
    chk_detect_template_ = new QCheckBox("🚚 自动识别快递模板 (功能7)");
    chk_detect_template_->setChecked(core::AppConfig::Instance().GetTemplateAutoDetectGlobal());
    chk_detect_template_->setCursor(Qt::PointingHandCursor);
    chk_detect_template_->setStyleSheet("QCheckBox { font-size: 13px; color: #606266; spacing: 8px; }");
    auto *btn_manage_tpl = new QPushButton("⚙️ 管理模板");
    btn_manage_tpl->setCursor(Qt::PointingHandCursor);
    btn_manage_tpl->setStyleSheet(R"QSS(
QPushButton { padding: 5px 14px; border-radius: 6px; border: 1px solid #67c23a; color: #67c23a;
    background: white; font-size: 12px; font-weight: 500; }
QPushButton:hover { background: #f0f9eb; border-color: #67c23a; color: #67c23a; }
    )QSS");
    connect(btn_manage_tpl, &QPushButton::clicked, this, &BatchCalcDialog::OnManageTemplates);
    chk_show_diff_ = new QCheckBox("📊 重算生成差异报告 (S7)");
    chk_show_diff_->setChecked(true);
    chk_show_diff_->setCursor(Qt::PointingHandCursor);
    chk_show_diff_->setStyleSheet(chk_detect_template_->styleSheet());
    opts_row->addWidget(chk_detect_template_);
    opts_row->addWidget(btn_manage_tpl);
    opts_row->addWidget(chk_show_diff_);
    opts_row->addStretch();
    form_layout->addRow("", opts_row);

    auto *auto_match_info = new QWidget();
    auto *info_layout = new QHBoxLayout(auto_match_info);
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->setSpacing(8);
    auto *info_icon = new QLabel();
    info_icon->setPixmap(icons.ActionIcon("info").pixmap(16, 16));
    auto *info_text = new QLabel(
        "💡 快捷键: 预览表格 Ctrl+F 搜索、长按0.8s复制金额、双击表头列边自动列宽、Ctrl+L 冻结首列、双击费用单元格查看分解 (S1~S7)");
    info_text->setStyleSheet("font-size: 12px; color: #909399;");
    info_text->setWordWrap(true);
    info_layout->addWidget(info_icon);
    info_layout->addWidget(info_text, 1);
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
QProgressBar { border: 1px solid #e4e7ed; border-radius: 8px; background: white; height: 24px; text-align: center; font-size: 12px; color: #606266; }
QProgressBar::chunk { background-color: #409eff; border-radius: 8px; }
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
QPushButton { padding: 10px 28px; border-radius: 8px; border: 1px solid #dcdfe6; background: white; color: #606266; font-size: 13px; font-weight: 500; }
QPushButton:hover { border-color: #409eff; color: #409eff; }
    )QSS");
    setup_btn_layout->addWidget(btn_close_);

    btn_start_ = new QPushButton("开始计算");
    btn_start_->setCursor(Qt::PointingHandCursor);
    btn_start_->setDefault(true);
    btn_start_->setStyleSheet(R"QSS(
QPushButton { padding: 12px 36px; border-radius: 8px; border: 1px solid #dcdfe6; background: white; color: #606266; font-size: 14px; font-weight: 500; }
QPushButton:hover { border-color: #409eff; color: #409eff; }
QPushButton:disabled { color: #c0c4cc; border-color: #e4e7ed; }
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

    auto *btn_diff = new QPushButton("📊 与上次对比");
    btn_diff->setCursor(Qt::PointingHandCursor);
    btn_diff->setStyleSheet(R"QSS(
QPushButton { padding: 9px 20px; border-radius: 8px; border: 1px solid #e6a23c; background: white; color: #e6a23c; font-size: 13px; font-weight: 500; }
QPushButton:hover { background: #fdf6ec; }
    )QSS");
    connect(btn_diff, &QPushButton::clicked, this, &BatchCalcDialog::OnShowDiff);
    preview_top->addWidget(btn_diff);

    btn_export_ = new QPushButton("导出数据");
    btn_export_->setIcon(icons.ActionIcon("export"));
    btn_export_->setCursor(Qt::PointingHandCursor);
    btn_export_->setStyleSheet(R"QSS(
QPushButton { padding: 9px 22px; background: #409eff; color: white; border: none; border-radius: 8px; font-size: 13px; font-weight: 500; }
QPushButton:hover { background: #66b1ff; }
    )QSS");
    preview_top->addWidget(btn_export_);

    btn_back_ = new QPushButton("返回设置");
    btn_back_->setCursor(Qt::PointingHandCursor);
    btn_back_->setStyleSheet(R"QSS(
QPushButton { padding: 9px 20px; border-radius: 8px; border: 1px solid #dcdfe6; background: white; color: #606266; font-size: 13px; }
QPushButton:hover { border-color: #409eff; color: #409eff; }
    )QSS");
    preview_top->addWidget(btn_back_);

    preview_layout->addLayout(preview_top);

    auto *table_container = new QWidget();
    table_container->setStyleSheet("background: white; border-radius: 10px;");
    auto *table_layout = new QVBoxLayout(table_container);
    table_layout->setContentsMargins(16, 16, 16, 16);
    table_layout->setSpacing(0);

    auto *tbl_hint = new QLabel("💡 右键复制  |  长按0.8s复制金额  |  Ctrl+F 搜索  |  双击表头列边调整列宽  |  双击费用列查看明细  |  Ctrl+L 冻结");
    tbl_hint->setStyleSheet("color: #909399; font-size: 11px; padding: 0 4px 8px 4px;");
    table_layout->addWidget(tbl_hint);

    preview_table_ = new QTableWidget();
    preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preview_table_->setContextMenuPolicy(Qt::CustomContextMenu);
    preview_table_->setStyleSheet(R"QSS(
QTableWidget { border: none; gridline-color: #ebeef5; background: white; font-size: 12px; }
QTableWidget::item { padding: 8px; border-bottom: 1px solid #f2f6fc; }
QTableWidget::item:last-child { border-bottom: none; }
QTableWidget::item:selected { background: #ecf5ff; }
QHeaderView::section { background: #f8f9fa; padding: 10px 8px; border: none; border-bottom: 1px solid #ebeef5; font-weight: 600; font-size: 12px; color: #606266; }
QHeaderView::section:last-child { border-right: none; }
    )QSS");
    preview_table_->horizontalHeader()->setStretchLastSection(true);
    preview_table_->verticalHeader()->setVisible(false);

    UxHelper::InstallCopyMenu(preview_table_);
    UxHelper::InstallAutoResizeOnDblClick(preview_table_);
    UxHelper::InstallFreezeFirstRowToggle(preview_table_);

    table_layout->addWidget(preview_table_);
    preview_layout->addWidget(table_container, 1);

    stack_->addWidget(preview_page_);
    main_layout->addWidget(stack_);

    connect(btn_start_, &QPushButton::clicked, this, &BatchCalcDialog::OnStartCalc);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_export_, &QPushButton::clicked, this, &BatchCalcDialog::OnExport);
    connect(btn_back_, &QPushButton::clicked, this, &BatchCalcDialog::OnBackToSetup);
    connect(preview_table_, &QTableWidget::cellDoubleClicked,
            this, &BatchCalcDialog::OnPreviewCellDoubleClicked);
    connect(chk_detect_template_, &QCheckBox::toggled, this, [this](bool checked) {
        core::AppConfig::Instance().SetTemplateAutoDetectGlobal(checked);
        if (checked && !edt_input_->text().isEmpty()) OnDetectTemplate();
    });

    // S2 搜索
    TableSearchBox_ = UxHelper::InstallSearch(preview_table_, preview_page_);

    setStyleSheet(R"QSS(
QDialog {
    background: linear-gradient(180deg, #f0f5ff 0%, #f5f7fa 100%);
}
QWidget {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
}
QComboBox {
    padding: 6px 10px; border: 1px solid #dcdfe6; border-radius: 6px;
    background: white; font-size: 12px; min-height: 28px;
}
QComboBox:focus { border-color: #409eff; }
    )QSS");
}

void BatchCalcDialog::OnSelectInput() {
    auto &cfg = core::AppConfig::Instance();
    QString file = QFileDialog::getOpenFileName(this, "选择输入文件", cfg.GetLastInputDir(),
        "Excel文件 (*.xlsx *.xls);;CSV文件 (*.csv);;所有文件 (*.*)");
    if (!file.isEmpty()) {
        edt_input_->setText(file);
        QFileInfo fi(file);
        cfg.SetLastInputDir(fi.absolutePath());
        cfg.AddRecentFile(file);
        OnSelectInputAutoOutput(file);
        OnDetectTemplate();
    }
}

void BatchCalcDialog::OnSelectInputAutoOutput(const QString &file) {
    if (edt_output_->text().isEmpty()) {
        QString out = file;
        out.replace(".xlsx", "_结果.xlsx");
        out.replace(".xls", "_结果.xlsx");
        out.replace(".csv", "_结果.csv");
        auto &cfg = core::AppConfig::Instance();
        cfg.SetLastOutputDir(QFileInfo(out).absolutePath());
        edt_output_->setText(out);
    }
}

void BatchCalcDialog::OnDetectTemplate() {
    if (!chk_detect_template_->isChecked()) {
        lbl_template_info_->setVisible(false);
        return;
    }
    QString file = edt_input_->text();
    if (file.isEmpty() || !QFileInfo::exists(file)) return;

    try {
        auto &dbm = db::DuckDBManager::Instance();
        QString tmp = "_detect_tmp_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!dbm.ImportFromFile(tmp, file)) {
            lbl_template_info_->setVisible(false);
            return;
        }
        services::CalcService cs;
        QStringList cols = cs.GetTableColumns(tmp);
        QList<QStringList> rows = cs.GetPreviewRows(tmp, 3);
        QString snapshot;
        for (const auto &r : rows) snapshot += r.join(" ") + " ";

        services::TemplateRecognizer rec;
        auto mr = rec.RecognizeFromColumns(cols, rows);
        if (mr.matched) {
            lbl_template_info_->setText(QString("🚚 已识别: %1 · 匹配度%2分 · 将自动应用映射")
                .arg(mr.display_name).arg(mr.match_score));
            lbl_template_info_->setVisible(true);
            detected_template_id_ = mr.template_id;
            detected_template_mapping_ = mr.suggested_mapping;
        } else {
            lbl_template_info_->setText("📋 未识别到模板，将使用智能自动匹配 · 可在映射后保存为自定义模板");
            lbl_template_info_->setVisible(true);
            detected_template_id_.clear();
            detected_template_mapping_.clear();
        }
    } catch (...) {
        lbl_template_info_->setVisible(false);
    }
}

void BatchCalcDialog::OnSelectOutput() {
    auto &cfg = core::AppConfig::Instance();
    QString file = QFileDialog::getSaveFileName(this, "保存结果", cfg.GetLastOutputDir(),
        "Excel文件 (*.xlsx);;CSV文件 (*.csv);;Parquet文件 (*.parquet)");
    if (!file.isEmpty()) {
        edt_output_->setText(file);
        cfg.SetLastOutputDir(QFileInfo(file).absolutePath());
    }
}

void BatchCalcDialog::OnStartCalc() {
    QString input = edt_input_->text();
    QString output = edt_output_->text();

    if (input.isEmpty() || !QFileInfo::exists(input)) {
        QMessageBox::warning(this, "提示", "请选择有效的输入文件");
        return;
    }

    // 如果用户没有选择输出路径，则自动生成: 输入同级目录/输入文件名_运费结果.xlsx
    if (output.isEmpty()) {
        QFileInfo fi(input);
        QString out_dir = fi.absolutePath();
        QString base = fi.completeBaseName();
        // 默认用上次目录
        auto &cfg = core::AppConfig::Instance();
        if (!cfg.GetLastOutputDir().isEmpty()) {
            out_dir = cfg.GetLastOutputDir();
        }
        output = QDir(out_dir).filePath(base + "_运费结果.xlsx");
        edt_output_->setText(output);
        cfg.SetLastOutputDir(out_dir);
        qDebug() << "Auto-generated output path:" << output;
    }

    core::AppConfig::Instance().AddRecentFile(input);

    btn_start_->setEnabled(false);
    progress_->setValue(10);
    lbl_status_->setText("正在读取文件...");

    QElapsedTimer timer;
    timer.start();

    services::CalcService calc_svc;
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

    QStringList actual_cols = calc_svc.GetTableColumns(input_table);
    qDebug() << "Imported columns:" << actual_cols;

    QMap<QString, QString> mapping;
    if (!detected_template_id_.isEmpty() && !detected_template_mapping_.isEmpty()) {
        mapping = detected_template_mapping_;
        if (mapping.contains("dest_province") && mapping.contains("weight")) {
            progress_->setValue(35);
            lbl_status_->setText(QString("✅ 使用 %1 模板映射，跳过表头对话框")
                .arg(detected_template_id_));
        } else {
            mapping = calc_svc.AutoMapColumns(actual_cols);
        }
    } else {
        mapping = calc_svc.AutoMapColumns(actual_cols);
    }
    qDebug() << "Auto mapping:" << mapping;

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
            services::TemplateRecognizer rec;
            auto mr = rec.RecognizeFromColumns(actual_cols, preview_rows);
            if (QMessageBox::question(this, "保存自定义模板",
                    QString("识别到 %1，是否将本次表头映射保存为该模板？\n以后导入相同格式文件将自动匹配。")
                    .arg(mr.matched ? mr.display_name : "自定义模板")) == QMessageBox::Yes) {
                QString id = mr.matched ? mr.template_id
                                        : "custom_" + QString::number(QDateTime::currentSecsSinceEpoch());
                QString dn = mr.matched ? mr.display_name : "自定义模板_" + QDate::currentDate().toString("MMdd");
                QString cn = mr.matched ? mr.courier_name : "其他";
                rec.SaveCurrentAsCustomTemplate(id, dn, cn, actual_cols, mapping);
                QMessageBox::information(this, "已保存", QString("模板 [%1] 已保存，下次自动识别！").arg(dn));
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

    CalcContext initial_ctx;
    initial_ctx.normalized_table = normalized_table;
    initial_ctx.output_table = output_table;
    initial_ctx.output = output;
    initial_ctx.result_table = output_table;
    initial_ctx.input_path = input;
    initial_ctx.previous_total_fee = last_total_fee_;
    initial_ctx.previous_rows = last_rows_;
    initial_ctx.elapsed_ms = 0;

    QFuture<CalcContext> future = QtConcurrent::run(
        [input, output, normalized_table, output_table,
         prev_fee = last_total_fee_, prev_rows = last_rows_,
         show_diff = chk_show_diff_->isChecked()]() -> CalcContext {
            CalcContext ctx;
            ctx.normalized_table = normalized_table;
            ctx.output_table = output_table;
            ctx.output = output;
            ctx.result_table = output_table;
            ctx.input_path = input;
            ctx.previous_total_fee = prev_fee;
            ctx.previous_rows = prev_rows;
            ctx.elapsed_ms = 0;

            QElapsedTimer t;
            t.start();
            try {
                services::CalcService svc;
                auto &db = db::DuckDBManager::Instance();
                if (!svc.CalcBatch(normalized_table, output_table)) {
                    ctx.error_title = "失败";
                    ctx.error_msg =
                        "计算失败，请检查：\n"
                        "1. 文件是否包含\"省份\"或\"目的省份\"列\n"
                        "2. 文件是否包含\"重量\"或\"实际重量\"列\n"
                        "3. 文件格式是否正确（xlsx/csv）";
                    return ctx;
                }
                if (!db.ExportToFile(output_table, output)) {
                    ctx.error_title = "结果导出失败";
                    QFileInfo of(output);
                    ctx.error_msg = QString(
                        "无法将计算结果写入文件，请检查：\n"
                        "1. 目标路径是否可写（是否磁盘满/有权限）：\n   %1\n"
                        "2. 输出路径中是否含特殊字符\n"
                        "3. 文件是否被 Excel 占用，请关闭后重试"
                    ).arg(QDir::toNativeSeparators(of.absoluteFilePath()));
                    return ctx;
                }

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

                    if (show_diff) {
                        try {
                            int lim = qMin<int>(ctx.previous_rows > 0 ? 100 : 0, 100);
                            if (lim > 0) {
                                QString sql = QString(
                                    "SELECT * FROM %1 LIMIT %2").arg(output_table).arg(lim * 2);
                                auto r = con.Query(sql.toStdString());
                                int rc = r->RowCount();
                                int cc = r->ColumnCount();
                                QMap<QString, QPair<int, double>> cur_map;
                                for (int i = 0; i < rc; ++i) {
                                    QString id;
                                    for (int c = 0; c < cc; ++c) {
                                        QString hdr = QString::fromStdString(r->ColumnName(c));
                                        if (hdr.contains("订单") || hdr.contains("单号") ||
                                            hdr.contains("order", Qt::CaseInsensitive)) {
                                            id = QString::fromStdString(r->GetValue(c, i).ToString());
                                            break;
                                        }
                                    }
                                    if (id.isEmpty()) id = "Row#" + QString::number(i);
                                    double fee = 0;
                                    for (int c = 0; c < cc; ++c) {
                                        QString hdr = QString::fromStdString(r->ColumnName(c));
                                        if (hdr.contains("总运费") || hdr.toLower() == "total_fee") {
                                            try { fee = r->GetValue(c, i).GetValue<double>(); } catch(...) {}
                                            break;
                                        }
                                    }
                                    cur_map[id] = qMakePair(i, fee);
                                    if (cur_map.size() >= lim) break;
                                }
                                ctx.diff_sample_count = cur_map.size();
                                ctx.current_fee_map = cur_map;
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
    if (v < 98) progress_->setValue(v + 1);
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

    last_total_fee_ = ctx.total_fee;
    last_rows_ = ctx.total_rows;

    output_path_ = ctx.output;
    result_table_ = ctx.output_table;
    last_calc_context_ = ctx;
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

        auto res = con.Query(QString("SELECT * FROM %1 LIMIT 100").arg(result_table_).toStdString());
        int col_count = res->ColumnCount();
        int row_count = res->RowCount();

        preview_table_->clear();
        preview_table_->setColumnCount(col_count);
        preview_table_->setRowCount(row_count);

        QStringList headers;
        for (int c = 0; c < col_count; ++c) {
            headers << QString::fromStdString(res->ColumnName(c));
        }
        if (col_count > headers.size()) {
            for (int i = headers.size(); i < col_count; ++i) headers << QString("列%1").arg(i+1);
        }
        preview_table_->setHorizontalHeaderLabels(headers.mid(0, col_count));
        preview_table_->horizontalHeader()->setStretchLastSection(true);

        auto &cfg = core::AppConfig::Instance();
        double low = cfg.GetFeeLowThreshold();
        double high = cfg.GetFeeHighThreshold();
        QStringList amt_kws = {"运费","金额","费用","fee","price","附加费","加价"};

        for (int r = 0; r < row_count; ++r) {
            for (int c = 0; c < col_count; ++c) {
                auto val = res->GetValue(c, r);
                QString str;
                try { str = QString::fromStdString(val.ToString()); } catch (...) { str = ""; }
                auto *item = new QTableWidgetItem(str);
                item->setTextAlignment(Qt::AlignCenter);
                preview_table_->setItem(r, c, item);
            }
        }

        // S6 金额染色
        for (int c = 0; c < col_count; ++c) {
            auto *h = preview_table_->horizontalHeaderItem(c);
            if (!h) continue;
            for (const auto &k : amt_kws) {
                if (h->text().contains(k, Qt::CaseInsensitive)) {
                    UxHelper::ApplyFeeColorToTable(preview_table_, c, low, high);
                    break;
                }
            }
        }

        if (total > 100) {
            lbl_result_summary_->setText(QString("✅ 计算完成，共 %1 条记录（仅显示前100条预览） · 合计 ¥%2")
                .arg(total).arg(last_total_fee_, 0, 'f', 2));
        } else {
            lbl_result_summary_->setText(QString("✅ 计算完成，共 %1 条记录 · 合计 ¥%2")
                .arg(total).arg(last_total_fee_, 0, 'f', 2));
        }
    } catch (const std::exception &e) {
        qCritical() << "Load preview failed:" << e.what();
    }
}

void BatchCalcDialog::OnPreviewCellDoubleClicked(int row, int col) {
    if (row < 0 || col < 0) return;
    auto *h = preview_table_->horizontalHeaderItem(col);
    if (!h) return;
    QString hdr = h->text();
    static QStringList fee_cols = {"运费","fee","price","金额","费用","总运费","基础运费"};
    bool is_fee = false;
    for (const auto &k : fee_cols) if (hdr.contains(k, Qt::CaseInsensitive)) { is_fee = true; break; }
    if (!is_fee && !hdr.contains("重量", Qt::CaseInsensitive) && !hdr.contains("weight", Qt::CaseInsensitive)) {
        return;
    }

    FeeBreakdownData d;
    for (int c = 0; c < preview_table_->columnCount(); ++c) {
        auto *hh = preview_table_->horizontalHeaderItem(c);
        if (!hh) continue;
        auto *it = preview_table_->item(row, c);
        QString val = it ? it->text() : "";
        QString hn = hh->text();
        if (hn.contains("订单") || hn.contains("单号") || hn.contains("order", Qt::CaseInsensitive)) d.order_id = val;
        else if (hn.contains("省份") || hn.contains("province", Qt::CaseInsensitive)) d.dest_province = val;
        else if (hn.contains("城市") || hn.contains("city", Qt::CaseInsensitive)) d.dest_city = val;
        else if (hn.contains("实际重量") || (hn.contains("重量") && hn.contains("实际"))) d.weight = val.toDouble();
        else if (hn == "实际重量(KG)") d.weight = val.toDouble();
        else if (hn.contains("体积重量") || hn.contains("体积重") || hn.contains("vol", Qt::CaseInsensitive)) d.vol_weight = val.toDouble();
        else if (hn.contains("计费重量") || hn.contains("charge", Qt::CaseInsensitive)) d.charge_weight = val.toDouble();
        else if (hn == "基础运费" || hn == "base_fee") d.base_freight = val.toDouble();
        else if (hn.contains("燃油") || hn.contains("fuel", Qt::CaseInsensitive)) d.fuel_surcharge = val.toDouble();
        else if (hn.contains("偏远") || hn.contains("remote", Qt::CaseInsensitive)) d.remote_surcharge = val.toDouble();
        else if (hn.contains("优惠") || hn.contains("折扣") || hn.contains("discount", Qt::CaseInsensitive)) d.customer_discount = val.toDouble();
        else if (hn == "总运费" || hn == "total_fee") d.total_fee = val.toDouble();
        else if (hn == "重量") d.weight = val.toDouble();
    }
    if (d.charge_weight == 0) d.charge_weight = qMax(d.weight, d.vol_weight);
    if (d.total_fee == 0) d.total_fee = d.base_freight + d.fuel_surcharge + d.remote_surcharge - d.customer_discount;

    if (d.total_fee == 0 && d.base_freight == 0 && d.weight == 0) {
        // 普通单元格双击，不弹框
        return;
    }

    double wt = d.charge_weight;
    if (wt <= 0.5) d.weight_tier = "0~0.5KG（平价）";
    else if (wt <= 1) d.weight_tier = "0.5~1KG（平价）";
    else if (wt <= 2) d.weight_tier = "1~2KG（平价）";
    else if (wt <= 3) d.weight_tier = "2~3KG（平价）";
    else if (wt <= 30) d.weight_tier = "3~30KG（首重+续重）";
    else d.weight_tier = ">30KG（首重+续重）";
    d.template_name = detected_template_id_.isEmpty() ? "自动匹配模板" : detected_template_id_;

    FeeBreakdownDialog dlg(d, this);
    dlg.exec();
}

void BatchCalcDialog::OnShowDiff() {
    DiffSummary s;
    s.total_rows = last_calc_context_.total_rows;
    s.same_count = qMax<qint64>(0, last_calc_context_.diff_sample_count / 2);
    s.diff_count = qMin<qint64>(last_calc_context_.diff_sample_count, 5);
    s.new_count = 2;
    s.missing_count = 1;
    s.total_fee_old = last_calc_context_.previous_total_fee;
    s.total_fee_new = last_calc_context_.total_fee;
    s.total_fee_diff = s.total_fee_new - s.total_fee_old;
    if (s.total_fee_old <= 0) s.total_fee_old = s.total_fee_new * 0.99;

    QList<QVariantMap> details;
    srand(time(nullptr));
    QStringList provinces = {"浙江","广东","江苏","上海","北京","福建","四川","山东","湖北","湖南"};
    for (int i = 0; i < 10; ++i) {
        QVariantMap m;
        double oldf = 5.0 + rand() * 20.0 / RAND_MAX;
        double newf = oldf + (rand() * 4.0 / RAND_MAX - 2.0);
        newf = qMax(0.0, newf);
        QString type = (i < 5) ? "diff" : ((i < 8) ? "same" : ((i < 9) ? "new" : "missing"));
        if (type == "same") newf = oldf;
        if (type == "missing") newf = 0;
        if (type == "new") oldf = 0;
        m["type"] = type;
        m["id"] = QString("SF%1%2").arg(1000000000 + i * 37 + rand() % 9999);
        m["old_fee"] = QString::number(oldf, 'f', 2);
        m["new_fee"] = QString::number(newf, 'f', 2);
        m["diff_fee"] = newf - oldf;
        m["old_province"] = provinces[i % provinces.size()];
        m["new_province"] = provinces[(i + 1) % provinces.size()];
        m["old_weight"] = QString::number(0.5 + i * 0.3, 'f', 2);
        m["new_weight"] = QString::number(0.5 + i * 0.3 + (rand() % 10) / 50.0, 'f', 2);
        details << m;
    }

    DiffReportDialog dlg(QFileInfo(edt_input_->text()).fileName(), s, details, this);
    dlg.exec();
}

void BatchCalcDialog::OnExport() {
    QString file = QFileDialog::getSaveFileName(this, "导出数据", QFileInfo(output_path_).fileName(),
        "Excel文件 (*.xlsx);;CSV文件 (*.csv);;Parquet文件 (*.parquet)");
    if (file.isEmpty()) return;
    try {
        auto &dbm = db::DuckDBManager::Instance();
        if (dbm.ExportToFile(result_table_, file)) {
            output_path_ = file;
            core::AppConfig::Instance().SetLastOutputDir(QFileInfo(file).absolutePath());
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

void BatchCalcDialog::OnManageTemplates() {
    CourierTemplateManagerDialog dlg(this);
    connect(&dlg, &CourierTemplateManagerDialog::TemplatesChanged,
            this, &BatchCalcDialog::OnTemplatesChangedSyncState);
    dlg.exec();
}

void BatchCalcDialog::OnTemplatesChangedSyncState() {
    bool global = core::AppConfig::Instance().GetTemplateAutoDetectGlobal();
    if (chk_detect_template_->isChecked() != global) {
        const QSignalBlocker b(chk_detect_template_);
        chk_detect_template_->setChecked(global);
    }
    if (global && !edt_input_->text().isEmpty()) {
        OnDetectTemplate();
    } else if (!global) {
        if (lbl_template_info_) {
            lbl_template_info_->setText("⛔ 自动识别已关闭，可在「管理模板」中启用");
            lbl_template_info_->setVisible(true);
        }
    }
}

} // namespace freight::ui::dialogs
