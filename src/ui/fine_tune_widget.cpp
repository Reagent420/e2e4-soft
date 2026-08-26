#include "fine_tune_widget.h"
#include "theme.h"
#include "core/tweak_service.h"
#include "core/tweak_registry.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <thread>

#ifdef PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace gno {

namespace {

QString appdataDir() {
    const char* env = std::getenv("APPDATA");
    return QString::fromLatin1(env ? env : "") + QStringLiteral("/GNO");
}

// Реальный доступ к реестру по спецификации твика.
class RegistryTweakAccess : public ITweakAccess {
public:
    TweakValue read(const TweakSpec& s) override {
        TweakValue v;
#ifdef PLATFORM_WINDOWS
        HKEY root = s.root == TweakRoot::HKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        HKEY key = nullptr;
        if (RegOpenKeyExA(root, s.subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return v;
        if (s.type == TweakType::Dword) {
            DWORD d = 0, sz = sizeof(d), t = 0;
            if (RegQueryValueExA(key, s.value_name, nullptr, &t,
                                 reinterpret_cast<LPBYTE>(&d), &sz) == ERROR_SUCCESS && t == REG_DWORD) {
                v.existed = true; v.dword = d;
            }
        } else {
            char buf[512] = {};
            DWORD sz = sizeof(buf), t = 0;
            if (RegQueryValueExA(key, s.value_name, nullptr, &t,
                                 reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS && t == REG_SZ) {
                v.existed = true; v.str = buf;
            }
        }
        RegCloseKey(key);
#else
        (void)s;
#endif
        return v;
    }

    void write(const TweakSpec& s, const TweakValue& v) override {
#ifdef PLATFORM_WINDOWS
        HKEY root = s.root == TweakRoot::HKLM ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        HKEY key = nullptr;
        if (RegCreateKeyExA(root, s.subkey, 0, nullptr, 0,
                            KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr)
            != ERROR_SUCCESS)
            throw std::runtime_error(std::string("registry open: ") + s.subkey);
        if (!v.existed) {
            RegDeleteValueA(key, s.value_name);
        } else if (s.type == TweakType::Dword) {
            const DWORD d = v.dword;
            RegSetValueExA(key, s.value_name, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&d), sizeof(d));
        } else {
            RegSetValueExA(key, s.value_name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(v.str.c_str()),
                           static_cast<DWORD>(v.str.size() + 1));
        }
        RegCloseKey(key);
#else
        (void)s; (void)v;
#endif
    }
};

} // namespace

FineTuneWidget::FineTuneWidget(QWidget* parent) : QWidget(parent) { setupUI(); }

void FineTuneWidget::setupUI() {
    setObjectName("fineTunePage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("FINE TUNING"), this);
    title->setStyleSheet(QString("font-size:17px; font-weight:700; letter-spacing:3px;"
                                 "color:%1; background:transparent;")
                             .arg(theme::Colors::ACCENT_NEON));

    auto* subtitle = new QLabel(QString::fromUtf8(
        "\xD0\x9A\xD0\xB0%D1%82\xD0\xB0%D0\xBB%D0%BE%D0%B3\x20\xD0\xBF%D1%80\xD0\xBE%D0%B2%D0%B5%D1%80%D0%B5%D0%BD%D0%BD%D1%8B\xD1%85\x20"
        "\xD1%82\xD0%B2%D0%B8%D0%BA%D0%BE%D0%B2\x20Windows\x2E\x20\xD0\xA1\xD0\xBD%D0%B0%D0\xBF%D1%88%D0%BE%D1%82\x20\xD1\x81\xD0\xBE%D1\x85%D1%80%D0%B0%D0%BD%D1%8F%D0%B5%D1%82\xD1%81%D1%8F\x20"
        "\xD0\xBF%D0%B5%D1%80%D0%B5%D0%B4\x20%D0%BA%D0%B0%D0%B6%D0%B4%D1%8B%D0%BC\x20%D0\xBF%D0%B0%D0%BA%D0%B5%D1%82%D0%BE%D0%BC\x20\x2D\x20\xD0\xBE%D1%82%D0%BA%D0%B0%D1%82\x20\xD0\xB2\x20%D0%BE%D0%B4%D0\xBD%D1%83\x20%D0\xBA%D0%BD%D0%BE%D0\xBF%D0%BA%D1%83\x2E"), this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString("color:%1; background:transparent;")
                                .arg(theme::Colors::TEXT_SECONDARY));

    auto* row = new QHBoxLayout();
    m_category_ = new QComboBox(this);
    m_category_->addItem(QString::fromUtf8("\xD0\x92\xD1\x81\xD0\xB5"), QString());
    {
        std::vector<QString> cats;
        for (const auto& spec : tweaks())
            if (std::find(cats.begin(), cats.end(), spec.category) == cats.end())
                cats.push_back(QString::fromStdString(spec.category));
        for (auto& c : cats) m_category_->addItem(c, c);
    }
    m_only_diff_ = new QCheckBox(QString::fromUtf8(
        "\xD0%A2%D0%BE%D0%BB%D1%8C%D0%BA%D0%BE\x20%D0\xBE%D1%82%D0\xBB%D0%B8%D1%87%D0%B0%D1%8E%D1%89%D0%B8%D0%B5%D1%81%D1%8F"), this);
    m_only_diff_->setChecked(false);

    row->addWidget(new QLabel(QStringLiteral("CATEGORY"), this));
    row->addWidget(m_category_);
    row->addSpacing(10);
    row->addWidget(m_only_diff_);
    row->addStretch();

    auto* btnRow = new QHBoxLayout();
    m_refresh_btn_ = new QPushButton(QStringLiteral("REFRESH"), this);
    m_apply_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9F\xD1%80\xD0%B8%D0%BC%D0%B5%D0%BD%D0%B8%D1%82%D1%8C\x20%D0\xBA%D0%B0%D1%82%D0%B5%D0%B3%D0%BE%D1%80%D0%B8%D1%8E"), this);
    m_apply_btn_->setObjectName("boostButton");
    m_rollback_btn_ = new QPushButton(QString::fromUtf8(
        "\xD0\x9E\xD1\x82%D0%BA%D0%B0%D1%82\x20\xD0\xBF%D0%BE%D1%81%D0%BB%D0%B5%D0%B4%D0%BD%D0%B5%D0%B3%D0\xBE\x20\xD0\xBF%D0%B0%D0%BA%D0%B5%D1%82%D0\xB0"), this);
    btnRow->addWidget(m_refresh_btn_);
    btnRow->addWidget(m_apply_btn_);
    btnRow->addWidget(m_rollback_btn_);
    btnRow->addStretch();

    m_status_ = new QLabel(QStringLiteral(" "), this);
    m_status_->setWordWrap(true);
    m_status_->setStyleSheet(QString("color:%1; background:transparent;")
                                 .arg(theme::Colors::TEXT_SECONDARY));

    m_table_ = new QTableWidget(0, 5, this);
    m_table_->setHorizontalHeaderLabels({QStringLiteral("CATEGORY"),
                                         QString::fromUtf8("\xD0\x9E\xD0\xBF\xD1\x86\xD0\xB8\xD1\x8F"),
                                         QString::fromUtf8("\xD0\xA1\xD0\xB5\xD0\xB9\xD1\x87\xD0\xB0\xD1\x81"),
                                         QString::fromUtf8("\xD0\xA0\xD0\xB5\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB4"),
                                         QString::fromUtf8("\xD0\x9E\xD0\xBF\xD0\xB8\xD1\x81\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5")});
    m_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table_->setColumnWidth(0, 130);
    m_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table_->setColumnWidth(1, 250);
    m_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table_->setColumnWidth(2, 100);
    m_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table_->setColumnWidth(3, 110);
    m_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table_->verticalHeader()->setVisible(false);
    m_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_->setAlternatingRowColors(true);
    m_table_->setWordWrap(true);
    m_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addLayout(row);
    layout->addLayout(btnRow);
    layout->addWidget(m_status_);
    layout->addWidget(m_table_, 1);

    connect(m_refresh_btn_, &QPushButton::clicked, this, &FineTuneWidget::onRefreshClicked);
    connect(m_apply_btn_, &QPushButton::clicked, this, &FineTuneWidget::onApplyClicked);
    connect(m_rollback_btn_, &QPushButton::clicked, this, &FineTuneWidget::onRollbackClicked);
}

void FineTuneWidget::refreshTable() {
    RegistryTweakAccess access;
    TweakService svc(access, appdataDir().toStdString());

    auto views = svc.listViews(m_category_->currentData().toString().toStdString());

    m_table_->setRowCount(static_cast<int>(views.size()));
    int row = 0, differing = 0;
    for (const auto& view : views) {
        const auto& s = *view.spec;
        if (m_only_diff_->isChecked() && !view.differs) continue;
        if (view.differs) ++differing;

        m_table_->setItem(row, 0, new QTableWidgetItem(s.category));
        m_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(s.title)));
        m_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(view.current_text)));
        m_table_->setItem(row, 3, new QTableWidgetItem(
            s.type == TweakType::Dword ? QString::number(s.dword_value) : s.sz_value));
        m_table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(s.description)));
        m_table_->item(row, 2)->setForeground(QBrush(QColor(
            !view.differs ? theme::Colors::SUCCESS : theme::Colors::WARNING)));
        ++row;
    }
    m_table_->setRowCount(row);
    m_table_->resizeRowsToContents();

    m_status_->setText(QString::fromUtf8(
        "\xD0%9E\xD1%82%D0%BB%D0%B8%D1%87%D0%B0%D1%8E%D1%82%D1%81%D1%8F\x20%D0%BE%D1%82\x20%D1%80%D0%B5%D0%BA%D0%BE%D0%BC%D0%B5%D0%BD%D0%B4%D0%BE%D0%B2%D0%B0%D0%BD%D0%BD%D1%8B%D1%85\x3A\x20")
        + QString::number(differing));
}

void FineTuneWidget::onRefreshClicked() { refreshTable(); }

void FineTuneWidget::onApplyClicked() {
    m_apply_btn_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString cat = m_category_->currentData().toString();

    std::thread([this, cat]() {
        RegistryTweakAccess access;
        TweakService svc(access, appdataDir().toStdString());
        const std::string result = svc.applyCategory(cat.toStdString());

        QMetaObject::invokeMethod(this, [this, result]() {
            QApplication::restoreOverrideCursor();
            m_apply_btn_->setEnabled(true);
            m_status_->setText(QString::fromStdString(result));
        }, Qt::QueuedConnection);
    }).detach();
}

void FineTuneWidget::onRollbackClicked() {
    m_rollback_btn_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::thread([this]() {
        RegistryTweakAccess access;
        TweakService svc(access, appdataDir().toStdString());
        const std::string result = svc.rollbackLast();

        QMetaObject::invokeMethod(this, [this, result]() {
            QApplication::restoreOverrideCursor();
            m_rollback_btn_->setEnabled(true);
            m_status_->setText(QString::fromStdString(result));
            refreshTable();
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace gno
