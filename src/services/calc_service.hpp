#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <QVariantMap>
#include <QVariantList>
#include "core/freight_types.hpp"

namespace freight::services {

class CalcService : public QObject {
    Q_OBJECT

public:
    explicit CalcService(QObject *parent = nullptr);

    core::CalcResult CalcSingle(const QString &province,
                                 double weight,
                                 double vol_weight = 0.0,
                                 const QString &template_id = "zto_standard",
                                 const QString &customer_id = QString());

    bool CalcBatch(const QString &input_table,
                   const QString &output_table);

    bool CalcFromFile(const QString &input_file,
                      const QString &output_file);

    // 表头映射支持
    QStringList GetTableColumns(const QString &table_name);
    QMap<QString, QString> AutoMapColumns(const QStringList &actual_cols);
    QString CreateNormalizedTable(const QString &input_table,
                                  const QMap<QString, QString> &mapping);
    QString NormalizeColumns(const QString &input_table);

    // 获取预览数据（前5行）
    QStringList GetPreviewHeaders(const QString &table_name);
    QList<QStringList> GetPreviewRows(const QString &table_name, int max_rows = 5);

signals:
    void ProgressChanged(int percent);
    void CalcFinished(bool success, const QString &message);

private:
    QString BuildCalcSQL(const QString &input_table,
                         const QString &output_table);
};

} // namespace freight::services
