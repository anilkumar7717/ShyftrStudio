#include "RelinkDialog.h"
#include "../core/ProjectStore.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ss {

RelinkDialog::RelinkDialog(const Project &p, QWidget *parent) : QDialog(parent), m_project(p)
{
    setWindowTitle("Relink media");
    resize(720, 380);
    auto *v = new QVBoxLayout(this);
    auto *intro = new QLabel("These files were moved or renamed. Point to the folder that contains them, or locate each "
                             "file. Matching uses the file name and size.");
    intro->setWordWrap(true);
    v->addWidget(intro);
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({"Media", "Last known location", "New location"});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    v->addWidget(m_table, 1);
    auto *h = new QHBoxLayout;
    auto *search = new QPushButton("Search folder…");
    auto *locate = new QPushButton("Locate selected…");
    h->addWidget(search);
    h->addWidget(locate);
    h->addStretch();
    v->addLayout(h);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Relink");
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(search, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, "Folder containing the media");
        if (dir.isEmpty())
            return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto found = ProjectStore::searchForMissing(m_project, dir);
        QApplication::restoreOverrideCursor();
        for (auto it = found.begin(); it != found.end(); ++it)
            m_found[it.key()] = it.value();
        refresh();
    });
    connect(locate, &QPushButton::clicked, this, [this]() {
        const int r = m_table->currentRow();
        if (r < 0)
            return;
        const QString id = m_table->item(r, 0)->data(Qt::UserRole).toString();
        const QString f = QFileDialog::getOpenFileName(this, "Locate " + m_table->item(r, 0)->text());
        if (!f.isEmpty()) {
            m_found[id] = f;
            refresh();
        }
    });
    refresh();
}

void RelinkDialog::refresh()
{
    m_table->setRowCount(0);
    for (const MediaAsset &m : m_project.media) {
        if (!m.missing)
            continue;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto *name = new QTableWidgetItem(m.name);
        name->setData(Qt::UserRole, m.id);
        m_table->setItem(r, 0, name);
        m_table->setItem(r, 1, new QTableWidgetItem(QFileInfo(m.path).absolutePath()));
        auto *nw = new QTableWidgetItem(m_found.value(m.id, "— not found —"));
        nw->setForeground(m_found.contains(m.id) ? QColor("#69DB7C") : QColor("#FFA94D"));
        m_table->setItem(r, 2, nw);
    }
    m_table->resizeColumnsToContents();
}

} // namespace ss
