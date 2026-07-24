#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include "core/app_config.hpp"

namespace freight::services {

struct TemplateMatchResult {
    bool matched = false;
    QString template_id;
    QString display_name;
    QString courier_name;
    QMap<QString, QString> suggested_mapping;
    int match_score = 0;
    QStringList matched_keywords;
    bool is_builtin = true;
};

class TemplateRecognizer : public QObject {
    Q_OBJECT
public:
    explicit TemplateRecognizer(QObject *parent = nullptr);

    TemplateMatchResult RecognizeFromColumns(const QStringList &column_names,
                                              const QList<QStringList> &preview_rows = {});

    TemplateMatchResult RecognizeFromFileContent(const QStringList &column_names,
                                                   const QString &all_text_snapshot);

    static QStringList GetAllCourierNames();

    QPair<QMap<QString, QString>, QString> ApplyTemplateMapping(
        const QString &template_id,
        const QStringList &actual_columns);

    bool SaveCurrentAsCustomTemplate(const QString &template_id,
                                     const QString &display_name,
                                     const QString &courier_name,
                                     const QStringList &actual_columns,
                                     const QMap<QString, QString> &confirmed_mapping);

    static quint32 ComputeColumnFingerprintCRC32(const QStringList &cols);

private:
    int ScoreTemplate(const core::TemplateFingerprint &fp,
                      const QStringList &column_names,
                      const QString &text_haystack,
                      QStringList *out_matched_keywords = nullptr);

    QMap<QString, QString> BuildMappingFromTemplate(
        const core::TemplateFingerprint &fp,
        const QStringList &actual_cols);
};

} // namespace freight::services
