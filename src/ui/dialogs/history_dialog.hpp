#pragma once
#include <QDialog>
#include <QTableWidget>

namespace freight::ui::dialogs {

class HistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);
    ~HistoryDialog() override;

private:
    void SetupUI();
    QTableWidget *table_ = nullptr;
};

} // namespace freight::ui::dialogs
