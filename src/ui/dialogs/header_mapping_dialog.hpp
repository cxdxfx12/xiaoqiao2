#pragma once
#include <QDialog>
#include <QMap>
#include <QStringList>

class QTableWidget;
class QLabel;
class QPushButton;

namespace freight::ui::dialogs {

class MappingView;

class HeaderMappingDialog : public QDialog {
    Q_OBJECT
public:
    HeaderMappingDialog(const QStringList& imported_headers,
                        const QMap<QString, QString>& auto_mapping,
                        const QStringList& preview_headers,
                        const QList<QStringList>& preview_rows,
                        QWidget* parent = nullptr);

    QMap<QString, QString> GetMapping() const;
    bool IsRememberRequested() const { return remember_; }

private slots:
    void OnConfirm();
    void OnOpenMappingSettings();

private:
    void SetupUI();

    QStringList imported_headers_;
    QStringList standard_names_;
    QStringList required_;
    QMap<QString, QString> mapping_;

    QStringList preview_headers_;
    QList<QStringList> preview_rows_;

    MappingView* view_ = nullptr;
    QTableWidget* preview_table_ = nullptr;
    QLabel* hint_label_ = nullptr;
    QPushButton* btn_settings_ = nullptr;
    bool remember_ = false;
};

} // namespace freight::ui::dialogs
