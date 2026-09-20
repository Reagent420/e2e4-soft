#pragma once

// TabbedSection — контейнер «секции с представлениями» для новой IA 8+1.
// Вкладки создаются лениво: контент фабрики строится при первом открытии вкладки
// (спека GNO_CONCEPT: разделы Network/System/Games — это группы представлений).

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>
#include <vector>

namespace gno {

class TabbedSection : public QWidget {
    Q_OBJECT
public:
    explicit TabbedSection(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        m_tabs = new QTabWidget(this);
        m_tabs->setDocumentMode(true);
        layout->addWidget(m_tabs);
        connect(m_tabs, &QTabWidget::currentChanged, this, &TabbedSection::onTabChanged);
    }

    // content_factory вызывается ровно один раз — при первой активации вкладки.
    void addTab(const QString& label, std::function<QWidget*()> content_factory) {
        auto* placeholder = new QWidget(m_tabs);
        const int idx = m_tabs->addTab(placeholder, label);
        m_factories.resize(static_cast<std::size_t>(idx) + 1);
        m_factories[static_cast<std::size_t>(idx)] = std::move(content_factory);
        m_content.resize(static_cast<std::size_t>(idx) + 1, nullptr);
        if (idx == 0) onTabChanged(0); // первая вкладка активна сразу
    }

    void setCurrentTab(int i) { m_tabs->setCurrentIndex(i); }

    // Контент вкладки, если он уже построен; nullptr — вкладку ещё не открывали
    // (фабрика вызывается лениво, при первой активации), и тогда беспокоить
    // некого. Возвращаем именно созданный контент, а не placeholder: до первой
    // активации в m_tabs лежит пустая заглушка. Нужно, чтобы одна вкладка могла
    // уведомить соседнюю об общем изменении (см. «Прайм-сетап» → «Центр
    // изменений»: у них один журнал транзакций).
    QWidget* contentAt(int index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= m_content.size()) return nullptr;
        return m_content[static_cast<std::size_t>(index)];
    }

private:
    void onTabChanged(int index) {
        if (m_filling || index < 0 ||
            static_cast<std::size_t>(index) >= m_factories.size()) return;
        auto& factory = m_factories[static_cast<std::size_t>(index)];
        if (!factory) return;
        QWidget* content = factory();
        factory = nullptr;
        const QString label = m_tabs->tabText(index);
        m_filling = true;
        auto* placeholder = m_tabs->widget(index);
        m_tabs->removeTab(index);          // placeholder без контента
        const int pos = m_tabs->insertTab(index, content, label);
        m_tabs->setCurrentIndex(pos < 0 ? index : pos);
        m_filling = false;
        placeholder->deleteLater();
        if (index < static_cast<int>(m_content.size()))
            m_content[static_cast<std::size_t>(index)] = content;
    }

    QTabWidget* m_tabs;
    std::vector<std::function<QWidget*()>> m_factories;
    // Созданный контент вкладок (nullptr, пока вкладку не открывали).
    std::vector<QWidget*> m_content;
    bool m_filling = false;
};

} // namespace gno
