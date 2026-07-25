#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
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
    double CurrentAddUnit() const;
    int    CurrentVolDivisor() const;
    QString CurrentRoundingMode() const;
    void SyncVolDivisorComboFromValue(int value);
    void SyncAddUnitComboFromValue(double value);
    void SyncRoundingComboFromMode(const QString &mode);

    // 模板基本信息
    QLineEdit *edt_id_ = nullptr;
    QLineEdit *edt_name_ = nullptr;
    QLineEdit *edt_carrier_ = nullptr;
    QCheckBox *chk_default_ = nullptr;
    QLineEdit *edt_desc_ = nullptr;

    // 计费参数（首重 / 续重进位 / 续重单位 / 体积重除数 / 无重默认运费）
    QDoubleSpinBox *spn_first_weight_ = nullptr;
    QComboBox     *cb_rounding_mode_ = nullptr;
    QComboBox     *cb_add_unit_ = nullptr;
    QDoubleSpinBox *spn_add_unit_custom_ = nullptr;
    QComboBox     *cb_vol_divisor_ = nullptr;
    QSpinBox      *spn_vol_divisor_custom_ = nullptr;
    QDoubleSpinBox *spn_no_weight_fee_ = nullptr;

    // 为了兼容代码中其他地方仍使用 spn_add_unit_/spn_vol_ratio_ 的访问路径，
    // 保留两个别名字段，指向最新值（用于 LoadData / OnSave 内部兼容）
    [[deprecated("use CurrentAddUnit()")]] QDoubleSpinBox *spn_add_unit_ = nullptr;
    [[deprecated("use CurrentVolDivisor()")]] QDoubleSpinBox *spn_vol_ratio_ = nullptr;

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
