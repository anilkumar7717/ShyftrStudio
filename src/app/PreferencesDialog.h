#pragma once

#include <QDialog>

class QTabWidget;
class QTableWidget;
class QLabel;

namespace ss {

// Preferences: general, tools, AI model packs, performance/GPU, shortcuts,
// privacy & storage, accessibility. Everything is stored in User_Preferences/.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    void showModelsTab();
signals:
    void settingsChanged();
    void modelsChanged();

private:
    QWidget *generalTab();
    QWidget *toolsTab();
    QWidget *modelsTab();
    QWidget *performanceTab();
    QWidget *shortcutsTab();
    QWidget *privacyTab();
    QWidget *accessibilityTab();
    void refreshModels();
    void refreshShortcuts();

    QTabWidget *m_tabs;
    QTableWidget *m_models = nullptr;
    QLabel *m_modelInfo = nullptr;
    QTableWidget *m_shortcuts = nullptr;
    int m_modelsIndex = 0;
};

} // namespace ss
