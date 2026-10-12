#include "WelcomePage.h"
#include "../core/ProjectStore.h"
#include "../core/Settings.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace ss {

WelcomePage::WelcomePage(QWidget *parent) : QWidget(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rootLayout->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *outer = new QBoxLayout(QBoxLayout::LeftToRight, content);
    m_box = outer;
    outer->setContentsMargins(40, 32, 40, 24);
    outer->setSpacing(40);

    // ---------------- left: new project
    auto *left = new QVBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(Theme::appIcon().pixmap(64, 64));
    left->addWidget(logo);
    auto *title = new QLabel("Shyftr Studio");
    title->setStyleSheet("font-size: 26pt; font-weight: 800;");
    left->addWidget(title);
    auto *tag = new QLabel("Import a recording, choose a caption style, and export a professional result — "
                           "without sending a single frame to the cloud.");
    tag->setWordWrap(true);
    tag->setObjectName("hint");
    left->addWidget(tag);
    left->addSpacing(16);
    auto *newLbl = new QLabel("New project");
    newLbl->setObjectName("heading");
    left->addWidget(newLbl);
    auto *f = new QFormLayout;
    m_name = new QLineEdit("Untitled project");
    f->addRow("Name", m_name);
    auto *aspectRow = new QHBoxLayout;
    m_aspect = new QButtonGroup(this);
    int id = 0;
    for (const QString &a : QStringList{"16:9", "9:16", "1:1", "4:5"}) {
        auto *b = new QToolButton;
        b->setText(a);
        b->setCheckable(true);
        b->setMinimumWidth(56);
        b->setToolTip(a == "9:16" ? "Shorts, Reels, TikTok" : a == "1:1" ? "Square feed posts" : a == "4:5" ? "Portrait feed posts"
                                                                                                             : "YouTube, landscape");
        m_aspect->addButton(b, id++);
        aspectRow->addWidget(b);
    }
    m_aspect->button(0)->setChecked(true);
    aspectRow->addStretch();
    f->addRow("Format", aspectRow);
    m_resolution = new QComboBox;
    m_resolution->addItem("1080p (Full HD)", 1080);
    m_resolution->addItem("2160p (4K)", 2160);
    m_resolution->addItem("720p", 720);
    f->addRow("Resolution", m_resolution);
    m_fps = new QComboBox;
    for (const auto &[l, r] : QVector<QPair<QString, QString>>{{"30", "30/1"}, {"25", "25/1"}, {"24", "24/1"}, {"29.97", "30000/1001"},
                                                                {"60", "60/1"}, {"50", "50/1"}, {"23.976", "24000/1001"}})
        m_fps->addItem(l + " fps", r);
    f->addRow("Frame rate", m_fps);
    auto *locRow = new QHBoxLayout;
    m_location = new QLineEdit(AppPaths::projectsDir());
    auto *browse = new QPushButton("…");
    locRow->addWidget(m_location, 1);
    locRow->addWidget(browse);
    f->addRow("Location", locRow);
    left->addLayout(f);
    auto *create = new QPushButton("Create project");
    create->setObjectName("primary");
    left->addWidget(create);
    left->addSpacing(12);
    auto *openOther = new QPushButton("Open project or package…");
    left->addWidget(openOther);
    left->addStretch(1);
    auto *prefs = new QPushButton("Preferences…");
    m_location->setMinimumWidth(120);
    left->addWidget(prefs);
    outer->addLayout(left, 2);

    // ---------------- right: recent
    auto *right = new QVBoxLayout;
    auto *recLbl = new QLabel("Recent projects");
    recLbl->setObjectName("heading");
    right->addWidget(recLbl);
    m_recent = new QListWidget;
    m_recent->setAccessibleName("Recent projects");
    m_recent->setSpacing(2);
    m_recent->setMinimumHeight(180);
    m_recent->setWordWrap(true);
    right->addWidget(m_recent, 1);
    auto *btns = new QHBoxLayout;
    m_open = new QPushButton("Open");
    m_recover = new QPushButton("Recover unsaved changes");
    auto *remove = new QPushButton("Remove from list");
    btns->addWidget(m_open);
    btns->addWidget(m_recover);
    btns->addStretch();
    btns->addWidget(remove);
    right->addLayout(btns);
    btns->setSpacing(6);
    auto *footer = new QLabel("Offline · No account · No telemetry · Projects are plain folders with a project.json");
    footer->setObjectName("hint");
    footer->setWordWrap(true);
    right->addWidget(footer);
    outer->addLayout(right, 3);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, "Project location", m_location->text());
        if (!d.isEmpty())
            m_location->setText(d);
    });
    connect(create, &QPushButton::clicked, this, [this]() {
        const int res = m_resolution->currentData().toInt();
        CanvasSettings c;
        const int a = m_aspect->checkedId();
        const int longSide = res * 16 / 9;
        if (a == 0) {
            c.width = longSide;
            c.height = res;
        } else if (a == 1) {
            c.width = res;
            c.height = longSide;
        } else if (a == 2) {
            c.width = res;
            c.height = res;
        } else {
            c.width = res;
            c.height = res * 5 / 4;
        }
        c.width &= ~1;
        c.height &= ~1;
        c.fps = Rational::fromString(m_fps->currentData().toString());
        emit createRequested(m_name->text().trimmed(), m_location->text().trimmed(), c);
    });
    connect(openOther, &QPushButton::clicked, this, &WelcomePage::openDialogRequested);
    connect(prefs, &QPushButton::clicked, this, &WelcomePage::preferencesRequested);
    connect(m_open, &QPushButton::clicked, this, [this]() {
        if (auto *it = m_recent->currentItem())
            emit openRequested(it->data(Qt::UserRole).toString(), false);
    });
    connect(m_recover, &QPushButton::clicked, this, [this]() {
        if (auto *it = m_recent->currentItem())
            emit openRequested(it->data(Qt::UserRole).toString(), true);
    });
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        emit openRequested(it->data(Qt::UserRole).toString(), false);
    });
    connect(m_recent, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
        m_open->setEnabled(it && it->data(Qt::UserRole + 1).toBool());
        m_recover->setEnabled(it && it->data(Qt::UserRole + 2).toBool());
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (auto *it = m_recent->currentItem()) {
            RecentProjects::remove(it->data(Qt::UserRole).toString());
            refresh();
        }
    });
    refresh();
}

void WelcomePage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (!m_box)
        return;
    const bool narrow = width() < 980;
    m_box->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const int m = width() < 1200 ? 20 : 40;
    m_box->setContentsMargins(m, m * 3 / 4, m, m / 2);
    m_box->setSpacing(narrow ? 20 : 40);
}

void WelcomePage::refresh()
{
    m_recent->clear();
    for (const QString &dir : RecentProjects::list()) {
        const bool exists = QFileInfo::exists(dir + "/project.json");
        const RecoveryInfo r = exists ? ProjectStore::recoveryInfo(dir) : RecoveryInfo();
        const bool recoverable = r.uncleanShutdown && r.autosaveIsNewer();
        QString text = QFileInfo(dir).fileName();
        QString sub = QDir::toNativeSeparators(dir);
        if (!exists)
            sub = "Not found: " + sub;
        else
            sub += " · " + QFileInfo(dir + "/project.json").lastModified().toString("d MMM yyyy, HH:mm");
        if (recoverable)
            sub += "\n⚠ Closed unexpectedly — unsaved changes from " + r.autosaveTime.toString("HH:mm") + " can be recovered";
        auto *it = new QListWidgetItem(text + "\n" + sub);
        it->setData(Qt::UserRole, dir);
        it->setData(Qt::UserRole + 1, exists);
        it->setData(Qt::UserRole + 2, recoverable);
        if (!exists)
            it->setForeground(Theme::textDim());
        else if (recoverable)
            it->setForeground(QColor("#FFA94D"));
        m_recent->addItem(it);
    }
    if (m_recent->count() == 0) {
        auto *it = new QListWidgetItem("No recent projects yet. Create one on the left.");
        it->setFlags(Qt::NoItemFlags);
        m_recent->addItem(it);
    }
    m_open->setEnabled(false);
    m_recover->setEnabled(false);
}

} // namespace ss
