#pragma once

#include "../core/Model.h"

#include <QWidget>

class QLineEdit;
class QListWidget;
class QComboBox;
class QPushButton;
class QButtonGroup;
class QBoxLayout;

namespace ss {

// Welcome / Projects workspace: new project, recent projects, crash recovery, open.
class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget *parent = nullptr);
    void refresh();

protected:
    void resizeEvent(QResizeEvent *e) override;

signals:
    void createRequested(const QString &name, const QString &parentDir, const ss::CanvasSettings &canvas);
    void openRequested(const QString &projectDir, bool recover);
    void openDialogRequested();
    void preferencesRequested();

private:
    QLineEdit *m_name;
    QLineEdit *m_location;
    QComboBox *m_resolution;
    QComboBox *m_fps;
    QButtonGroup *m_aspect;
    QListWidget *m_recent;
    QPushButton *m_open;
    QPushButton *m_recover;
    QBoxLayout *m_box = nullptr;
};

} // namespace ss
