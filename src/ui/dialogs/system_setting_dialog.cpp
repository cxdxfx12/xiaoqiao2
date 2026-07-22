#include "ui/dialogs/system_setting_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QGroupBox>

namespace freight::ui::dialogs {

SystemSettingDialog::SystemSettingDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
    LoadSettings();
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

    chk_auto_perf_ = new QCheckBox("自动优化性能（使用系统90%资源）");
    chk_auto_perf_->setChecked(true);
    perf_layout->addWidget(chk_auto_perf_);

    auto *sys_group = new QGroupBox("系统信息");
    auto *sys_layout = new QVBoxLayout(sys_group);
    lbl_sys_info_ = new QLabel();
    lbl_sys_info_->setStyleSheet("color: #606266; font-size: 13px; line-height: 1.6;");
    sys_layout->addWidget(lbl_sys_info_);
    perf_layout->addWidget(sys_group);

    auto *manual_group = new QGroupBox("手动设置");
    auto *manual_layout = new QVBoxLayout(manual_group);
    manual_layout->setSpacing(12);

    auto *mem_row = new QHBoxLayout();
    mem_row->addWidget(new QLabel("内存限制(MB)："));
    spn_mem_ = new QSpinBox();
    spn_mem_->setRange(256, 262144);
    spn_mem_->setValue(4096);
    mem_row->addWidget(spn_mem_);
    mem_row->addStretch();
    manual_layout->addLayout(mem_row);

    auto *thread_row = new QHBoxLayout();
    thread_row->addWidget(new QLabel("计算线程数："));
    spn_thread_ = new QSpinBox();
    spn_thread_->setRange(1, 128);
    spn_thread_->setValue(4);
    thread_row->addWidget(spn_thread_);
    thread_row->addStretch();
    manual_layout->addLayout(thread_row);

    perf_layout->addWidget(manual_group);

    auto *hint_label = new QLabel("提示：修改性能设置后需重启应用生效");
    hint_label->setStyleSheet("color: #909399; font-size: 12px;");
    perf_layout->addWidget(hint_label);

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
    connect(btn_ok, &QPushButton::clicked, this, &SystemSettingDialog::OnAccepted);
    connect(chk_auto_perf_, &QCheckBox::toggled, this, &SystemSettingDialog::OnAutoPerformanceToggled);

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
QGroupBox {
    border: 1px solid #e4e7ed;
    border-radius: 6px;
    margin-top: 8px;
    padding-top: 10px;
    font-weight: 500;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #606266;
}
    )QSS");
}

void SystemSettingDialog::LoadSettings() {
    auto &cfg = core::AppConfig::Instance();

    int total_mem = core::AppConfig::GetTotalMemoryMB();
    int cpu_cores = core::AppConfig::GetCpuCoreCount();
    lbl_sys_info_->setText(
        QString("系统总内存：%1 GB (%2 MB)<br>"
                "CPU核心数：%3 核<br>"
                "自动优化配置：内存限制 %4 MB，计算线程 %5 个")
            .arg(total_mem / 1024)
            .arg(total_mem)
            .arg(cpu_cores)
            .arg(static_cast<int>(total_mem * 0.9))
            .arg(std::max(1, static_cast<int>(cpu_cores * 0.9)))
    );

    chk_auto_perf_->setChecked(cfg.GetAutoPerformance());
    spn_mem_->setValue(cfg.GetMemoryLimitMB());
    spn_thread_->setValue(cfg.GetThreadCount());

    OnAutoPerformanceToggled(cfg.GetAutoPerformance());
}

void SystemSettingDialog::SaveSettings() {
    auto &cfg = core::AppConfig::Instance();

    if (chk_auto_perf_->isChecked()) {
        cfg.SetAutoPerformance(true);
    } else {
        cfg.SetMemoryLimitMB(spn_mem_->value());
        cfg.SetThreadCount(spn_thread_->value());
    }
}

void SystemSettingDialog::OnAutoPerformanceToggled(bool checked) {
    spn_mem_->setEnabled(!checked);
    spn_thread_->setEnabled(!checked);
}

void SystemSettingDialog::OnAccepted() {
    SaveSettings();
    accept();
}

} // namespace freight::ui::dialogs
