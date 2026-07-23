#include "services/template_recognizer.hpp"
#include "services/calc_service.hpp"
#include "core/app_config.hpp"
#include <QSet>
#include <QByteArray>
#include <QCryptographicHash>
#include <algorithm>

namespace freight::services {

TemplateRecognizer::TemplateRecognizer(QObject *parent) : QObject(parent) {}

QStringList TemplateRecognizer::GetAllCourierNames() {
    QSet<QString> s;
    for (const auto &fp : core::AppConfig::BuiltinTemplateFingerprints()) {
        s.insert(fp.courier_name);
    }
    auto list = s.values();
    std::sort(list.begin(), list.end());
    return list;
}

quint32 TemplateRecognizer::ComputeColumnFingerprintCRC32(const QStringList &cols) {
    QString normalized;
    for (const auto &c : cols) {
        normalized += c.trimmed().toLower() + "|";
    }
    QByteArray ba = normalized.toUtf8();
    quint32 crc = qChecksum(ba.constData(), ba.size());
    return crc;
}

int TemplateRecognizer::ScoreTemplate(const core::TemplateFingerprint &fp,
                                      const QStringList &column_names,
                                      const QString &text_haystack,
                                      QStringList *out_matched_keywords) {
    int score = 0;
    QSet<QString> matched_kw;

    QString all_cols = column_names.join(" ").toLower();
    QString haystack = (all_cols + " " + text_haystack).toLower();

    for (const auto &kw : fp.required_keywords) {
        if (haystack.contains(kw.toLower())) {
            matched_kw.insert(kw);
            score += 25;
        }
    }

    int col_hits = 0;
    for (auto it = fp.column_mapping.begin(); it != fp.column_mapping.end(); ++it) {
        const QString &actual_name = it.key();
        for (const auto &c : column_names) {
            QString cl = c.trimmed().toLower();
            QString al = actual_name.trimmed().toLower();
            if (cl == al || cl.contains(al) || al.contains(cl)) {
                col_hits++;
                break;
            }
        }
    }
    score += col_hits * 15;

    if (out_matched_keywords) {
        *out_matched_keywords = matched_kw.values();
    }
    return score;
}

QMap<QString, QString> TemplateRecognizer::BuildMappingFromTemplate(
    const core::TemplateFingerprint &fp, const QStringList &actual_cols) {

    QMap<QString, QString> result;

    for (const auto &std_col : core::AppConfig::StandardColumnOrder()) {
        QString best_actual;
        int best_score = -1;

        for (const auto &actual : actual_cols) {
            QString al = actual.trimmed().toLower();

            for (auto it = fp.column_mapping.begin(); it != fp.column_mapping.end(); ++it) {
                if (it.value() != std_col) continue;
                QString template_kw = it.key().trimmed().toLower();
                int s = 0;
                if (al == template_kw) s = 100;
                else if (al.contains(template_kw)) s = 70;
                else if (template_kw.contains(al)) s = 50;
                if (s > best_score) {
                    best_score = s;
                    best_actual = actual;
                }
            }
        }

        if (best_score >= 50 && !best_actual.isEmpty()) {
            result[std_col] = best_actual;
        }
    }

    if (!result.contains("dest_province") || !result.contains("weight")) {
        services::CalcService tmp;
        auto auto_map = tmp.AutoMapColumns(actual_cols);
        for (auto it = auto_map.begin(); it != auto_map.end(); ++it) {
            if (!result.contains(it.key())) {
                result[it.key()] = it.value();
            }
        }
    }
    return result;
}

TemplateMatchResult TemplateRecognizer::RecognizeFromColumns(
    const QStringList &column_names, const QList<QStringList> &preview_rows) {

    QString haystack;
    for (int r = 0; r < qMin(3, preview_rows.size()); ++r) {
        haystack += preview_rows[r].join(" ") + " ";
    }
    return RecognizeFromFileContent(column_names, haystack);
}

TemplateMatchResult TemplateRecognizer::RecognizeFromFileContent(
    const QStringList &column_names, const QString &all_text_snapshot) {

    TemplateMatchResult best;
    int best_score = 0;

    auto &cfg = core::AppConfig::Instance();
    if (!cfg.GetTemplateAutoDetectGlobal()) {
        best.matched = false;
        best.display_name = "模板识别已在管理面板中关闭";
        return best;
    }

    // 自定义优先（相同ID时，自定义覆盖内置）：先取自定义+内置，enabled=true，自定义去重优先
    QList<core::TemplateFingerprint> enabled_only = cfg.GetAllTemplateFingerprints(true);
    QMap<QString, core::TemplateFingerprint> dedup_map;
    for (const auto &fp : enabled_only) {
        if (dedup_map.contains(fp.template_id)) {
            if (fp.is_builtin) continue;           // 已有同ID的自定义，跳过内置
            else dedup_map[fp.template_id] = fp;   // 覆盖内置（自定义优先）
        } else {
            dedup_map.insert(fp.template_id, fp);
        }
    }

    for (auto it = dedup_map.constBegin(); it != dedup_map.constEnd(); ++it) {
        const core::TemplateFingerprint &fp = it.value();
        QStringList matched_kw;
        int s = ScoreTemplate(fp, column_names, all_text_snapshot, &matched_kw);
        if (s > best_score) {
            best_score = s;
            best.matched = (s >= 40);
            best.template_id = fp.template_id;
            best.display_name = fp.display_name;
            best.courier_name = fp.courier_name;
            best.suggested_mapping = BuildMappingFromTemplate(fp, column_names);
            best.match_score = s;
            best.matched_keywords = matched_kw;
            best.is_builtin = fp.is_builtin;
        }
    }
    return best;
}

QPair<QMap<QString, QString>, QString> TemplateRecognizer::ApplyTemplateMapping(
    const QString &template_id, const QStringList &actual_columns) {

    QList<core::TemplateFingerprint> all = core::AppConfig::Instance().GetAllTemplateFingerprints();
    for (const auto &fp : all) {
        if (fp.template_id == template_id) {
            return qMakePair(BuildMappingFromTemplate(fp, actual_columns), fp.display_name);
        }
    }
    services::CalcService tmp;
    return qMakePair(tmp.AutoMapColumns(actual_columns), QString("自动匹配"));
}

bool TemplateRecognizer::SaveCurrentAsCustomTemplate(
    const QString &template_id,
    const QString &display_name,
    const QString &courier_name,
    const QStringList &actual_columns,
    const QMap<QString, QString> &confirmed_mapping) {

    core::TemplateFingerprint fp;
    fp.template_id = template_id;
    fp.display_name = display_name;
    fp.courier_name = courier_name;
    fp.required_keywords = actual_columns.mid(0, 5);

    QMap<QString, QString> inv;
    for (auto it = confirmed_mapping.begin(); it != confirmed_mapping.end(); ++it) {
        if (!it.value().isEmpty()) {
            inv[it.value()] = it.key();
        }
    }
    fp.column_mapping = inv;

    core::AppConfig::Instance().AddCustomTemplateFingerprint(fp);
    return true;
}

} // namespace freight::services
