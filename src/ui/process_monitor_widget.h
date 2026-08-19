#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

namespace gno {

class ProcessMonitor;

class ProcessMonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProcessMonitorWidget(QWidget* parent = nullptr);

private slots:
    void refreshProcesses();
    void onKillClicked(uint32_t pid, const QString& name);
    void onBlockClicked(const QString& name);

private:
    void setupUI();

    ProcessMonitor* m_processMonitor;
    QWidget* m_processList;
    QTimer* m_refreshTimer;
};

} // namespace gno
