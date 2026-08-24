#pragma once

#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTableWidget>

namespace gno {

// «Тонкая настройка Windows»: декларативный каталог твиков с транзакционным
// применением и откатом последнего пакета.
class FineTuneWidget : public QWidget {
    Q_OBJECT
public:
    explicit FineTuneWidget(QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onApplyClicked();
    void onRollbackClicked();

private:
    void setupUI();
    void refreshTable();

    QComboBox* m_category_;
    QCheckBox* m_only_diff_;
    QTableWidget* m_table_;
    QPushButton* m_refresh_btn_;
    QPushButton* m_apply_btn_;
    QPushButton* m_rollback_btn_;
    QLabel* m_status_;
};

} // namespace gno
