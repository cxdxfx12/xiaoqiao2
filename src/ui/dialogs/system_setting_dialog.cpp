#include "ui/dialogs/system_setting_dialog.hpp"
#include "ui/icon_manager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>

namespace freight::ui::dialogs {

SystemSettingDialog::SystemSettingDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
}

SystemSettingDialog::~SystemSettingDialog() = default;

void SystemSettingDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("系统设置");
    setWindowIcon(icons.SettingIcon("system_setting"));
    resize(550, 450);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    tab_widget_ = new QTabWidget();

    // 通用设置
    auto *gen_tab = new QWidget();
    auto *gen_layout = new QVBoxLayout(gen_tab);
    gen_layout->setSpacing(12);

    auto *chk_startup = new QCheckBox("开机自启动");
    auto *chk_minimize = new QCheckBox("最小化到系统托盘");
    auto *chk_auto_update = new QCheckBox("自动检查更新");
    gen_layout->addWidget(chk_startup);
    gen_layout->addWidget(chk_minimize);
    gen_layout->addWidget(chk_auto_update);
    gen_layout->addStretch();

    tab_widget_->addTab(gen_tab, "通用");

    // 性能设置
    auto *perf_tab = new QWidget();
    auto *perf_layout = new QVBoxLayout(perf_tab);
    perf_layout->setSpacing(12);

    auto *mem_row = new QHBoxLayout();
    mem_row->addWidget(new QLabel("内存限制(MB)："));
    auto *spn_mem = new QSpinBox();
    spn_mem->setRange(256, 32768);
    spn_mem->setValue(4096);
    mem_row->addWidget(spn_mem);
    mem_row->addStretch();
    perf_layout->addLayout(mem_row);

    auto *thread_row = new QHBoxLayout();
    thread_row->addWidget(new QLabel("计算线程数："));
    auto *spn_thread = new QSpinBox();
    spn_thread->setRange(1, 64);
    spn_thread->setValue(4);
    thread_row->addWidget(spn_thread);
    thread_row->addStretch();
    perf_layout->addLayout(thread_row);

    perf_layout->addStretch();
    tab_widget_->addTab(perf_tab, "性能");

    // 数据清理
    auto *data_tab = new QWidget();
    auto *data_layout = new QVBoxLayout(data_tab);
    data_layout->setSpacing(12);

    auto *chk_auto_clean = new QCheckBox("自动清理历史记录");
    data_layout->addWidget(chk_auto_clean);

    auto *day_row = new QHBoxLayout();
    day_row->addWidget(new QLabel("保留天数："));
    auto *spn_days = new QSpinBox();
    spn_days->setRange(7, 365);
    spn_days->setValue(90);
    day_row->addWidget(spn_days);
    day_row->addStretch();
    data_layout->addLayout(day_row);

    auto *btn_clean = new QPushButton("立即清理历史数据");
    data_layout->addWidget(btn_clean);
    data_layout->addStretch();

    tab_widget_->addTab(data_tab, "数据管理");

    main_layout->addWidget(tab_widget_);

    auto *btn_layout = new QHBoxLayout();
    btn_layout->addStretch();
    auto *btn_cancel = new QPushButton(" 取消");
    btn_cancel->setObjectName("normalBtn");
    btn_cancel->setCursor(Qt::PointingHandCursor);
    auto *btn_ok = new QPushButton(" 确定");
    btn_ok->setObjectName("primaryBtn");
    btn_ok->setCursor(Qt::PointingHandCursor);
    btn_layout->addWidget(btn_cancel);
    btn_layout->addWidget(btn_ok);
    main_layout->addLayout(btn_layout);

    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_ok, &QPushButton::clicked, this, &QDialog::accept);

    setStyleSheet(R"QSS(
QDialog { background-color: #f5f7fa; }
QTabWidget::pane {
    border: 1px solid #e4e7ed;
    border-radius: 8px;
    background: white;
}
QTabBar::tab {
    padding: 8px 18px;
    border: 1px solid #e4e7ed;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    background: #f5f7fa;
    margin-right: 4px;
}
QTabBar::tab:selected {
    background: white;
    color: #409eff;
}
QPushButton#primaryBtn {
    background-color: #409eff;
    color: white;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
}
QPushButton#primaryBtn:hover { background-color: #66b1ff; }
QPushButton#normalBtn {
    background-color: #ffffff;
    color: #606266;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
    padding: 8px 20px;
}
QPushButton#normalBtn:hover {
    border-color: #409eff;
    color: #409eff;
}
QCheckBox { spacing: 8px; }
    )QSS");
}

} // namespace freight::ui::dialogs
