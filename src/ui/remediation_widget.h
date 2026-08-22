#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>

class QScrollArea;
class QVBoxLayout;

namespace gno {

// "Оптимизация Windows": allowlisted fixes table with observe/apply/rollback,
// plus the honest can-do / cannot-do capability lists.
class RemediationWidget : public QWidget {
    Q_OBJECT
public:
    explicit RemediationWidget(QWidget* parent = nullptr);

private slots:
    void onObserveClicked();
    void onApplyClicked();
    void onRollbackClicked();
    void onRollbackSelectedClicked();

private:
    void setupUI();
    void refreshHistory();
    static QString backupDir();

    QTableWidget* m_table_;
    QPushButton* m_observe_btn_;
    QPushButton* m_apply_btn_;
    QPushButton* m_rollback_btn_;
    QLabel* m_status_label_;
    QTextEdit* m_result_label_;
    QCheckBox* m_autopilot_;
    QTableWidget* m_history_;
    QPushButton* m_rollback_sel_btn_;

};

} // namespace gno
