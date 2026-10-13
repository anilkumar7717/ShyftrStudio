#pragma once

#include "../captions/OverlayRenderer.h"
#include "ExportGraph.h"

#include <QObject>
#include <QThread>
#include <atomic>

namespace ss {

struct ExportRequest {
    Project project;          // snapshot (the user can keep editing during export)
    QString projectDir;
    ExportSettings settings;
    OverlayScene overlay;     // built on the GUI thread
    QString srtText;          // optional sidecars (already range-mapped)
    QString vttText;
    bool forceCompositor = false; // otherwise chosen automatically (Compositor::projectNeedsCompositor)
};

// Runs FFmpeg in a worker thread, streaming rendered overlay frames into its stdin.
// Output is written to "<name>.partial.<ext>" and renamed only on success, so a crash
// or cancel never leaves a half-written file with the final name.
class ExportJob : public QThread {
    Q_OBJECT
public:
    explicit ExportJob(ExportRequest req, QObject *parent = nullptr);
    void cancel() { m_cancel = true; }
    QString logPath() const { return m_logPath; }
    const ExportPlan &plan() const { return m_plan; }

signals:
    void progressChanged(double fraction, double fps, double etaSeconds, const QString &status);
    void succeeded(const QString &outputPath, double seconds, const QStringList &warnings);
    void failedWithReport(const QString &message, const QString &logPath);
    void cancelledByUser();

protected:
    void run() override;

private:
    ExportRequest m_req;
    ExportPlan m_plan;
    std::atomic<bool> m_cancel{false};
    QString m_logPath;
};

} // namespace ss
