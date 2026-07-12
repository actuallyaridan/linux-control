#include "MainWindow.h"
#include "CategoryWidget.h"
#include "IconHelper.h"

#include <QApplication>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QFrame>
#include <QIcon>
#include <QFont>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSizeGrip>
#include <QLayoutItem>
#include <QEvent>
#include <QMouseEvent>
#include <QVariantAnimation>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QMessageBox>
#include <AeroQt/navbuttons.h>
#include <AeroQt/insetwindow.h>
#include "Categories.h"
#include "pages/LinuxUpdatePage.h"
#include "pages/SystemPage.h"
#include "pages/InstalledUpdatesPage.h"
#include "pages/ProgramsFeaturesPage.h"
#include "pages/NetworkSharingPage.h"
#include "pages/FirewallPage.h"
#include "pages/PowerOptionsPage.h"
#include "pages/PerformancePage.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Title the main window explicitly rather than via applicationDisplayName,
    // which Qt would otherwise append to every dialog title ("Foo - Control
    // Panel"). With no display name set, dialogs show just their own title.
    setWindowTitle("Control Panel");
    setMinimumSize(700, 520);
    resize(1000, 650);

    m_navSound.setSource(QUrl::fromLocalFile(
        "/usr/share/sounds/Windows 7/og/Windows Navigation Start.wav"));
    m_navSound.setVolume(1.0f);

    // Hide menu bar (like real Windows 7 Control Panel)
    menuBar()->hide();

    // Crumb bar: Aero glass top inset
    buildCrumbBar();

    // Central container
    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scroll = new QScrollArea;
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet("QScrollArea { background: #FFFFFF; }");
    mainLayout->addWidget(m_scroll, 1);

    navigateHome();

    setCentralWidget(central);
    statusBar()->hide();

    // Aero glass on crumb bar: must be after setCentralWidget
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    Aero::makeInsetWindow(this, central, m_crumbBar, nullptr);

    // Intercept mouse side-button events from any child widget.
    qApp->installEventFilter(this);
}

void MainWindow::buildCrumbBar()
{
    m_crumbBar = new QWidget;
    m_crumbBar->setFixedHeight(34);
    auto *layout = new QHBoxLayout(m_crumbBar);
    layout->setContentsMargins(4, 3, 6, 3);
    layout->setSpacing(2);

    // Aero-style back/forward nav buttons
    auto *navBtns = new Aero::NavButtons(m_crumbBar);
    m_backBtn = navBtns->back();
    m_forwardBtn = navBtns->forward();
    m_backBtn->setEnabled(false);
    m_forwardBtn->setEnabled(false);
    QObject::connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::goBack);
    QObject::connect(m_forwardBtn, &QPushButton::clicked, this, &MainWindow::goForward);
    layout->addWidget(navBtns);
    layout->addSpacing(4);

    // Address path box: icon + crumb trail. Reuse AeroQt's glass-entry texture so
    // this box matches the search field. Scoped with #addressBox so child labels
    // are unaffected.
    auto *pathBox = new QWidget;
    pathBox->setObjectName("addressBox");
    pathBox->setStyleSheet(
        "#addressBox { border-image: url(:/AeroQt/transpbg/entry/normal.png) 4 4 4 4 repeat; border-width: 4px; }"
        "#addressBox:hover { border-image: url(:/AeroQt/transpbg/entry/hover.png) 4 4 4 4 repeat; }"
    );
    pathBox->setFixedHeight(24);
    m_pathLayout = new QHBoxLayout(pathBox);
    m_pathLayout->setContentsMargins(4, 0, 4, 0);
    m_pathLayout->setSpacing(4);

    layout->addWidget(pathBox, 1);
    layout->addSpacing(6);

    // Search box. The crumb bar already carries _Aero_transpbg=true (set by
    // makeInsetWindow), so AeroQt's stylesheet gives plain QLineEdits a
    // translucent glass background that brightens on hover. We must NOT set our
    // own border/background or it overrides that border-image. On focus we swap
    // to a solid white field (AeroQt has no focus-state glass image).
    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText("Search Control Panel");
    m_searchBox->setFixedWidth(190);
    m_searchBox->setFixedHeight(24);
    m_searchBox->setClearButtonEnabled(false);
    m_searchBox->addAction(themeIcon({"system-search"}), QLineEdit::TrailingPosition);

    // Italic only while the placeholder shows; upright once the user types.
    QObject::connect(m_searchBox, &QLineEdit::textChanged, m_searchBox,
        [this](const QString &text) {
            QFont f = m_searchBox->font();
            f.setItalic(text.isEmpty());
            m_searchBox->setFont(f);
        });
    QFont searchFont = m_searchBox->font();
    searchFont.setItalic(true);
    m_searchBox->setFont(searchFont);

    QObject::connect(qApp, &QApplication::focusChanged, m_searchBox,
        [this](QWidget *, QWidget *now) {
            if (now == m_searchBox) {
                // Solid field while typing
                m_searchBox->setStyleSheet(
                    "QLineEdit { border: 1px solid #7EB4EA; border-radius: 2px;"
                    " background: #FFFFFF; color: #000000; }"
                );
            } else {
                // Fall back to AeroQt's translucent glass entry
                m_searchBox->setStyleSheet(QString());
            }
        });

    layout->addWidget(m_searchBox);
}

void MainWindow::setCrumbTrail(const QStringList &trail)
{
    // Clear existing crumb widgets.
    while (QLayoutItem *item = m_pathLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addArrow = [this]() {
        auto *arrow = new QLabel("▶");
        arrow->setStyleSheet("color: #666666; font-size: 5pt;");
        m_pathLayout->addWidget(arrow);
    };

    auto *controlIcon = new QLabel;
    controlIcon->setPixmap(themeIcon({"preferences-system"}).pixmap(16, 16));
    m_pathLayout->addWidget(controlIcon);
    addArrow();

    // "Control Panel" is always a link back to the home view.
    m_homeCrumb = new QLabel("Control Panel");
    m_homeCrumb->setObjectName("crumbHome");
    m_homeCrumb->setCursor(Qt::PointingHandCursor);
    m_homeCrumb->setStyleSheet(
        "QLabel#crumbHome { color: #000000; }"
        "QLabel#crumbHome:hover { color: #003399; }"
    );
    m_homeCrumb->installEventFilter(this);
    m_pathLayout->addWidget(m_homeCrumb);

    // Intermediate segments are clickable links; the last segment is plain text.
    for (int i = 0; i < trail.size(); ++i) {
        addArrow();
        bool isLast = (i == trail.size() - 1);
        auto *label = new QLabel(trail[i]);
        if (isLast) {
            label->setStyleSheet("color: #000000;");
        } else {
            // Clickable intermediate crumb: navigates to that category page.
            label->setCursor(Qt::PointingHandCursor);
            label->setStyleSheet(
                "QLabel { color: #000000; }"
                "QLabel:hover { color: #003399; }"
            );
            label->installEventFilter(this);
            // Build the partial path up to this segment.
            m_crumbNavLinks.insert(label, trail.mid(0, i + 1).join('/'));
        }
        m_pathLayout->addWidget(label);
    }

    m_pathLayout->addStretch();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Mouse side buttons: XButton1 = back, XButton2 = forward.
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::XButton1) { goBack();    return true; }
        if (me->button() == Qt::XButton2) { goForward(); return true; }
    }

    // Qt's QLabel ignores `text-decoration` in style sheets, so underline-on-
    // hover has to be driven by toggling the font's underline flag directly.
    // Only applies to labels explicitly registered as interactive links; the
    // global qApp filter would otherwise underline every QLabel in the app.
    if (auto *label = qobject_cast<QLabel *>(watched)) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            if (label == m_homeCrumb || label == m_checkUpdatesLabel
                || m_navLinks.contains(watched)
                || m_subpageLinks.contains(watched)
                || m_commandLinks.contains(watched)
                || m_crumbNavLinks.contains(watched))
            {
                QFont font = label->font();
                font.setUnderline(event->type() == QEvent::Enter);
                label->setFont(font);
            }
        }
    }

    // "Control Panel" crumb: a press returns home. Press (not release) because a
    // plain QLabel doesn't accept the press, so the release can be routed away
    // and the click lost. Queued because navigating rebuilds the crumb trail,
    // deleting this very label while its event is still on the stack.
    if (watched == m_homeCrumb && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
            QMetaObject::invokeMethod(this, [this]() { navigateHome(); },
                                      Qt::QueuedConnection);
        return true;
    }

    // Intermediate crumb segments navigate on press for the same reason as the
    // home crumb above: a plain QLabel doesn't accept the press, so its parent
    // grabs the mouse and the *release* is delivered there instead of to the
    // label; handling release here loses the click intermittently. Queued
    // because navigating rebuilds the crumb trail, deleting this label while its
    // event is still on the stack.
    if (event->type() == QEvent::MouseButtonPress) {
        auto crumbIt = m_crumbNavLinks.constFind(watched);
        if (crumbIt != m_crumbNavLinks.constEnd()) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QString path = crumbIt.value();
                QMetaObject::invokeMethod(this,
                    [this, path]() { navigateTo(path); },
                    Qt::QueuedConnection);
            }
            return true;
        }
    }

    // "Check for updates" sidebar link on the Linux Update page.
    if (watched == m_checkUpdatesLabel &&
        event->type() == QEvent::MouseButtonRelease &&
        m_updatePage)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
            QMetaObject::invokeMethod(m_updatePage,
                &LinuxUpdatePage::checkForUpdates, Qt::QueuedConnection);
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // External-launch links (e.g. Desktop Gadgets -> KDE widget panels).
            auto cmdIt = m_commandLinks.constFind(watched);
            if (cmdIt != m_commandLinks.constEnd()) {
                QStringList cmd = cmdIt.value();
                const QString program = cmd.takeFirst();
                // If the target program isn't on PATH, tell the user it's not
                // installed rather than failing silently.
                if (QStandardPaths::findExecutable(program).isEmpty()) {
                    QMessageBox::warning(this, tr("Control Panel"),
                        tr("\"%1\" is not installed.").arg(program));
                } else {
                    QProcess::startDetached(program, cmd);
                }
                return true;
            }
            auto navIt = m_navLinks.constFind(watched);
            if (navIt != m_navLinks.constEnd()) {
                const QString path = navIt.value();
                QMetaObject::invokeMethod(this,
                    [this, path]() { navigateTo(path); },
                    Qt::QueuedConnection);
                return true;
            }
            auto subIt = m_subpageLinks.constFind(watched);
            if (subIt != m_subpageLinks.constEnd()) {
                const QString path = subIt.value();
                // An empty target means "Control Panel Home".
                auto go = [this, path]() {
                    if (path.isEmpty()) navigateHome();
                    else                navigateTo(path);
                };
                if (m_sidebarTextEffect) {
                    auto *anim = new QPropertyAnimation(m_sidebarTextEffect, "opacity");
                    anim->setStartValue(1.0);
                    anim->setEndValue(0.0);
                    anim->setDuration(300);
                    anim->setEasingCurve(QEasingCurve::InCubic);
                    QObject::connect(anim, &QPropertyAnimation::finished, this, go);
                    anim->start(QAbstractAnimation::DeleteWhenStopped);
                } else {
                    QMetaObject::invokeMethod(this, go, Qt::QueuedConnection);
                }
                return true;
            }
            // (Intermediate crumb links are handled on press, above.)
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Paths for the Linux Update status page, its in-window select-updates view,
// and the Programs and Features "Installed Updates" list.
static const QString kUpdatePath = QStringLiteral("System and Security/Linux Update");
static const QString kSelectPath =
    QStringLiteral("System and Security/Linux Update/Select updates to install");
static const QString kProgramsFeaturesPath =
    QStringLiteral("Programs/Programs and Features");
static const QString kInstalledUpdatesPath =
    QStringLiteral("Programs/Programs and Features/Installed Updates");
static const QString kNetworkSharingPath =
    QStringLiteral("Network and Internet/Network and Sharing Center");
static const QString kFirewallPath =
    QStringLiteral("System and Security/Linux Firewall");
static const QString kPowerOptionsPath =
    QStringLiteral("System and Security/Power Options");
static const QString kPerformancePath =
    QStringLiteral("System and Security/Performance Information and Tools");

// Desktop Gadgets links hook into KDE Plasma's real widget panels. The
// "Desktop Gadgets" heading and "Add gadgets to the desktop" link toggle
// Plasma's widget explorer (the "Add or Manage widgets" panel) over D-Bus;
// "Get more gadgets online" opens the Get-New-Widgets download dialog.
static const QStringList kWidgetExplorerCmd = {
    QStringLiteral("qdbus6"), QStringLiteral("org.kde.plasmashell"),
    QStringLiteral("/PlasmaShell"),
    QStringLiteral("org.kde.PlasmaShell.toggleWidgetExplorer")
};
static const QStringList kGetWidgetsCmd = {
    QStringLiteral("knewstuff-dialog6"),
    QStringLiteral("/usr/share/knsrcfiles/plasmoids.knsrc")
};

// "Device Manager" links launch the standalone devmgmt program.
static const QStringList kDeviceManagerCmd = {
    QStringLiteral("devmgmt")
};

// A sub-path (one containing '/') is routable only if showEntry knows how to
// render it. Category-level paths are validated separately via detailGroupsFor.
static bool isRoutableSubPath(const QString &path)
{
    return path == kUpdatePath || path == kSelectPath
        || path == kProgramsFeaturesPath
        || path == kInstalledUpdatesPath
        || path == kNetworkSharingPath
        || path == kFirewallPath
        || path == kPowerOptionsPath
        || path == kPerformancePath
        || path == QStringLiteral("System and Security/System");
}

void MainWindow::navigateHome()
{
    pushHistory(QString());
    showEntry(QString());
}

void MainWindow::navigateTo(const QString &path)
{
    if (path.contains('/')) {
        // Sub-path: if we can't render it (e.g. an intermediate crumb like
        // "Programs/Programs and Features" with no page of its own), fall back
        // to its parent category page rather than showing a blank pane.
        if (!isRoutableSubPath(path)) {
            const QString category = path.section('/', 0, 0);
            if (detailGroupsFor(category))
                navigateTo(category);
            return;
        }
    } else if (!detailGroupsFor(path)) {
        // Category-level path with no detail page: nothing to show.
        return;
    }
    m_navSound.play();
    pushHistory(path);
    showEntry(path);
}

// Render an entry without touching history. Empty string == home page.
void MainWindow::showEntry(const QString &entry)
{
    // Moving between the status view and the select-updates view reuses the
    // live page so the checked-update state survives; only the crumb trail and
    // the page's internal view switch. Recreating it would reset the checks.
    if ((entry == kUpdatePath || entry == kSelectPath)
        && m_updatePage && m_scroll->widget() == m_updatePage) {
        m_crumbNavLinks.clear();
        setCrumbTrail(entry.split('/'));
        if (entry == kSelectPath) m_updatePage->showSelectView();
        else                      m_updatePage->showStatusView();
        return;
    }

    // The page about to be replaced owns the old nav-link labels.
    m_navLinks.clear();
    m_subpageLinks.clear();
    m_commandLinks.clear();
    m_crumbNavLinks.clear();
    m_sidebarTextEffect  = nullptr;
    m_updatePage         = nullptr;
    m_checkUpdatesLabel  = nullptr;

    if (entry.isEmpty()) {
        setCrumbTrail({});
        m_scroll->setWidget(buildHomePage());
    } else if (entry.contains('/')) {
        // Sub-page path: "Category/SubPage[/SubView]"
        QStringList parts = entry.split('/');
        setCrumbTrail(parts);
        if (parts.size() >= 2 && parts[0] == "System and Security"
            && parts[1] == "Linux Update") {
            auto *sidebar = buildSubpageSidebar(
                LinuxUpdatePage::sidebarLinks(),
                LinuxUpdatePage::sidebarSeeAlso());
            auto *wuPage = new LinuxUpdatePage(sidebar);
            m_updatePage = wuPage;
            QObject::connect(wuPage, &LinuxUpdatePage::navigateHomeRequested,
                             this, &MainWindow::navigateHome, Qt::QueuedConnection);
            QObject::connect(wuPage, &LinuxUpdatePage::selectUpdatesRequested,
                             this, [this]() { navigateTo(kSelectPath); },
                             Qt::QueuedConnection);
            QObject::connect(wuPage, &LinuxUpdatePage::selectViewClosed,
                             this, &MainWindow::goBack, Qt::QueuedConnection);
            QObject::connect(wuPage, &QObject::destroyed, this,
                             [this]() { m_updatePage = nullptr; });
            m_scroll->setWidget(wuPage);
            if (entry == kSelectPath) m_updatePage->showSelectView();
        } else if (parts.size() == 2 && parts[0] == "System and Security"
                   && parts[1] == "System") {
            auto *sidebar = buildSubpageSidebar(
                SystemPage::sidebarLinks(),
                SystemPage::sidebarSeeAlso());
            auto *sysPage = new SystemPage(sidebar);
            QObject::connect(sysPage, &SystemPage::performanceRequested, this,
                             [this]() { navigateTo(kPerformancePath); },
                             Qt::QueuedConnection);
            m_scroll->setWidget(sysPage);
        } else if (entry == kPerformancePath) {
            auto *sidebar = buildSubpageSidebar(
                PerformancePage::sidebarLinks(),
                PerformancePage::sidebarSeeAlso());
            m_scroll->setWidget(new PerformancePage(sidebar));
        } else if (entry == kInstalledUpdatesPath) {
            auto *sidebar = buildSubpageSidebar(
                InstalledUpdatesPage::sidebarLinks(),
                InstalledUpdatesPage::sidebarSeeAlso());
            m_scroll->setWidget(new InstalledUpdatesPage(sidebar));
        } else if (entry == kProgramsFeaturesPath) {
            auto *sidebar = buildSubpageSidebar(
                ProgramsFeaturesPage::sidebarLinks(),
                ProgramsFeaturesPage::sidebarSeeAlso());
            m_scroll->setWidget(new ProgramsFeaturesPage(sidebar));
        } else if (entry == kNetworkSharingPath) {
            auto *sidebar = buildSubpageSidebar(
                NetworkSharingPage::sidebarLinks(),
                NetworkSharingPage::sidebarSeeAlso());
            m_scroll->setWidget(new NetworkSharingPage(sidebar));
        } else if (entry == kFirewallPath) {
            auto *sidebar = buildSubpageSidebar(
                FirewallPage::sidebarLinks(),
                FirewallPage::sidebarSeeAlso());
            m_scroll->setWidget(new FirewallPage(sidebar));
        } else if (entry == kPowerOptionsPath) {
            auto *sidebar = buildSubpageSidebar(
                PowerOptionsPage::sidebarLinks(),
                PowerOptionsPage::sidebarSeeAlso());
            m_scroll->setWidget(new PowerOptionsPage(sidebar));
        }
    } else {
        setCrumbTrail({ entry });
        m_scroll->setWidget(buildCategoryPage(entry));
    }
}

void MainWindow::pushHistory(const QString &entry)
{
    // Re-navigating to the current location shouldn't grow history.
    if (m_historyIndex >= 0 && m_history.at(m_historyIndex) == entry)
        return;
    // Drop any forward entries before branching to a new location.
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(entry);
    m_historyIndex = m_history.size() - 1;
    updateNavButtons();
}

void MainWindow::goBack()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    showEntry(m_history.at(m_historyIndex));
    updateNavButtons();
}

void MainWindow::goForward()
{
    if (m_historyIndex >= m_history.size() - 1)
        return;
    ++m_historyIndex;
    showEntry(m_history.at(m_historyIndex));
    updateNavButtons();
}

void MainWindow::updateNavButtons()
{
    if (m_backBtn)
        m_backBtn->setEnabled(m_historyIndex > 0);
    if (m_forwardBtn)
        m_forwardBtn->setEnabled(m_historyIndex < m_history.size() - 1);
}

QWidget *MainWindow::buildHomePage()
{
    // Outer: full-width white, centers the inner column
    auto *content = new QWidget;
    content->setStyleSheet("background: #FFFFFF;");
    auto *outerH = new QHBoxLayout(content);
    outerH->setContentsMargins(0, 0, 0, 0);
    outerH->setSpacing(0);

    // Inner column: sized to its content (the two-column grid), holds heading
    // row + grid. Centred in the white area like the real Control Panel.
    auto *inner = new QWidget;
    inner->setStyleSheet("background: transparent;");
    inner->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    outerH->addStretch(1);
    outerH->addWidget(inner, 0, Qt::AlignTop);
    outerH->addStretch(1);

    auto *innerV = new QVBoxLayout(inner);
    innerV->setContentsMargins(0, 15, 0, 10);
    innerV->setSpacing(0);

    // Heading row: "Adjust your computer's settings"  |  View by: Category
    auto *headRow = new QHBoxLayout;
    headRow->setContentsMargins(0, 0, 0, 22);

    auto *settingsLabel = new QLabel("Adjust your computer's settings");
    QFont f = settingsLabel->font();
    f.setPointSize(12);
    settingsLabel->setFont(f);
    settingsLabel->setStyleSheet("color: #003399;");
    headRow->addWidget(settingsLabel);
    headRow->addStretch();

    auto *viewByLabel = new QLabel("View by:");
    viewByLabel->setStyleSheet("color: #333333; font-size: 9pt;");
    headRow->addWidget(viewByLabel);

    auto *categoryBtn = new QToolButton;
    categoryBtn->setText("Category");
    categoryBtn->setPopupMode(QToolButton::MenuButtonPopup);
    categoryBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    categoryBtn->setArrowType(Qt::DownArrow);
    categoryBtn->setStyleSheet(
        "QToolButton { color: #003399; border: none; font-size: 9pt; padding-right: 14px; }"
        "QToolButton:hover { color: #FF6600; }"
    );
    auto *viewMenu = new QMenu(categoryBtn);
    viewMenu->addAction("Category");
    viewMenu->addAction("Large icons");
    viewMenu->addAction("Small icons");
    categoryBtn->setMenu(viewMenu);
    headRow->addWidget(categoryBtn);

    innerV->addLayout(headRow);

    // Two independent columns. The real Control Panel stacks each column as its
    // own list with even gaps; it does NOT align rows across the two columns, so
    // a short item never gets padded to match a tall item beside it.
    auto *columnsWidget = new QWidget;
    columnsWidget->setStyleSheet("background: transparent;");
    auto *columns = new QHBoxLayout(columnsWidget);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(28);

    auto *colLeft = new QVBoxLayout;
    colLeft->setContentsMargins(0, 0, 0, 0);
    colLeft->setSpacing(0);
    auto *colRight = new QVBoxLayout;
    colRight->setContentsMargins(0, 0, 0, 0);
    colRight->setSpacing(0);

    const QList<CategoryItem> &categories = homeCategories();
    for (int i = 0; i < categories.size(); ++i) {
        auto *w = new CategoryWidget(categories[i]);
        // Queued: the swap deletes this widget, so it must not run while the
        // widget's own click event is still on the call stack.
        QObject::connect(w, &CategoryWidget::titleActivated,
                         this, &MainWindow::navigateTo, Qt::QueuedConnection);
        // Task sub-links: known tasks deep-link to their dedicated page; any
        // other task falls back to its parent category page.
        QObject::connect(w, &CategoryWidget::taskActivated, this,
            [this](const QString &category, const QString &task) {
                static const QHash<QString, QString> knownTaskPaths = {
                    { "Uninstall a program", kProgramsFeaturesPath },
                };
                const auto it = knownTaskPaths.constFind(task);
                navigateTo(it != knownTaskPaths.constEnd() ? it.value()
                                                           : category);
            }, Qt::QueuedConnection);
        (i % 2 == 0 ? colLeft : colRight)->addWidget(w);
    }
    colLeft->addStretch(1);
    colRight->addStretch(1);

    columns->addLayout(colLeft);
    columns->addLayout(colRight);

    innerV->addWidget(columnsWidget);
    innerV->addStretch(1);

    return content;
}

MainWindow::Sidebar MainWindow::buildSidebarShell(int initialWidth)
{
    auto *clip = new QScrollArea;
    clip->setFixedWidth(initialWidth);
    clip->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clip->setFrameShape(QFrame::NoFrame);
    clip->setWidgetResizable(true);
    clip->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto *pane = new QFrame;
    pane->setObjectName("navPane");
    pane->setFixedWidth(168);
    pane->setStyleSheet(
        "#navPane { background: #F1F4F9; border-right: 1px solid #DCE0E8; }"
    );
    auto *outerV = new QVBoxLayout(pane);
    outerV->setContentsMargins(0, 0, 0, 0);
    outerV->setSpacing(0);

    // Text lives in a child widget so a fade effect touches only the text,
    // never the pane background.
    auto *textWrap = new QWidget;
    textWrap->setStyleSheet("background: transparent;");
    auto *navV = new QVBoxLayout(textWrap);
    navV->setContentsMargins(12, 14, 8, 10);
    navV->setSpacing(0);
    outerV->addWidget(textWrap);

    auto *controlHome = new QPushButton("Control Panel Home");
    controlHome->setCursor(Qt::PointingHandCursor);
    controlHome->setFlat(true);
    controlHome->setStyleSheet(
        "QPushButton { border: none; background: transparent; color: #000000;"
        " text-align: left; padding: 0; font-size: 9pt; }"
        "QPushButton:hover { color: #0033AA; }"
    );
    QObject::connect(controlHome, &QPushButton::clicked, this,
                     &MainWindow::navigateHome, Qt::QueuedConnection);
    navV->addWidget(controlHome);
    navV->addSpacing(16);

    clip->setWidget(pane);
    return { clip, textWrap, navV };
}

QScrollArea *MainWindow::buildNavSidebar(const QString &currentCategory)
{
    // Width 0: buildCategoryPage animates the clip open to 168px.
    Sidebar bar = buildSidebarShell(0);

    for (const QString &cat : navOrder()) {
        if (cat == currentCategory) {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 3, 0, 3);
            row->setSpacing(4);
            auto *bullet = new QLabel("•");
            bullet->setStyleSheet("color: #000000; font-size: 9pt; background: transparent;");
            row->addWidget(bullet, 0, Qt::AlignTop);
            auto *label = new QLabel(cat);
            QFont bf = label->font();
            bf.setPointSize(9);
            bf.setBold(true);
            label->setFont(bf);
            label->setWordWrap(true);
            label->setStyleSheet("color: #000000; background: transparent;");
            row->addWidget(label, 1);
            bar.navV->addLayout(row);
        } else {
            auto *link = new QLabel(cat);
            QFont lf = link->font();
            lf.setPointSize(9);
            link->setFont(lf);
            link->setWordWrap(true);
            link->setCursor(Qt::PointingHandCursor);
            link->setContentsMargins(12, 3, 0, 3);
            link->setStyleSheet(
                "QLabel { color: #000000; background: transparent; }"
                "QLabel:hover { color: #0033AA; }"
            );
            link->installEventFilter(this);
            m_navLinks.insert(link, cat);
            bar.navV->addWidget(link);
        }
    }
    bar.navV->addStretch(1);

    // Static sidebar (shown at full opacity). The effect is kept so that
    // navigating into a subpage can fade this text out first.
    m_sidebarTextEffect = new QGraphicsOpacityEffect(bar.textWrap);
    bar.textWrap->setGraphicsEffect(m_sidebarTextEffect);
    m_sidebarTextEffect->setOpacity(1.0);

    return bar.clip;
}

QScrollArea *MainWindow::buildSubpageSidebar(const QStringList &links,
                                              const QStringList &seeAlso)
{
    Sidebar bar = buildSidebarShell(168);

    auto addLink = [&](const QString &text) {
        auto *l = new QLabel(text);
        QFont f = l->font();
        f.setPointSize(9);
        l->setFont(f);
        l->setWordWrap(true);
        l->setCursor(Qt::PointingHandCursor);
        l->setContentsMargins(12, 3, 0, 3);
        l->setStyleSheet(
            "QLabel { color: #000000; background: transparent; }"
            "QLabel:hover { color: #0033AA; }"
        );
        l->installEventFilter(this);
        if (text == "Check for updates")
            m_checkUpdatesLabel = l;
        // Cross-navigation links shared by the subpage sidebars. "Control Panel
        // Home" maps to the empty path, which the click handler routes home.
        static const QHash<QString, QString> knownLinks = {
            { "Control Panel Home",          QString() },
            { "View installed updates",      kInstalledUpdatesPath },
            { "Uninstall a program",         kProgramsFeaturesPath },
            { "Linux Firewall",              kFirewallPath },
            { "Network and Sharing Center",  kNetworkSharingPath },
            { "Performance Information and Tools", kPerformancePath },
        };
        if (knownLinks.contains(text))
            m_subpageLinks.insert(l, knownLinks.value(text));
        else if (text == "Device Manager")
            m_commandLinks.insert(l, kDeviceManagerCmd);
        bar.navV->addWidget(l);
    };

    for (const QString &text : links)
        addLink(text);

    bar.navV->addStretch(1);

    if (!seeAlso.isEmpty()) {
        auto *sep = new QFrame;
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #B8C4D8;");
        bar.navV->addWidget(sep);
        bar.navV->addSpacing(4);

        auto *seeAlsoLabel = new QLabel("See also");
        QFont f = seeAlsoLabel->font();
        f.setPointSize(8);
        seeAlsoLabel->setFont(f);
        seeAlsoLabel->setStyleSheet("color: #666666; background: transparent;");
        bar.navV->addWidget(seeAlsoLabel);

        for (const QString &text : seeAlso)
            addLink(text);
    }

    // Subpage sidebars are full width from the start and fade their text in.
    auto *fadeEffect = new QGraphicsOpacityEffect(bar.textWrap);
    bar.textWrap->setGraphicsEffect(fadeEffect);
    fadeEffect->setOpacity(0.0);
    auto *fadeAnim = new QPropertyAnimation(fadeEffect, "opacity", bar.clip);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setDuration(2000);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, bar.clip, [fadeAnim]() { fadeAnim->start(); });

    return bar.clip;
}

QWidget *MainWindow::buildCategoryPage(const QString &currentCategory)
{
    auto *page = new QWidget;
    page->setStyleSheet("background: #FFFFFF;");
    auto *root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *sidebarClip = buildNavSidebar(currentCategory);
    root->addWidget(sidebarClip);

    // ---- Right content pane ----------------------------------------------
    auto *contentWrap = new QWidget;
    contentWrap->setStyleSheet("background: #FFFFFF;");
    auto *contentV = new QVBoxLayout(contentWrap);
    contentV->setContentsMargins(18, 14, 18, 14);
    contentV->setSpacing(0);

    auto makeSep = []() {
        auto *s = new QLabel("|");
        QFont f = s->font();
        f.setPointSize(9);
        s->setFont(f);
        s->setStyleSheet("color: #B0B0B0;");
        return s;
    };
    auto makeTask = [this](const QString &text) {
        auto *l = new QLabel(text);
        QFont f = l->font();
        f.setPointSize(9);
        l->setFont(f);
        l->setCursor(Qt::PointingHandCursor);
        l->setStyleSheet(
            "QLabel { color: #1F4E99; }"
            "QLabel:hover { color: #0033AA; }"
        );
        l->installEventFilter(this);
        // Known task links navigate to their page (others are decorative).
        if (text == "View installed updates")
            m_subpageLinks.insert(l, kInstalledUpdatesPath);
        else if (text == "Check firewall status"
                 || text == "Allow a program through Linux Firewall")
            m_subpageLinks.insert(l, kFirewallPath);
        else if (text == "View network status and tasks")
            m_subpageLinks.insert(l, kNetworkSharingPath);
        else if (text == "Choose a power plan")
            m_subpageLinks.insert(l, kPowerOptionsPath);
        else if (text == "Uninstall a program")
            m_subpageLinks.insert(l, kProgramsFeaturesPath);
        else if (text == "Add gadgets to the desktop")
            m_commandLinks.insert(l, kWidgetExplorerCmd);
        else if (text == "Get more gadgets online")
            m_commandLinks.insert(l, kGetWidgetsCmd);
        else if (text == "Device Manager")
            m_commandLinks.insert(l, kDeviceManagerCmd);
        return l;
    };

    for (const DetailGroup &group : *detailGroupsFor(currentCategory)) {
        auto *groupRow = new QHBoxLayout;
        groupRow->setContentsMargins(0, 0, 0, 12);
        groupRow->setSpacing(12);

        auto *iconLabel = new QLabel;
        iconLabel->setPixmap(resolveIcon(group.iconName).pixmap(32, 32));
        iconLabel->setFixedSize(32, 32);
        groupRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto *textBlock = new QVBoxLayout;
        textBlock->setContentsMargins(0, 0, 0, 0);
        textBlock->setSpacing(2);

        auto *title = new QLabel(group.title);
        QFont tf = title->font();
        tf.setPointSize(11);
        title->setFont(tf);
        title->setCursor(Qt::PointingHandCursor);
        title->setStyleSheet(
            "QLabel { color: #1E7B1E; }"
            "QLabel:hover { color: #145214; }"
        );
        title->installEventFilter(this);

        // Known subpages: register the title label so clicking navigates there.
        static const QHash<QString, QString> knownSubpages = {
            { "Linux Update",              "System and Security/Linux Update" },
            { "Linux Firewall",            "System and Security/Linux Firewall" },
            { "System",                    "System and Security/System" },
            { "Programs and Features",     "Programs/Programs and Features" },
            { "Power Options",             "System and Security/Power Options" },
            { "Network and Sharing Center",
              "Network and Internet/Network and Sharing Center" },
        };
        if (knownSubpages.contains(group.title))
            m_subpageLinks.insert(title, knownSubpages.value(group.title));
        else if (group.title == "Desktop Gadgets")
            m_commandLinks.insert(title, kWidgetExplorerCmd);

        textBlock->addWidget(title);

        for (const QStringList &line : group.lines) {
            auto *lineRow = new QHBoxLayout;
            lineRow->setContentsMargins(0, 0, 0, 0);
            lineRow->setSpacing(6);
            for (int i = 0; i < line.size(); ++i) {
                if (i > 0)
                    lineRow->addWidget(makeSep());
                lineRow->addWidget(makeTask(line[i]));
            }
            lineRow->addStretch(1);
            textBlock->addLayout(lineRow);
        }

        groupRow->addLayout(textBlock, 1);
        contentV->addLayout(groupRow);
    }

    contentV->addStretch(1);
    root->addWidget(contentWrap, 1);

    // sidebarClip slides from 0 to 168; sidebar inside stays full-width so text
    // never reflows. The content pane rides rightward naturally as the clip grows.
    auto *sidebarAnim = new QVariantAnimation(page);
    sidebarAnim->setStartValue(0);
    sidebarAnim->setEndValue(168);
    sidebarAnim->setDuration(250);
    sidebarAnim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(sidebarAnim, &QVariantAnimation::valueChanged, sidebarClip,
        [sidebarClip](const QVariant &v) { sidebarClip->setFixedWidth(v.toInt()); });

    // Content fades in over the same span.
    auto *fadeEffect = new QGraphicsOpacityEffect(contentWrap);
    contentWrap->setGraphicsEffect(fadeEffect);
    fadeEffect->setOpacity(0.0);
    auto *contentAnim = new QPropertyAnimation(fadeEffect, "opacity", page);
    contentAnim->setStartValue(0.0);
    contentAnim->setEndValue(1.0);
    contentAnim->setDuration(260);
    contentAnim->setEasingCurve(QEasingCurve::OutCubic);

    QTimer::singleShot(0, page, [sidebarAnim, contentAnim]() {
        sidebarAnim->start();
        contentAnim->start();
    });

    return page;
}

