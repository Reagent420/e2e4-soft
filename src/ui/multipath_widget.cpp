#include "multipath_widget.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

#include <QMetaObject>

namespace gno {

namespace {

QString msText(double value, int precision = 1) {
    return QString::number(value, 'f', precision);
}

} // namespace

MultipathWidget::MultipathWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    // Колбэки движка вызываются из потока зондирования. Qt-объекты трогать
    // напрямую оттуда нельзя, поэтому перебрасываем себя в поток интерфейса
    // через QPointer (виджет могут удалить, пока идёт цикл зондирования).
    QPointer<MultipathWidget> self(this);
    engine_.setMetricsUpdateCallback([self](const std::vector<PathMetrics>& paths) {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, paths]() {
            if (self) self->onMetricsUpdated(paths);
        }, Qt::QueuedConnection);
    });
    engine_.setPathSwitchCallback([self](uint32_t old_id, uint32_t new_id) {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, old_id, new_id]() {
            if (self) self->onPathSwitched(old_id, new_id);
        }, Qt::QueuedConnection);
    });
}

MultipathWidget::~MultipathWidget()
{
    // Останавливаем движок до разрушения виджета: иначе поток зондирования
    // успел бы постучаться в уже удалённую таблицу. stop() джойнит поток.
    engine_.stop();
}

void MultipathWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QString::fromUtf8("Мультипуть"), this);
    title->setObjectName("sectionTitle");
    titleRow->addWidget(title);
    titleRow->addStretch();
    auto* subtitle = new QLabel(QString::fromUtf8(
        "Каждый локальный шлюз — отдельный путь. Программа зондирует их и выбирает лучший"), this);
    subtitle->setObjectName("sectionSubtitle");
    titleRow->addWidget(subtitle);
    mainLayout->addLayout(titleRow);

    // ---------- Панель управления ----------
    auto* panel = new QFrame(this);
    panel->setObjectName("settingsGroup");
    auto* cp = new QHBoxLayout(panel);
    cp->setContentsMargins(14, 10, 14, 10);
    cp->setSpacing(10);

    cp->addWidget(new QLabel(QString::fromUtf8("Адрес для проверки:"), panel));
    m_targetEdit_ = new QLineEdit(QStringLiteral("8.8.8.8"), panel);
    m_targetEdit_->setMaximumWidth(160);
    m_targetEdit_->setPlaceholderText(QStringLiteral("1.1.1.1"));
    cp->addWidget(m_targetEdit_);

    m_startBtn_ = new QPushButton(QString::fromUtf8("▶  Запустить"), panel);
    m_startBtn_->setObjectName("boostButton");
    m_startBtn_->setFixedHeight(40);
    m_startBtn_->setMinimumWidth(160);
    m_startBtn_->setCursor(Qt::PointingHandCursor);
    cp->addWidget(m_startBtn_);

    m_stopBtn_ = new QPushButton(QString::fromUtf8("■  Остановить"), panel);
    m_stopBtn_->setFixedHeight(40);
    m_stopBtn_->setMinimumWidth(160);
    m_stopBtn_->setCursor(Qt::PointingHandCursor);
    m_stopBtn_->setVisible(false);
    cp->addWidget(m_stopBtn_);

    m_autoSwitchCb_ = new QCheckBox(QString::fromUtf8("Автовыбор лучшего пути"), panel);
    m_autoSwitchCb_->setChecked(true);
    cp->addWidget(m_autoSwitchCb_);
    cp->addStretch();
    mainLayout->addWidget(panel);

    m_statusLabel_ = new QLabel(QString::fromUtf8(
        "Движок остановлен. Нажми «Запустить» — программа перечислит интерфейсы со своими "
        "шлюзами и начнёт их зондировать."), this);
    m_statusLabel_->setObjectName("sectionSubtitle");
    m_statusLabel_->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel_);

    // ---------- Таблица путей ----------
    m_table_ = new QTableWidget(this);
    m_table_->setColumnCount(6);
    m_table_->setHorizontalHeaderLabels({
        QString::fromUtf8("Путь"),
        QString::fromUtf8("Шлюз"),
        QString::fromUtf8("Задержка"),
        QString::fromUtf8("Джиттер"),
        QString::fromUtf8("Потери"),
        QString::fromUtf8("Состояние"),
    });
    m_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int col = 2; col < 6; ++col)
        m_table_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    m_table_->verticalHeader()->setVisible(false);
    m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table_->setSelectionMode(QAbstractItemView::NoSelection);
    m_table_->setFocusPolicy(Qt::NoFocus);
    m_table_->setRowCount(0);
    mainLayout->addWidget(m_table_, 1);

    connect(m_startBtn_, &QPushButton::clicked, this, &MultipathWidget::onStart);
    connect(m_stopBtn_, &QPushButton::clicked, this, &MultipathWidget::onStop);
    connect(m_autoSwitchCb_, &QCheckBox::toggled, this, &MultipathWidget::onAutoSwitchToggled);
}

void MultipathWidget::setRunningUi(bool running)
{
    if (m_running_ == running) return;
    m_running_ = running;
    m_startBtn_->setVisible(!running);
    m_stopBtn_->setVisible(running);
    m_targetEdit_->setEnabled(!running);
    m_autoSwitchCb_->setEnabled(!running);
    emit engineRunningChanged(running);
}

void MultipathWidget::onStart()
{
    QString target = m_targetEdit_->text().trimmed();
    if (target.isEmpty()) {
        m_statusLabel_->setText(QString::fromUtf8("Укажи адрес для проверки — по нему сравниваются пути."));
        return;
    }

    MultipathConfig cfg;
    cfg.probe_interval_ms = 2000;   // как в консольном --multipath
    cfg.auto_switch = m_autoSwitchCb_->isChecked();
    engine_.setConfig(cfg);

    // На старте очищаем таблицу: метрики прошлой сессии не должны висеть,
    // пока не прошёл первый цикл зондирования.
    m_table_->setRowCount(0);

    engine_.start(target.toStdString());
    if (!engine_.isActive()) {
        m_statusLabel_->setText(QString::fromUtf8("Не удалось запустить движок зондирования."));
        return;
    }

    setRunningUi(true);
    if (engine_.getPathMetrics().empty()) {
        m_statusLabel_->setText(QString::fromUtf8(
            "Не найдено интерфейсов со шлюзом — зондировать нечего. "
            "Подключи сеть (кабель или Wi-Fi) и попробуй снова."));
        // Пути отсутствуют — продолжать бессмысленно, но движок активен:
        // остановим, чтобы не висел вхолостую.
        engine_.stop();
        setRunningUi(false);
        return;
    }
    m_statusLabel_->setText(QString::fromUtf8("Зондирую шлюзы локальных интерфейсов…"));
}

void MultipathWidget::onStop()
{
    engine_.stop();
    setRunningUi(false);
    m_statusLabel_->setText(QString::fromUtf8("Зондирование остановлено. Метрики последнего цикла остаются в таблице."));
}

void MultipathWidget::onAutoSwitchToggled(bool on)
{
    // Переключатель доступен только при остановленном движке, но на старте
    // конфиг всё равно перечитывается — так что эта установка не теряется.
    MultipathConfig cfg = engine_.getConfig();
    cfg.auto_switch = on;
    engine_.setConfig(cfg);
}

QString MultipathWidget::describePath(const PathMetrics& m, bool active)
{
    // Активный путь — единственный, по которому идёт выбор движка; метки
    // «●» в отдельной колонке нет, поэтому активность пишем в состояние.
    if (!m.is_healthy)
        return QString::fromUtf8("⚠ не отвечает");
    if (active)
        return QString::fromUtf8("● активный, здоров");
    return QString::fromUtf8("здоров");
}

void MultipathWidget::onMetricsUpdated(const std::vector<PathMetrics>& paths)
{
    refreshPathRows(paths);
    if (paths.empty()) {
        m_statusLabel_->setText(QString::fromUtf8("Пути исчезли — например, отключился интерфейс."));
        return;
    }
    const auto best = engine_.getBestPath();
    if (best) {
        const auto gw = engine_.getPathGateway(best->path_id);
        m_statusLabel_->setText(QString::fromUtf8("Лучший путь: #%1 (%2) — %3 мс")
            .arg(best->path_id)
            .arg(gw ? QString::fromStdString(*gw) : QStringLiteral("?"))
            .arg(msText(best->latency_ms, 0)));
    }
}

void MultipathWidget::refreshPathRows(const std::vector<PathMetrics>& paths)
{
    const uint32_t active_id = engine_.getActivePathId();

    m_table_->setRowCount(static_cast<int>(paths.size()));
    for (int row = 0; row < static_cast<int>(paths.size()); ++row) {
        const PathMetrics& m = paths[static_cast<size_t>(row)];
        const bool active = m.path_id == active_id;
        const auto gw = engine_.getPathGateway(m.path_id);
        const QString gwText = gw ? QString::fromStdString(*gw) : QStringLiteral("—");
        const QString state = describePath(m, active);
        const QString latency = m.latency_ms > 0.0 ? msText(m.latency_ms, 0) + QStringLiteral(" мс")
                                                   : QStringLiteral("—");
        const QString jitter = m.jitter_ms > 0.0 ? msText(m.jitter_ms) + QStringLiteral(" мс")
                                                 : QStringLiteral("—");
        const QString loss = msText(m.packet_loss_percent) + QStringLiteral(" %");

        const QStringList cells = {
            QStringLiteral("#%1").arg(m.path_id), gwText, latency, jitter, loss, state
        };
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setTextAlignment(Qt::AlignVCenter | (col == 1 ? Qt::AlignLeft : Qt::AlignCenter));
            if (active) {
                // Подсветка активного пути: яркий текст + лёгкий фон рамки.
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
                item->setForeground(QColor(theme::Colors::ACCENT_CYAN));
            } else if (!m.is_healthy) {
                item->setForeground(QColor(theme::Colors::WARNING));
            }
            m_table_->setItem(row, col, item);
        }
    }
}

void MultipathWidget::onPathSwitched(uint32_t old_id, uint32_t new_id)
{
    refreshPathRows(engine_.getPathMetrics());

    const auto from = engine_.getPathGateway(old_id);
    const auto to = engine_.getPathGateway(new_id);
    const QString summary = QString::fromUtf8("Переключение пути: #%1 (%2) → #%3 (%4)")
        .arg(old_id).arg(from ? QString::fromStdString(*from) : QStringLiteral("?"))
        .arg(new_id).arg(to ? QString::fromStdString(*to) : QStringLiteral("?"));
    m_statusLabel_->setText(summary);
    emit pathSwitched(summary);
}

} // namespace gno
