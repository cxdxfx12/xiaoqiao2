#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QStringList>

namespace freight::db { class SqliteRuleRepository; }

namespace freight::ui::dialogs {

class TemplateEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit TemplateEditDialog(const QString &template_id, QWidget *parent = nullptr);
    ~TemplateEditDialog() override;

    void SetTemplateId(const QString &id);

private:
    void SetupUI();
    void LoadData();

    // 模板基本信息
    QLineEdit *edt_id_ = nullptr;
    QLineEdit *edt_name_ = nullptr;
    QLineEdit *edt_carrier_ = nullptr;
    QDoubleSpinBox *spn_first_weight_ = nullptr;
    QDoubleSpinBox *spn_add_unit_ = nullptr;
    QDoubleSpinBox *spn_vol_ratio_ = nullptr;
    QCheckBox *chk_default_ = nullptr;
    QLineEdit *edt_desc_ = nullptr;

    // 阶梯价格表
    QTableWidget *pricing_table_ = nullptr;

    // 分区-省份表
    QTableWidget *zone_table_ = nullptr;

    // 燃油附加费表
    QTableWidget *fuel_table_ = nullptr;

    QString template_id_;
    bool is_new_ = false;

    void OnSave();
    void OnSavePricing();
    void OnSaveZones();
    void OnSaveFuel();
    void OnPricingCellChanged(int row, int col);

    db::SqliteRuleRepository *repo_ = nullptr;
    bool loading_data_ = false;
};

} // namespace freight::ui::dialogs
