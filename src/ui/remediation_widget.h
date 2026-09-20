#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QSet>
#include <QSettings>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimeEdit>

class QScrollArea;
class QVBoxLayout;

namespace gno {

// "Оптимизация Windows": allowlisted fixes table with observe/apply/rollback,
// plus the honest can-do / cannot-do capability lists.
class RemediationWidget : public QWidget {
    Q_OBJECT
public:
    explicit RemediationWidget(QWidget* parent = nullptr);

    // Перечитать журнал транзакций. Нужно, когда изменения применили из другой
    // вкладки («Прайм-сетап»): у обеих один и тот же журнал, и без этого вызова
    // таблица истории показывала бы устаревший список.
    void refreshJournal() { refreshHistory(); }

signals:
    // Журнал транзакций изменился (здесь выполнено применение или откат).
    // Соседняя вкладка «Прайм-сетап» по этому сигналу пересчитывает доступность
    // своей кнопки отката — иначе после одиночного фикса она оставалась серой
    // при реально применённых изменениях.
    void journalChanged();

private slots:
    void onObserveClicked();
    void onApplyClicked();
    void onRollbackClicked();
    void onRollbackSelectedClicked();

private:
    void setupUI();
    void refreshHistory();
    // Единая точка включения/выключения кнопок на время операции: две
    // параллельные операции (применение + откат) писали бы в один журнал.
    void setBusy(bool busy);
    void updateRollbackButtons();
    void updateSelectedRollbackButton();
    static QString backupDir();

    QTableWidget* m_table_;
    QPushButton* m_observe_btn_;
    QPushButton* m_apply_btn_;
    QPushButton* m_rollback_btn_;
    QLabel* m_status_label_;
    QTextEdit* m_result_label_;
    QCheckBox* m_autopilot_;
    QCheckBox* m_evening_;
    QTimeEdit* m_evening_time_;
    QCheckBox* m_morning_;
    QTableWidget* m_history_;
    QPushButton* m_rollback_sel_btn_;

    // id транзакций, которые движок действительно может откатить
    // (isRollbackEligible). Заполняется в refreshHistory().
    QSet<QString> m_revertable_ids_;
    // Итог последнего применения. Нужен потому, что сразу после применения
    // страница перезапускает «Проверить состояние», и её сводка затирала
    // сообщение о результате — пользователь не видел ни «применено N», ни
    // «не удалось M».
    QString m_apply_summary_;
    bool busy_ = false;
};

} // namespace gno
