#pragma once

#include "../core/Model.h"

#include <QDialog>
#include <QHash>

class QTableWidget;

namespace ss {

// Lists offline media and lets the user search a folder or locate each file.
class RelinkDialog : public QDialog {
    Q_OBJECT
public:
    RelinkDialog(const Project &p, QWidget *parent = nullptr);
    QHash<QString, QString> result() const { return m_found; }

private:
    void refresh();
    Project m_project;
    QHash<QString, QString> m_found;
    QTableWidget *m_table;
};

} // namespace ss
