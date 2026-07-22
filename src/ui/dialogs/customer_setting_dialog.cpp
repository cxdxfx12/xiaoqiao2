#include "ui/dialogs/customer_setting_dialog.hpp"
#include "ui/icon_manager.hpp"
#include "db/sqlite_rule_repository.hpp"
#include "core/app_config.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QMessageBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>
#include <QFont>
#include <QSet>
#include <QMap>

namespace freight::ui::dialogs {

CustomerSettingDialog::CustomerSettingDialog(QWidget *parent) : QDialog(parent) {
    SetupUI();
    LoadCustomerList();
}

CustomerSettingDialog::~CustomerSettingDialog() = default;

void CustomerSettingDialog::SetupUI() {
    auto &icons = IconManager::Instance();

    setWindowTitle("客户规则设置");
    setWindowIcon(icons.SettingIcon("customer"));
    resize(1200, 700);
    setModal(true);

    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(15, 15, 15, 15);
    main_layout->setSpacing(10);

    auto *splitter = new QSplitter(Qt::Horizontal);

    // 左侧：客户列表
    auto *left_widget = new QWidget();
    auto *left_layout = new QVBoxLayout(left_widget);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    auto *left_title = new QLabel("客户列表");
    left_title->setStyleSheet("font-weight: bold; font-size: 14px; color: #303133;");
    left_layout->addWidget(left_title);

    customer_list_ = new QListWidget();
    customer_list_->setStyleSheet(R"QSS(
QListWidget {
    border: 1px solid #ebeef5;
    border-radius: 6px;
    background: white;
    padding: 4px;
}
QListWidget::item {
    padding: 10px 12px;
    border-radius: 4px;
    margin: 2px 0;
}
QListWidget::item:selected {
    background: #ecf5ff;
    color: #409eff;
}
QListWidget::item:hover {
    background: #f5f7fa;
}
    )QSS");
    left_layout->addWidget(customer_list_, 1);

    // 左侧按钮
    auto *left_btn_layout = new QHBoxLayout();
    auto *btn_add = new QPushButton("新增");
    btn_add->setIcon(icons.ActionIcon("add"));
    btn_add->setCursor(Qt::PointingHandCursor);
    auto *btn_edit = new QPushButton("编辑");
    btn_edit->setCursor(Qt::PointingHandCursor);
    auto *btn_del = new QPushButton("删除");
    btn_del->setIcon(icons.ActionIcon("delete"));
    btn_del->setCursor(Qt::PointingHandCursor);
    left_btn_layout->addWidget(btn_add);
    left_btn_layout->addWidget(btn_edit);
    left_btn_layout->addWidget(btn_del);
    left_layout->addLayout(left_btn_layout);

    auto *btn_import = new QPushButton("批量导入");
    btn_import->setIcon(icons.ActionIcon("import"));
    btn_import->setCursor(Qt::PointingHandCursor);
    left_layout->addWidget(btn_import);

    splitter->addWidget(left_widget);

    // 右侧：报价表
    auto *right_widget = new QWidget();
    auto *right_layout = new QVBoxLayout(right_widget);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);

    auto *right_title_layout = new QHBoxLayout();
    auto *right_title = new QLabel("客户报价表");
    right_title->setStyleSheet("font-weight: bold; font-size: 14px; color: #303133;");
    right_title_layout->addWidget(right_title);
    right_title_layout->addStretch();

    btn_save_pricing_ = new QPushButton("保存报价");
    btn_save_pricing_->setCursor(Qt::PointingHandCursor);
    btn_save_pricing_->setEnabled(false);
    btn_save_pricing_->setStyleSheet(R"QSS(
QPushButton {
    padding: 6px 16px;
    background: #409eff;
    color: white;
    border: none;
    border-radius: 4px;
}
QPushButton:hover { background: #66b1ff; }
QPushButton:disabled { background: #a0cfff; }
    )QSS");
    right_title_layout->addWidget(btn_save_pricing_);
    right_layout->addLayout(right_title_layout);

    pricing_table_ = new QTableWidget(0, 10);
    pricing_table_->setHorizontalHeaderLabels(
        {"报价区域", "目的省份", "0-0.5KG", "0.51KG-1KG", "1-2KG", "2-3KG",
         "3-30KG\n首重", "3-30KG\n续重", "30KG以上\n首重", "30KG以上\n续重"});
    pricing_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pricing_table_->verticalHeader()->setVisible(false);
    pricing_table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    pricing_table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    pricing_table_->horizontalHeader()->setMinimumHeight(40);
    pricing_table_->setStyleSheet(R"QSS(
QTableWidget {
    border: 1px solid #ebeef5;
    border-radius: 6px;
    gridline-color: #ebeef5;
    background: white;
    font-size: 12px;
}
QTableWidget::item { padding: 4px; }
QHeaderView::section {
    background: #f5f7fa;
    padding: 8px 4px;
    border: none;
    border-right: 1px solid #ebeef5;
    border-bottom: 1px solid #ebeef5;
    font-weight: 500;
    font-size: 12px;
}
    )QSS");
    right_layout->addWidget(pricing_table_, 1);

    auto *empty_tip = new QLabel("请选择左侧客户查看报价表");
    empty_tip->setAlignment(Qt::AlignCenter);
    empty_tip->setStyleSheet("color: #909399; font-size: 13px; padding: 40px;");
    right_layout->addWidget(empty_tip);
    pricing_table_->setHidden(true);
    connect(pricing_table_, &QTableWidget::cellChanged, this, &CustomerSettingDialog::OnPricingCellChanged);

    splitter->addWidget(right_widget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setSizes({240, 960});

    main_layout->addWidget(splitter, 1);

    // 底部关闭按钮
    auto *bottom_layout = new QHBoxLayout();
    bottom_layout->addStretch();
    auto *btn_close = new QPushButton("关闭");
    btn_close->setObjectName("normalBtn");
    btn_close->setCursor(Qt::PointingHandCursor);
    btn_close->setStyleSheet(R"QSS(
QPushButton#normalBtn {
    padding: 8px 24px;
    background: white;
    color: #606266;
    border: 1px solid #dcdfe6;
    border-radius: 6px;
}
QPushButton#normalBtn:hover {
    border-color: #409eff;
    color: #409eff;
}
    )QSS");
    bottom_layout->addWidget(btn_close);
    main_layout->addLayout(bottom_layout);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);
    connect(customer_list_, &QListWidget::currentItemChanged, this, &CustomerSettingDialog::OnCustomerSelected);
    connect(btn_save_pricing_, &QPushButton::clicked, this, &CustomerSettingDialog::OnSaveCustomerPricing);

    connect(btn_add, &QPushButton::clicked, this, [this]() {
        ShowCustomerDialog(true);
    });
    connect(btn_edit, &QPushButton::clicked, this, [this]() {
        if (!current_cust_id_.isEmpty()) {
            ShowCustomerDialog(false);
        } else {
            QMessageBox::warning(this, "提示", "请先选择一个客户");
        }
    });
    connect(btn_del, &QPushButton::clicked, this, [this]() {
        if (current_cust_id_.isEmpty()) {
            QMessageBox::warning(this, "提示", "请先选择一个客户");
            return;
        }
        auto ret = QMessageBox::question(this, "确认", "确定要删除该客户吗？");
        if (ret == QMessageBox::Yes) {
            auto &cfg = core::AppConfig::Instance();
            db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
            repo.Init();
            repo.DeleteCustomer(current_cust_id_);
            current_cust_id_.clear();
            LoadCustomerList();
            pricing_table_->setHidden(true);
            btn_save_pricing_->setEnabled(false);
            QMessageBox::information(this, "成功", "客户已删除");
        }
    });
    connect(btn_import, &QPushButton::clicked, this, &CustomerSettingDialog::OnBatchImport);

    setStyleSheet("QDialog { background-color: #f5f7fa; }");
}

void CustomerSettingDialog::LoadCustomerList() {
    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    customer_list_->clear();
    auto customers = repo.ListCustomers();
    for (const auto &c : customers) {
        auto map = c.toMap();
        auto *item = new QListWidgetItem(map["customer_name"].toString());
        item->setData(Qt::UserRole, map["customer_id"].toString());
        double discount = map["discount_rate"].toDouble();
        item->setToolTip(QString("折扣: %1折").arg(discount * 10, 0, 'f', 1));
        customer_list_->addItem(item);
    }
}

void CustomerSettingDialog::OnCustomerSelected(QListWidgetItem *current, QListWidgetItem *previous) {
    Q_UNUSED(previous);
    if (!current) return;

    current_cust_id_ = current->data(Qt::UserRole).toString();
    LoadCustomerPricing(current_cust_id_);
    pricing_table_->setHidden(false);
    btn_save_pricing_->setEnabled(true);
}

void CustomerSettingDialog::LoadCustomerPricing(const QString &cust_id) {
    loading_data_ = true;

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    QString template_id = "cust_" + cust_id;

    // 验证模板是否存在
    QSqlQuery q(repo.Database());
    q.prepare("SELECT group_code, group_name, sort_order FROM zone_groups "
              "WHERE template_id = ? ORDER BY sort_order");
    q.addBindValue(template_id);
    q.exec();

    pricing_table_->setRowCount(0);
    int row = 0;

    while (q.next()) {
        QString group_code = q.value(0).toString();
        QString group_name = q.value(1).toString();

        QSqlQuery q2(repo.Database());
        q2.prepare("SELECT province FROM zone_group_provinces WHERE template_id = ? AND group_code = ? ORDER BY province");
        q2.addBindValue(template_id);
        q2.addBindValue(group_code);
        q2.exec();

        QStringList provinces;
        while (q2.next()) {
            provinces << q2.value(0).toString();
        }

        QSqlQuery q3(repo.Database());
        q3.prepare("SELECT tier_code, first_price, additional_price FROM tiered_pricing "
                   "WHERE template_id = ? AND group_code = ? ORDER BY sort_order");
        q3.addBindValue(template_id);
        q3.addBindValue(group_code);
        q3.exec();

        QMap<QString, QPair<double, double>> tier_prices;
        while (q3.next()) {
            tier_prices[q3.value(0).toString()] = qMakePair(q3.value(1).toDouble(), q3.value(2).toDouble());
        }

        int zone_start_row = row;
        for (int i = 0; i < provinces.size(); i++) {
            pricing_table_->insertRow(row);

            auto *zone_item = new QTableWidgetItem(i == 0 ? group_name : "");
            zone_item->setData(Qt::UserRole, group_code);
            zone_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            zone_item->setTextAlignment(Qt::AlignCenter);
            zone_item->setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
            pricing_table_->setItem(row, 0, zone_item);

            auto *prov_item = new QTableWidgetItem(provinces[i]);
            prov_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            prov_item->setTextAlignment(Qt::AlignCenter);
            pricing_table_->setItem(row, 1, prov_item);

            auto setPrice = [&](int col, const QString &tier_code, bool is_add = false) {
                auto it = tier_prices.find(tier_code);
                if (it != tier_prices.end()) {
                    double val = is_add ? it.value().second : it.value().first;
                    auto *item = new QTableWidgetItem(QString::number(val, 'f', 2));
                    item->setTextAlignment(Qt::AlignCenter);
                    pricing_table_->setItem(row, col, item);
                }
            };

            setPrice(2, "tier_0_0.5");
            setPrice(3, "tier_0.5_1");
            setPrice(4, "tier_1_2");
            setPrice(5, "tier_2_3");
            setPrice(6, "tier_3_30");
            setPrice(7, "tier_3_30", true);
            setPrice(8, "tier_30_plus");
            setPrice(9, "tier_30_plus", true);

            row++;
        }

        if (provinces.size() > 1) {
            pricing_table_->setSpan(zone_start_row, 0, provinces.size(), 1);
        }
    }

    loading_data_ = false;
}

void CustomerSettingDialog::OnPricingCellChanged(int row, int col) {
    if (loading_data_) return;
    if (col < 2 || col >= 10) return;

    auto *zone_item = pricing_table_->item(row, 0);
    if (!zone_item) return;
    QString group_code = zone_item->data(Qt::UserRole).toString();
    if (group_code.isEmpty()) return;

    auto *changed_item = pricing_table_->item(row, col);
    if (!changed_item) return;
    QString new_value = changed_item->text();

    loading_data_ = true;
    for (int r = 0; r < pricing_table_->rowCount(); r++) {
        if (r == row) continue;
        auto *zi = pricing_table_->item(r, 0);
        if (!zi) continue;
        if (zi->data(Qt::UserRole).toString() == group_code) {
            auto *price_item = pricing_table_->item(r, col);
            if (price_item) {
                price_item->setText(new_value);
            }
        }
    }
    loading_data_ = false;
}

void CustomerSettingDialog::OnSaveCustomerPricing() {
    if (current_cust_id_.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择一个客户");
        return;
    }

    auto *editor = pricing_table_->indexWidget(pricing_table_->currentIndex());
    if (editor) {
        pricing_table_->itemDelegate()->commitData(editor);
        pricing_table_->itemDelegate()->closeEditor(editor, QAbstractItemDelegate::NoHint);
    }

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    if (!repo.Init()) {
        QMessageBox::critical(this, "错误", "数据库初始化失败");
        return;
    }

    QString template_id = "cust_" + current_cust_id_;
    QSqlDatabase db = repo.Database();

    if (!db.transaction()) {
        QMessageBox::critical(this, "错误", "无法启动事务: " + db.lastError().text());
        return;
    }

    bool success = true;
    QString error_msg;
    int total_updated = 0;

    QSet<QString> processed_zones;
    for (int row = 0; row < pricing_table_->rowCount(); row++) {
        auto *zone_item = pricing_table_->item(row, 0);
        if (!zone_item) continue;
        QString group_code = zone_item->data(Qt::UserRole).toString();
        if (group_code.isEmpty() || processed_zones.contains(group_code)) continue;
        processed_zones.insert(group_code);

        bool ok_p05, ok_p1, ok_p2, ok_p3, ok_midf, ok_mida, ok_bigf, ok_biga;
        double p05 = pricing_table_->item(row, 2)->text().toDouble(&ok_p05);
        double p1 = pricing_table_->item(row, 3)->text().toDouble(&ok_p1);
        double p2 = pricing_table_->item(row, 4)->text().toDouble(&ok_p2);
        double p3 = pricing_table_->item(row, 5)->text().toDouble(&ok_p3);
        double mid_first = pricing_table_->item(row, 6)->text().toDouble(&ok_midf);
        double mid_add = pricing_table_->item(row, 7)->text().toDouble(&ok_mida);
        double big_first = pricing_table_->item(row, 8)->text().toDouble(&ok_bigf);
        double big_add = pricing_table_->item(row, 9)->text().toDouble(&ok_biga);

        if (!ok_p05 || !ok_p1 || !ok_p2 || !ok_p3 || !ok_midf || !ok_mida || !ok_bigf || !ok_biga) {
            error_msg = QString("第%1行价格格式不正确").arg(row + 1);
            success = false;
            break;
        }

        QVector<QPair<QString, QPair<double, double>>> tiers = {
            {"tier_0_0.5", {p05, 0.0}},
            {"tier_0.5_1", {p1, 0.0}},
            {"tier_1_2", {p2, 0.0}},
            {"tier_2_3", {p3, 0.0}},
            {"tier_3_30", {mid_first, mid_add}},
            {"tier_30_plus", {big_first, big_add}},
        };

        for (const auto &tier : tiers) {
            QSqlQuery q(db);
            q.prepare("UPDATE tiered_pricing SET first_price=?, additional_price=? "
                      "WHERE template_id=? AND group_code=? AND tier_code=?");
            q.addBindValue(tier.second.first);
            q.addBindValue(tier.second.second);
            q.addBindValue(template_id);
            q.addBindValue(group_code);
            q.addBindValue(tier.first);

            if (!q.exec()) {
                error_msg = "保存失败: " + q.lastError().text();
                success = false;
                break;
            }

            if (q.numRowsAffected() == 0) {
                QSqlQuery q2(db);
                q2.prepare("INSERT INTO tiered_pricing "
                           "(template_id, group_code, tier_code, tier_name, "
                           " min_weight, max_weight, first_price, additional_price, sort_order) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
                q2.addBindValue(template_id);
                q2.addBindValue(group_code);
                q2.addBindValue(tier.first);
                q2.addBindValue("");
                q2.addBindValue(0);
                q2.addBindValue(0);
                q2.addBindValue(tier.second.first);
                q2.addBindValue(tier.second.second);
                q2.addBindValue(0);

                if (!q2.exec()) {
                    error_msg = "插入失败: " + q2.lastError().text();
                    success = false;
                    break;
                }
                total_updated++;
            } else {
                total_updated += q.numRowsAffected();
            }
        }

        if (!success) break;
    }

    if (success) {
        if (!db.commit()) {
            error_msg = "提交事务失败: " + db.lastError().text();
            success = false;
        }
    }

    if (!success) {
        db.rollback();
        QMessageBox::critical(this, "错误", error_msg);
        return;
    }

    QMessageBox::information(this, "成功", QString("报价表已保存，共更新 %1 条记录").arg(total_updated));

    LoadCustomerPricing(current_cust_id_);
}

void CustomerSettingDialog::ShowCustomerDialog(bool is_add) {
    QDialog dlg(this);
    dlg.setWindowTitle(is_add ? "新增客户" : "编辑客户");
    dlg.resize(400, 300);
    dlg.setModal(true);

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();

    auto *name_edit = new QLineEdit();
    auto *discount_spin = new QDoubleSpinBox();
    discount_spin->setRange(0.1, 10.0);
    discount_spin->setSingleStep(0.1);
    discount_spin->setDecimals(1);
    discount_spin->setValue(10.0);
    auto *contact_edit = new QLineEdit();
    auto *phone_edit = new QLineEdit();

    QString cust_id_to_edit;
    if (!is_add && !current_cust_id_.isEmpty()) {
        cust_id_to_edit = current_cust_id_;
        auto customers = repo.ListCustomers();
        for (const auto &c : customers) {
            auto map = c.toMap();
            if (map["customer_id"].toString() == cust_id_to_edit) {
                name_edit->setText(map["customer_name"].toString());
                discount_spin->setValue(map["discount_rate"].toDouble() * 10);
                contact_edit->setText(map["contact_person"].toString());
                phone_edit->setText(map["contact_phone"].toString());
                break;
            }
        }
    }

    form->addRow("客户名称:", name_edit);
    form->addRow("折扣率(折):", discount_spin);
    form->addRow("联系人:", contact_edit);
    form->addRow("联系电话:", phone_edit);

    layout->addLayout(form);

    auto *btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(btn_box);

    connect(btn_box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        if (name_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "提示", "客户名称不能为空");
            return;
        }

        QVariantMap cust;
        cust["customer_name"] = name_edit->text().trimmed();
        cust["discount_rate"] = discount_spin->value() / 10.0;
        cust["default_template"] = "";
        cust["contact_person"] = contact_edit->text().trimmed();
        cust["contact_phone"] = phone_edit->text().trimmed();

        QString new_cust_id;
        if (is_add) {
            new_cust_id = "cust_" + QString::number(QDateTime::currentSecsSinceEpoch())
                        + "_" + QString::number(QRandomGenerator::global()->generate() % 10000);
            cust["customer_id"] = new_cust_id;
            if (repo.AddCustomer(cust)) {
                LoadCustomerList();
                // 选中新客户
                for (int i = 0; i < customer_list_->count(); i++) {
                    if (customer_list_->item(i)->data(Qt::UserRole).toString() == new_cust_id) {
                        customer_list_->setCurrentRow(i);
                        break;
                    }
                }
                QMessageBox::information(this, "成功", "客户已添加，默认报价表已生成");
            } else {
                QMessageBox::warning(this, "错误", "添加失败");
            }
        } else {
            cust["customer_id"] = cust_id_to_edit;
            if (repo.UpdateCustomer(cust)) {
                LoadCustomerList();
                // 重新选中
                for (int i = 0; i < customer_list_->count(); i++) {
                    if (customer_list_->item(i)->data(Qt::UserRole).toString() == cust_id_to_edit) {
                        customer_list_->setCurrentRow(i);
                        break;
                    }
                }
                QMessageBox::information(this, "成功", "客户已更新");
            } else {
                QMessageBox::warning(this, "错误", "更新失败");
            }
        }
    }
}

void CustomerSettingDialog::OnBatchImport() {
    QString file_path = QFileDialog::getOpenFileName(this, "选择CSV文件", "", "CSV文件 (*.csv)");
    if (file_path.isEmpty()) return;

    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QTextStream in(&file);
    QString header = in.readLine();
    QStringList headers = header.split(",");

    int name_col = headers.indexOf(QRegularExpression(".*名称.*|.*name.*", QRegularExpression::CaseInsensitiveOption));
    int discount_col = headers.indexOf(QRegularExpression(".*折扣.*|.*rate.*", QRegularExpression::CaseInsensitiveOption));
    int contact_col = headers.indexOf(QRegularExpression(".*联系人.*|.*contact.*", QRegularExpression::CaseInsensitiveOption));
    int phone_col = headers.indexOf(QRegularExpression(".*电话.*|.*phone.*", QRegularExpression::CaseInsensitiveOption));
    int address_col = headers.indexOf(QRegularExpression(".*地址.*|.*address.*", QRegularExpression::CaseInsensitiveOption));

    if (name_col < 0) {
        QMessageBox::warning(this, "错误", "未找到客户名称列");
        return;
    }

    auto &cfg = core::AppConfig::Instance();
    db::SqliteRuleRepository repo(cfg.GetRulesDbPath());
    repo.Init();

    int success = 0, failed = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(",");

        if (fields.size() <= name_col || fields[name_col].trimmed().isEmpty()) continue;

        QString name = fields[name_col].trimmed();
        double discount = 1.0;
        if (discount_col >= 0 && discount_col < fields.size()) {
            discount = fields[discount_col].trimmed().toDouble() / 10.0;
        }
        QString contact = (contact_col >= 0 && contact_col < fields.size()) ? fields[contact_col].trimmed() : "";
        QString phone = (phone_col >= 0 && phone_col < fields.size()) ? fields[phone_col].trimmed() : "";
        QString address = (address_col >= 0 && address_col < fields.size()) ? fields[address_col].trimmed() : "";

        QVariantMap cust;
        QString cust_id = "cust_" + QString::number(QDateTime::currentSecsSinceEpoch())
                        + "_" + QString::number(QRandomGenerator::global()->generate() % 10000);
        cust["customer_id"] = cust_id;
        cust["customer_name"] = name;
        cust["discount_rate"] = discount;
        cust["default_template"] = "";
        cust["contact_person"] = contact;
        cust["contact_phone"] = phone;
        cust["address"] = address;

        if (repo.AddCustomer(cust)) {
            success++;
        } else {
            failed++;
        }
    }

    file.close();
    LoadCustomerList();
    QMessageBox::information(this, "完成", QString("导入完成：成功 %1 条，失败 %2 条\n每个客户均已生成默认报价表").arg(success).arg(failed));
}

} // namespace freight::ui::dialogs
