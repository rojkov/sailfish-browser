/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Raine Makelainen <raine.makelainen@jollamobile.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "declarativewebcontainer.h"
#include "persistenttabmodel.h"
#include "privatetabmodel.h"
#include "declarativewebpage.h"
#include "dbmanager.h"
#include "downloadmanager.h"
#include "declarativewebutils.h"

#include <QPointer>
#include <QTimerEvent>
#include <QQuickWindow>
#include <QDir>
#include <QTransform>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <QGuiApplication>
#include <QScreen>
#include <QMetaMethod>

#include <QOpenGLFunctions_ES2>

#include <qmozcontext.h>
#include <QGuiApplication>

#include <qpa/qplatformnativeinterface.h>


#ifndef DEBUG_LOGS
#define DEBUG_LOGS 0
#endif

DeclarativeWebContainer::DeclarativeWebContainer(QWindow *parent)
    : QWindow(parent)
    , m_rotationHandler(0)
    , m_webPage(0)
    , m_context(0)
    , m_model(0)
    , m_webPageComponent(0)
    , m_settingManager(SettingManager::instance())
    , m_enabled(true)
    , m_foreground(true)
    , m_allowHiding(true)
    , m_popupActive(false)
    , m_portrait(true)
    , m_fullScreenMode(false)
    , m_fullScreenHeight(0.0)
    , m_imOpened(false)
    , m_inputPanelOpenHeight(0.0)
    , m_toolbarHeight(0.0)
    , m_tabId(0)
    , m_loading(false)
    , m_loadProgress(0)
    , m_completed(false)
    , m_initialized(false)
    , m_privateMode(m_settingManager->autostartPrivateBrowsing())
    , m_hasBeenExposed(false)
{

    QSize screenSize = QGuiApplication::primaryScreen()->size();
    resize(screenSize.width(), screenSize.height());;
    setSurfaceType(QWindow::OpenGLSurface);

    QSurfaceFormat format(requestedFormat());
    format.setAlphaBufferSize(0);
    setFormat(format);

    create();
    setObjectName("WebView");

    if (QPlatformWindow *windowHandle = handle()) {
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        native->setWindowProperty(windowHandle, QStringLiteral("BACKGROUND_VISIBLE"), false);
    }

    QMozContext::GetInstance()->setPixelRatio(2.0);

    m_webPages = new WebPages(this);
    m_persistentTabModel = new PersistentTabModel(this);
    m_privateTabModel = new PrivateTabModel(this);

    setTabModel(privateMode() ? m_privateTabModel.data() : m_persistentTabModel.data());

    connect(DownloadManager::instance(), SIGNAL(initializedChanged()), this, SLOT(initialize()));
    connect(DownloadManager::instance(), SIGNAL(downloadStarted()), this, SLOT(onDownloadStarted()));
    connect(QMozContext::GetInstance(), SIGNAL(onInitialized()), this, SLOT(initialize()));
    connect(this, SIGNAL(portraitChanged()), this, SLOT(resetHeight()));

    QString cacheLocation = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir dir(cacheLocation);
    if(!dir.exists() && !dir.mkpath(cacheLocation)) {
        qWarning() << "Can't create directory "+ cacheLocation;
        return;
    }

    connect(this, SIGNAL(foregroundChanged()), this, SLOT(updateWindowFlags()));

    qApp->installEventFilter(this);

    showFullScreen();
    connect(this, SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(updateWindowState(Qt::WindowState)));
}

DeclarativeWebContainer::~DeclarativeWebContainer()
{
    // Disconnect all signal slot connections
    if (m_webPage) {
        disconnect(m_webPage, 0, 0, 0);
    }
}

DeclarativeWebPage *DeclarativeWebContainer::webPage() const
{
    return m_webPage;
}

void DeclarativeWebContainer::setWebPage(DeclarativeWebPage *webPage)
{
    if (m_webPage != webPage) {
        // Disconnect previous page.
        if (m_webPage) {
            m_webPage->disconnect(this);
        }

        m_webPage = webPage;

        if (m_webPage) {
            m_webPage->setWindow(this);
            if (m_chromeWindow) {
                updateContentOrientation(m_chromeWindow->contentOrientation());
            }
            m_tabId = m_webPage->tabId();
        } else {
            m_tabId = 0;
        }

        emit contentItemChanged();
        emit tabIdChanged();
        emit loadingChanged();
        emit focusObjectChanged(m_webPage);

        setLoadProgress(m_webPage ? m_webPage->loadProgress() : 0);
    }
}

DeclarativeTabModel *DeclarativeWebContainer::tabModel() const
{
    return m_model;
}

void DeclarativeWebContainer::setTabModel(DeclarativeTabModel *model)
{
    if (m_model != model) {
        int oldCount = 0;
        if (m_model) {
            disconnect(m_model, 0, 0, 0);
            oldCount = m_model->count();
        }

        m_model = model;
        int newCount = 0;
        if (m_model) {
            connect(m_model, SIGNAL(activeTabChanged(int,int,bool)), this, SLOT(onActiveTabChanged(int,int,bool)));
            connect(m_model, SIGNAL(loadedChanged()), this, SLOT(initialize()));
            connect(m_model, SIGNAL(tabClosed(int)), this, SLOT(releasePage(int)));
            connect(m_model, SIGNAL(newTabRequested(QString,QString,int)), this, SLOT(onNewTabRequested(QString,QString,int)));
            newCount = m_model->count();
        }
        emit tabModelChanged();
        if (oldCount != newCount) {
            emit m_model->countChanged();
        }
    }
}

bool DeclarativeWebContainer::completed() const
{
    return m_completed;
}

bool DeclarativeWebContainer::foreground() const
{
    return m_foreground;
}

void DeclarativeWebContainer::setForeground(bool active)
{
    if (m_foreground != active) {
        m_foreground = active;

        if (!m_foreground) {
            // Respect content height when browser brought back from home
            resetHeight(true);
        }
        emit foregroundChanged();
    }
}

int DeclarativeWebContainer::maxLiveTabCount() const
{
    return m_webPages->maxLivePages();
}

void DeclarativeWebContainer::setMaxLiveTabCount(int count)
{
    if (m_webPages->setMaxLivePages(count)) {
        emit maxLiveTabCountChanged();
    }
}

bool DeclarativeWebContainer::privateMode() const
{
    return m_privateMode;
}

void DeclarativeWebContainer::setPrivateMode(bool privateMode)
{
    if (m_privateMode != privateMode) {
        m_privateMode = privateMode;
        m_settingManager->setAutostartPrivateBrowsing(privateMode);
        updateMode();
        emit privateModeChanged();
    }
}

bool DeclarativeWebContainer::loading() const
{
    if (m_webPage) {
        return m_webPage->loading();
    } else {
        return m_model ? m_model->count() : false;
    }
}

int DeclarativeWebContainer::loadProgress() const
{
    return m_loadProgress;
}

void DeclarativeWebContainer::setLoadProgress(int loadProgress)
{
    if (m_loadProgress != loadProgress) {
        m_loadProgress = loadProgress;
        emit loadProgressChanged();
    }
}

bool DeclarativeWebContainer::imOpened() const
{
    return m_imOpened;
}

QObject *DeclarativeWebContainer::chromeWindow() const
{
    return m_chromeWindow;
}

void DeclarativeWebContainer::setChromeWindow(QObject *chromeWindow)
{
    QQuickView *quickView = qobject_cast<QQuickView*>(chromeWindow);
    if (quickView && (quickView != m_chromeWindow)) {
        m_chromeWindow = quickView;
        if (m_chromeWindow) {
            m_chromeWindow->setTransientParent(this);
            m_chromeWindow->showFullScreen();
            updateContentOrientation(m_chromeWindow->contentOrientation());
            connect(m_chromeWindow, SIGNAL(contentOrientationChanged(Qt::ScreenOrientation)), this, SLOT(updateContentOrientation(Qt::ScreenOrientation)));
        }
        emit chromeWindowChanged();
    }
}

bool DeclarativeWebContainer::canGoForward() const
{
    return m_webPage ? m_webPage->canGoForward() : false;
}

bool DeclarativeWebContainer::canGoBack() const
{
    return m_webPage ? m_webPage->canGoBack() : false;
}

int DeclarativeWebContainer::tabId() const
{
    return m_tabId;
}

QString DeclarativeWebContainer::url() const
{
    return m_webPage ? m_webPage->url().toString() : QString();
}

QString DeclarativeWebContainer::title() const
{
    return m_webPage ? m_webPage->title() : QString();
}

bool DeclarativeWebContainer::isActiveTab(int tabId)
{
    return m_webPage && m_webPage->tabId() == tabId;
}

void DeclarativeWebContainer::load(QString url, QString title, bool force)
{
    if (url.isEmpty()) {
        url = "about:blank";
    }

    if (m_webPage && m_webPage->completed()) {
        m_webPage->loadTab(url, force);
    } else if (!canInitialize()) {
        m_initialUrl = url;
    } else if (m_model && m_model->count() == 0) {
        // Browser running all tabs are closed.
        m_model->newTab(url, title);
    }
}

/**
 * @brief DeclarativeWebContainer::reload
 * Reloads the active tab. If not tabs exist this does nothing. If the page was
 * virtualized it will be resurrected.
 */
void DeclarativeWebContainer::reload(bool force)
{
    if (m_tabId > 0) {
        if (force && m_webPage && m_webPage->completed() && m_webPage->tabId() == m_tabId) {
            // Reload live active tab directly.
            m_webPage->reload();
        } else {
            loadTab(m_model->activeTab(), force);
        }
    }
}

void DeclarativeWebContainer::goForward()
{
    if (m_webPage &&  m_webPage->canGoForward()) {
        DBManager::instance()->goForward(m_webPage->tabId());
        m_webPage->goForward();
    }
}

void DeclarativeWebContainer::goBack()
{
    if (m_webPage && m_webPage->canGoBack()) {
        DBManager::instance()->goBack(m_webPage->tabId());
        m_webPage->goBack();
    }
}

bool DeclarativeWebContainer::activatePage(const Tab& tab, bool force, int parentId)
{
    if (!m_model) {
        return false;
    }

    m_webPages->initialize(this, m_webPageComponent.data());
    if ((m_model->loaded() || force) && tab.tabId() > 0 && m_webPages->initialized()) {
        WebPageActivationData activationData = m_webPages->page(tab, parentId);
        setWebPage(activationData.webPage);
        // Reset always height so that orentation change is taken into account.
        m_webPage->forceChrome(false);
        m_webPage->setChrome(true);
        connect(m_webPage, SIGNAL(imeNotification(int,bool,int,int,QString)),
                this, SLOT(imeNotificationChanged(int,bool,int,int,QString)), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(windowCloseRequested()), this, SLOT(closeWindow()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(urlChanged()), this, SLOT(onPageUrlChanged()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(loadingChanged()), this, SLOT(updateLoading()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(loadProgressChanged()), this, SLOT(updateLoadProgress()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(titleChanged()), this, SLOT(onPageTitleChanged()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(domContentLoadedChanged()), this, SLOT(sendVkbOpenCompositionMetrics()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(heightChanged()), this, SLOT(sendVkbOpenCompositionMetrics()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(widthChanged()), this, SLOT(sendVkbOpenCompositionMetrics()), Qt::UniqueConnection);
        connect(m_webPage, SIGNAL(requestGLContext()), this, SLOT(createGLContext()), Qt::DirectConnection);
        return activationData.activated;
    }
    return false;
}

bool DeclarativeWebContainer::alive(int tabId)
{
    return m_webPages->alive(tabId);
}

void DeclarativeWebContainer::updateMode()
{
    setTabModel(privateMode() ? m_privateTabModel.data() : m_persistentTabModel.data());
    setActiveTabData();

    // Reload active tab from new mode
    if (m_model->count() > 0) {
        reload(false);
    } else {
        setWebPage(NULL);
        emit contentItemChanged();
    }
}

void DeclarativeWebContainer::dumpPages() const
{
    m_webPages->dumpPages();
}

QObject *DeclarativeWebContainer::focusObject() const
{
    return m_webPage ? m_webPage : QWindow::focusObject();
}

bool DeclarativeWebContainer::eventFilter(QObject *obj, QEvent *event)
{
    // Hiding stops rendering. Don't pass it through if hiding is not allowed.
    if (event->type() == QEvent::Expose) {
        if (isExposed() && !m_hasBeenExposed) {
            QMutexLocker lock(&m_exposedMutex);
            QOpenGLContext context;
            context.setFormat(requestedFormat());
            context.create();
            context.makeCurrent(this);

            QOpenGLFunctions_ES2* funcs = context.versionFunctions<QOpenGLFunctions_ES2>();
            if (funcs) {
                funcs->glClearColor(1.0, 1.0, 1.0, 0.0);
                funcs->glClear(GL_COLOR_BUFFER_BIT);
            }

            context.swapBuffers(this);
            context.doneCurrent();
            m_hasBeenExposed = true;
            m_windowExposed.wakeAll();
        } else if (!isExposed() && !m_allowHiding) {
            return true;
        }
    } else if (event->type() == QEvent::Close && m_webPage) {
        // Make sure gecko does not use GL context we gave it in ::createGLContext
        // after the window has been closed.
        m_webPage->suspendView();
    }

    // Emit chrome exposed when both chrome window and browser window has been exposed. This way chrome
    // window can be raised to the foreground if needed.
    static bool hasExposedChrome = false;
    if (!hasExposedChrome && event->type() == QEvent::Show && m_chromeWindow && m_chromeWindow->isExposed() && isExposed()) {
        emit chromeExposed();
        hasExposedChrome = true;
    }

    return QObject::eventFilter(obj, event);
}

void DeclarativeWebContainer::touchEvent(QTouchEvent *event)
{
    if (!m_rotationHandler) {
        qWarning() << "Cannot deliver touch events without rotationHandler";
        return;
    }

    if (m_webPage && m_enabled) {
        QList<QTouchEvent::TouchPoint> touchPoints = event->touchPoints();
        QTouchEvent mappedTouchEvent = *event;

        for (int i = 0; i < touchPoints.count(); ++i) {
            QPointF pt = m_rotationHandler->mapFromScene(touchPoints.at(i).pos());
            touchPoints[i].setPos(pt);
        }

        mappedTouchEvent.setTouchPoints(touchPoints);
        m_webPage->touchEvent(&mappedTouchEvent);
    }
}

QVariant DeclarativeWebContainer::inputMethodQuery(Qt::InputMethodQuery property) const
{
    if (m_webPage && m_enabled) {
        return m_webPage->inputMethodQuery(property);
    }
    return QVariant();
}

void DeclarativeWebContainer::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->inputMethodEvent(event);
    }
}

void DeclarativeWebContainer::keyPressEvent(QKeyEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->keyPressEvent(event);
    }
}

void DeclarativeWebContainer::keyReleaseEvent(QKeyEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->keyReleaseEvent(event);
    }
}

void DeclarativeWebContainer::focusInEvent(QFocusEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->focusInEvent(event);
    }
}

void DeclarativeWebContainer::focusOutEvent(QFocusEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->focusOutEvent(event);
    }
}

void DeclarativeWebContainer::timerEvent(QTimerEvent *event)
{
    if (m_webPage && m_enabled) {
        m_webPage->timerEvent(event);
    }
}

void DeclarativeWebContainer::classBegin()
{
}

void DeclarativeWebContainer::componentComplete()
{
    if (m_initialized && !m_completed) {
        m_completed = true;
        emit completedChanged();
    }
}

void DeclarativeWebContainer::resetHeight(bool respectContentHeight)
{
    if (!m_webPage) {
        return;
    }

    m_webPage->resetHeight(respectContentHeight);
}

void DeclarativeWebContainer::updateContentOrientation(Qt::ScreenOrientation orientation)
{
    if (m_webPage) {
        m_webPage->updateContentOrientation(orientation);
    }

    reportContentOrientationChange(orientation);
}

void DeclarativeWebContainer::updateWindowState(Qt::WindowState windowState)
{
    if (m_webPage && windowState >= Qt::WindowMaximized) {
        m_webPage->update();
    }
}

void DeclarativeWebContainer::imeNotificationChanged(int state, bool open, int cause, int focusChange, const QString &type)
{
    Q_UNUSED(open)
    Q_UNUSED(cause)
    Q_UNUSED(focusChange)
    Q_UNUSED(type)

    // QmlMozView's input context open is actually intention (0 closed, 1 opened).
    // cause 3 equals InputContextAction::CAUSE_MOUSE nsIWidget.h
    if (state == 1 && cause == 3) {
        // For safety reset height based on contentHeight before going to "boundHeightControl" state
        // so that when vkb is closed we get correctly reset height back.
        resetHeight(true);
    }
}

qreal DeclarativeWebContainer::contentHeight() const
{
    if (m_webPage) {
        return m_webPage->contentHeight();
    } else {
        return 0.0;
    }
}

void DeclarativeWebContainer::onActiveTabChanged(int oldTabId, int activeTabId, bool loadActiveTab)
{
    if (activeTabId <= 0) {
        return;
    }
    setActiveTabData();

    if (!loadActiveTab) {
        return;
    }

    // Switch to different tab.
    if (oldTabId != activeTabId) {
        reload(false);
    }
}

void DeclarativeWebContainer::initialize()
{
    // This signal handler is responsible for activating
    // the first page.
    if (!canInitialize() || m_initialized) {
        return;
    }

    bool clearTabs = m_settingManager->clearHistoryRequested();
    int oldCount = m_model->count();

    // Clear tabs immediately from the model.
    if (clearTabs) {
        m_model->clear();
    }

    // If data was cleared when initialized and we had tabs in previous
    // session, reset tab model to unloaded state. DBManager emits
    // tabsAvailable with empty list when tabs are cleared => tab model
    // changes back to loaded and the initialize() slot gets called again.
    if (m_settingManager->initialize() && (oldCount > 0) && clearTabs) {
        m_model->setUnloaded();
        return;
    }

    // Load test
    // 1) no tabs and firstUseDone or we have incoming url, load initial url or home page to a new tab.
    // 2) model has tabs, load initial url or active tab.
    bool firstUseDone = DeclarativeWebUtils::instance()->firstUseDone();
    if (m_model->count() == 0 && (firstUseDone || !m_initialUrl.isEmpty())) {
        QString url = m_initialUrl.isEmpty() ? DeclarativeWebUtils::instance()->homePage() : m_initialUrl;
        QString title = "";
        m_model->newTab(url, title);
    } else if (m_model->count() > 0) {
        Tab tab = m_model->activeTab();
        if (!m_initialUrl.isEmpty()) {
            tab.setUrl(m_initialUrl);
        }
        loadTab(tab, true);
    }

    if (!m_completed) {
        m_completed = true;
        emit completedChanged();
    }
    m_initialized = true;
    disconnect(m_chromeWindow, SIGNAL(contentOrientationChanged(Qt::ScreenOrientation)), this, SLOT(updateContentOrientation(Qt::ScreenOrientation)));
}

void DeclarativeWebContainer::onDownloadStarted()
{
    // This is not 100% solid. A newTab is called on incoming
    // url (during browser start) if no tabs exist (waitingForNewTab). In slow network
    // connectivity one can create a new tab before downloadStarted is emitted
    // from DownloadManager. To get this to the 100%, we should add downloadStatus
    // to the QmlMozView containing status of downloading.
    if (m_model->waitingForNewTab())  {
        m_model->setWaitingForNewTab(false);
    } else {
        // In case browser is started with a dl url we have an "incorrect" initial url.
        // Emit urlChange() in order to trigger restoreHistory()
        emit m_webPage->urlChanged();
    }

    if (m_model->count() == 0) {
        // Download doesn't add tab to model. Mimic
        // model change in case downloading was started without
        // existing tabs.
        emit m_model->countChanged();
    }
}

void DeclarativeWebContainer::onNewTabRequested(QString url, QString title, int parentId)
{
    // TODO: Remove unused title argument.
    Q_UNUSED(title);
    Tab tab;
    tab.setTabId(m_model->nextTabId());
    if (activatePage(tab, false, parentId)) {
        m_webPage->loadTab(url, false);
    }
}

int DeclarativeWebContainer::parentTabId(int tabId) const
{
    if (m_webPages) {
        return m_webPages->parentTabId(tabId);
    }
    return 0;
}

void DeclarativeWebContainer::releasePage(int tabId, bool virtualize)
{
    if (m_webPages) {
        m_webPages->release(tabId, virtualize);
        // Successfully destroyed. Emit relevant property changes.
        if (!m_webPage || m_model->count() == 0) {

            if (m_tabId != 0) {
                m_tabId = 0;
                emit tabIdChanged();
            }

            emit contentItemChanged();
            emit loadingChanged();
            setLoadProgress(0);
        }
    }
}

void DeclarativeWebContainer::closeWindow()
{
    DeclarativeWebPage *webPage = qobject_cast<DeclarativeWebPage *>(sender());
    if (webPage && m_model) {
        int parentPageTabId = parentTabId(webPage->tabId());
        // Closing only allowed if window was created by script
        if (parentPageTabId > 0) {
            m_model->activateTabById(parentPageTabId);
            m_model->removeTabById(webPage->tabId(), isActiveTab(webPage->tabId()));
        }
    }
}

void DeclarativeWebContainer::onPageUrlChanged()
{
    DeclarativeWebPage *webPage = qobject_cast<DeclarativeWebPage *>(sender());
    if (webPage && m_model) {
        QString url = webPage->url().toString();
        int tabId = webPage->tabId();
        bool activeTab = isActiveTab(tabId);

        // Initial url should not be considered as navigation request that increases navigation history.
        // Cleanup this.
        bool initialLoad = !webPage->initialLoadHasHappened();
        // Virtualized pages need to be checked from the model.
        if (!initialLoad || m_model->contains(tabId)) {
            m_model->updateUrl(tabId, activeTab, url, initialLoad);
        } else {
            // Adding tab to the model is delayed so that url resolved to download link do not get added
            // to the model. We should have downloadStatus(status) and linkClicked(url) signals in QmlMozView.
            // To distinguish linkClicked(url) from downloadStatus(status) the downloadStatus(status) signal
            // should not be emitted when link clicking started downloading or opened (will open) a new window.
            m_model->addTab(url, "");
        }
        webPage->setInitialLoadHasHappened();
    }
}

void DeclarativeWebContainer::onPageTitleChanged()
{
    DeclarativeWebPage *webPage = qobject_cast<DeclarativeWebPage *>(sender());
    if (webPage && m_model) {
        QString url = webPage->url().toString();
        QString title = webPage->title();
        int tabId = webPage->tabId();
        bool activeTab = isActiveTab(tabId);
        m_model->updateTitle(tabId, activeTab, url, title);
    }
}

void DeclarativeWebContainer::updateLoadProgress()
{
    if (!m_webPage || (m_loadProgress == 0 && m_webPage->loadProgress() == 50)) {
        return;
    }

    int progress = m_webPage->loadProgress();
    if (progress > m_loadProgress) {
        setLoadProgress(progress);
    }
}

void DeclarativeWebContainer::updateLoading()
{
    if (m_webPage && m_webPage->loading()) {
        setLoadProgress(0);
    }

    emit loadingChanged();
}

void DeclarativeWebContainer::setActiveTabData()
{
    const Tab &tab = m_model->activeTab();
#if DEBUG_LOGS
    qDebug() << &tab;
#endif

    if (m_tabId != tab.tabId()) {
        m_tabId = tab.tabId();
        emit tabIdChanged();
    }
}

void DeclarativeWebContainer::updateWindowFlags()
{
    if (m_webPage) {
        static Qt::WindowFlags f = 0;
        if (f == 0) {
            f = flags();
        }

        if (!m_foreground) {
            setFlags(f | Qt::CoverWindow | Qt::FramelessWindowHint);
        } else {
            setFlags(f);
        }
    }
}

void DeclarativeWebContainer::updatePageFocus(bool focus)
{
    if (m_webPage) {
        if (focus) {
            QFocusEvent focusEvent(QEvent::FocusIn);
            m_webPage->focusInEvent(&focusEvent);
        } else {
            QFocusEvent focusEvent(QEvent::FocusOut);
            m_webPage->focusOutEvent(&focusEvent);
        }
    }
}

void DeclarativeWebContainer::updateVkbHeight()
{
    qreal vkbHeight = 0;
    // Keyboard rect is updated too late, when vkb hidden we cannot yet get size.
    // We need to send correct information to embedlite-components before virtual keyboard is open
    // so that when input element is focused contect is zoomed to the correct target (available area).
#if 0
    if (qGuiApp->inputMethod()) {
        vkbHeight = qGuiApp->inputMethod()->keyboardRectangle().height();
    }
#else
    // TODO: remove once keyboard height is not zero when hidden and take above #if 0 block into use.
    vkbHeight = 440;
    if (width() > height()) {
        vkbHeight = 340;
    }
    vkbHeight *= DeclarativeWebUtils::instance()->silicaPixelRatio();
#endif
    m_inputPanelOpenHeight = vkbHeight;
}

bool DeclarativeWebContainer::canInitialize() const
{
    return QMozContext::GetInstance()->initialized() && DownloadManager::instance()->initialized() && m_model && m_model->loaded();
}

void DeclarativeWebContainer::loadTab(const Tab& tab, bool force)
{
    if (activatePage(tab, true) || force) {
        // Note: active pages containing a "link" between each other (parent-child relationship)
        // are not destroyed automatically e.g. in low memory notification.
        // Hence, parentId is not necessary over here.
        m_webPage->loadTab(tab.url(), force);
    }
}

void DeclarativeWebContainer::sendVkbOpenCompositionMetrics()
{
    updateVkbHeight();

    QVariantMap map;

    // Round values to even numbers.
    int vkbOpenCompositionHeight = height() - m_inputPanelOpenHeight;
    int vkbOpenMaxCssCompositionWidth = width() / QMozContext::GetInstance()->pixelRatio();
    int vkbOpenMaxCssCompositionHeight = vkbOpenCompositionHeight / QMozContext::GetInstance()->pixelRatio();

    map.insert("compositionHeight", vkbOpenCompositionHeight);
    map.insert("maxCssCompositionWidth", vkbOpenMaxCssCompositionWidth);
    map.insert("maxCssCompositionHeight", vkbOpenMaxCssCompositionHeight);

    QVariant data(map);

    if (m_webPage) {
        m_webPage->sendAsyncMessage("embedui:vkbOpenCompositionMetrics", data);
    }
}

void DeclarativeWebContainer::createGLContext()
{
    QMutexLocker lock(&m_exposedMutex);
    if (!m_hasBeenExposed) {
        m_windowExposed.wait(&m_exposedMutex);
    }

    if (!m_context) {
        m_context = new QOpenGLContext();
        m_context->setFormat(requestedFormat());
        m_context->create();
        m_context->makeCurrent(this);
        initializeOpenGLFunctions();
    } else {
        m_context->makeCurrent(this);
    }
}
