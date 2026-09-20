#pragma once

// «Прайм-сетап» — профиль оптимизации одним нажатием (вкладка раздела
// «Оптимизации»). UI только собирает запрос и показывает результат: все
// системные изменения уходят в транзакционный движок (BoostGate ->
// RemediationService -> FixTransaction), у каждого есть бэкап и откат.

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTabWidget>
#include <QTextEdit>
#include <QVector>
#include <QWidget>

#include "../core/system_audit.h"

class QVBoxLayout;
class QGridLayout;

namespace gno {

struct FPSBoostSettings {
    bool disable_game_dvr = true;
    bool disable_fullscreen_opt = true;
    bool disable_mouse_accel = true;
    bool disable_game_mode = false;
    bool optimize_power_plan = true;
    bool set_high_priority = true;
    bool optimize_virtual_memory = false;  // вне белого списка движка — пункт отключён в UI
    // Требуют серверной сети (подписка) — пока недоступны, честно помечены в UI.
    bool multipath_routing = true;
    bool real_time_route = true;
    bool packet_loss_compensation = true;
    bool custom_dns = false;
    bool tcp_optimization = true;
    bool mtu_optimization = true;
    QString dns_server = QStringLiteral("1.1.1.1");
    // Движок пишет единственное безопасное значение (kEngineMtuValue = 1500);
    // прежний дефолт 1400 делал пункт MTU молча неработающим.
    int mtu_value = 1500;
};

class OptimizerWidget : public QWidget {
    Q_OBJECT
public:
    explicit OptimizerWidget(QWidget* parent = nullptr);
    FPSBoostSettings getSettings() const;
    void setSettings(const FPSBoostSettings& settings);

    // Пересчитать состояние, зависящее от журнала транзакций (доступность
    // «Откатить последние изменения»). Нужно, когда применение или откат
    // выполнили в соседней вкладке «Центр изменений» — журнал у обеих один.
    void refreshExternalState();

signals:
    // Системные параметры изменились (применение ИЛИ откат). Соседняя вкладка
    // «Центр изменений» читает тот же журнал и без этого сигнала показывала бы
    // устаревший список транзакций. Раньше сигнал назывался
    // optimizationsApplied и не испускался нигде — то есть был мёртвым.
    void systemStateChanged();

private slots:
    void onApplyClicked();
    void onRevertClicked();
    void onResetClicked();
    void onDnsToggled(bool checked);
    void onMtuToggled(bool checked);
    void onSelectionChanged();
    void onRefreshStateClicked();

private:
    // построение
    void buildHeader(QVBoxLayout* root);
    void buildControlPanel(QVBoxLayout* root);
    void buildTabs(QVBoxLayout* root);
    QWidget* buildProfileTab();
    QWidget* buildChangesTab();
    QWidget* buildLogTab();

protected:
    // Фокус на поле ввода уводит список к середине — при показе возвращаем начало.
    void showEvent(QShowEvent* event) override;

private:
    // состояние
    void loadSettings();
    void saveSettings();
    void setRunningUi(bool running);
    void appendLog(const QString& line);
    void renderChanges(const QVector<SettingChange>& changes);
    QWidget* makeChangeCard(const SettingChange& c);
    void refreshSelectionSummary();
    void refreshRevertState();
    // Испускает systemStateChanged() из рабочего потока (через GUI-поток).
    void notifyStateChanged();
    void refreshReadiness();
    void refreshReadinessAsync();
    void updateInputHints();
    int countSelected() const;
    QString backupsDir() const;
    void setStatus(const QString& text, const QString& color);

    QCheckBox* fps_checkboxes_[7] = {};
    QCheckBox* net_checkboxes_[6] = {};
    QLineEdit* dns_input_ = nullptr;
    QLineEdit* mtu_input_ = nullptr;
    QLabel* dns_hint_ = nullptr;
    QLabel* mtu_hint_ = nullptr;
    QLabel* admin_badge_ = nullptr;
    QLabel* readiness_label_ = nullptr;
    QLabel* selection_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* apply_btn_ = nullptr;
    QPushButton* revert_btn_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QScrollArea* profile_scroll_ = nullptr;
    QWidget* changes_list_ = nullptr;
    QTextEdit* log_view_ = nullptr;

    bool running_ = false;
    bool game_running_ = false;
    bool interface_found_ = false;
    bool is_admin_ = false;
    bool scroll_reset_done_ = false;
};

} // namespace gno
