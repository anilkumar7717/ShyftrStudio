#include "../core/Settings.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileOpenEvent>
#include <QMessageBox>
#include <QTimer>

namespace {
// Handles project folders / project.json opened from Finder on macOS.
class StudioApplication : public QApplication {
public:
    using QApplication::QApplication;
    ss::MainWindow *window = nullptr;
    QString pending;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString f = static_cast<QFileOpenEvent *>(e)->file();
            if (window)
                window->openProject(f);
            else
                pending = f;
            return true;
        }
        return QApplication::event(e);
    }
};
} // namespace

int main(int argc, char **argv)
{
    QApplication::setOrganizationName("Shyftr");
    QApplication::setOrganizationDomain("shyftr.studio");
    QApplication::setApplicationName("Shyftr Studio");
    QApplication::setApplicationVersion("0.1.0");
    StudioApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("Offline desktop video editor with local AI subtitles");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", "Project folder or project.json to open");
    // Developer aids for automated UI smoke tests / documentation screenshots.
    QCommandLineOption shotOpt("screenshot", "Save a screenshot of the main window to <file> and quit.", "file");
    QCommandLineOption delayOpt("screenshot-delay", "Delay before the screenshot in ms (default 2500).", "ms", "2500");
    QCommandLineOption modeOpt("mode", "Start in 'quick' or 'advanced' mode.", "mode");
    QCommandLineOption tabOpt("subtitle-tab", "Show the AI Subtitles panel tab (0-2).", "index");
    parser.addOption(shotOpt);
    parser.addOption(delayOpt);
    parser.addOption(modeOpt);
    parser.addOption(tabOpt);
    parser.process(app);

    QString err;
    if (!ss::AppPaths::ensureLayout(&err)) {
        QMessageBox::critical(nullptr, "Shyftr Studio", "The workspace folder could not be created:\n" + err);
        return 1;
    }
    ss::Settings::instance().load();
    ss::Theme::apply(app, ss::Settings::instance().highContrast, ss::Settings::instance().uiScale);
    app.setWindowIcon(ss::Theme::appIcon());

    ss::MainWindow w;
    app.window = &w;
    w.show();
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        w.openProject(args.first());
    else if (!app.pending.isEmpty())
        w.openProject(app.pending);
    if (parser.isSet(modeOpt) || parser.isSet(tabOpt))
        QTimer::singleShot(600, &w, [&w, &parser, modeOpt, tabOpt]() {
            if (parser.isSet(modeOpt))
                w.setMode(parser.value(modeOpt) == "quick");
            if (parser.isSet(tabOpt))
                w.showSubtitleTab(parser.value(tabOpt).toInt());
        });
    if (parser.isSet(shotOpt)) {
        const QString file = parser.value(shotOpt);
        QTimer::singleShot(parser.value(delayOpt).toInt(), &w, [&w, file]() {
            w.grab().save(file);
            QApplication::exit(0);
        });
    }
    return app.exec();
}
