/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Raine Makelainen <raine.makelainen@jollamobile.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DECLARATIVEWEBCONTAINER_H
#define DECLARATIVEWEBCONTAINER_H

#include "settingmanager.h"
#include "webpages.h"

#include <qqml.h>
#include <QtGui/QWindow>
#include <QtGui/QOpenGLFunctions>
#include <QPointer>
#include <QImage>
#include <QFutureWatcher>
#include <QQmlComponent>
#include <QQuickView>
#include <QQuickItem>

#include <QMutex>
#include <QWaitCondition>

class QInputMethodEvent;
class QTimerEvent;
class DeclarativeTabModel;
class DeclarativeWebPage;
class Tab;

class DeclarativeWebContainer : public QWindow, public QQmlParserStatus, protected QOpenGLFunctions {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(QQuickItem *rotationHandler MEMBER m_rotationHandler NOTIFY rotationHandlerChanged FINAL)
    Q_PROPERTY(DeclarativeWebPage *contentItem READ webPage NOTIFY contentItemChanged FINAL)
    Q_PROPERTY(DeclarativeTabModel *tabModel READ tabModel WRITE setTabModel NOTIFY tabModelChanged FINAL)
    Q_PROPERTY(bool completed READ completed NOTIFY completedChanged FINAL)
    Q_PROPERTY(bool enabled MEMBER m_enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged FINAL)
    Q_PROPERTY(int maxLiveTabCount READ maxLiveTabCount WRITE setMaxLiveTabCount NOTIFY maxLiveTabCountChanged FINAL)
    // This property should cover all possible popus
    Q_PROPERTY(bool popupActive MEMBER m_popupActive NOTIFY popupActiveChanged FINAL)
    Q_PROPERTY(bool portrait MEMBER m_portrait NOTIFY portraitChanged FINAL)
    Q_PROPERTY(bool fullscreenMode MEMBER m_fullScreenMode NOTIFY fullscreenModeChanged FINAL)
    Q_PROPERTY(qreal fullscreenHeight MEMBER m_fullScreenHeight NOTIFY fullscreenHeightChanged FINAL)
    Q_PROPERTY(bool imOpened MEMBER m_imOpened NOTIFY imOpenedChanged FINAL)
    Q_PROPERTY(qreal toolbarHeight MEMBER m_toolbarHeight NOTIFY toolbarHeightChanged FINAL)
    Q_PROPERTY(bool allowHiding MEMBER m_allowHiding NOTIFY allowHidingChanged FINAL)

    Q_PROPERTY(QString favicon MEMBER m_favicon NOTIFY faviconChanged)

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged FINAL)

    Q_PROPERTY(int tabId READ tabId NOTIFY tabIdChanged FINAL)

    Q_PROPERTY(bool privateMode READ privateMode WRITE setPrivateMode NOTIFY privateModeChanged FINAL)

    Q_PROPERTY(QQmlComponent* webPageComponent MEMBER m_webPageComponent NOTIFY webPageComponentChanged FINAL)
    Q_PROPERTY(QObject *chromeWindow READ chromeWindow WRITE setChromeWindow NOTIFY chromeWindowChanged FINAL)
public:
    DeclarativeWebContainer(QWindow *parent = 0);
    ~DeclarativeWebContainer();

    DeclarativeWebPage *webPage() const;

    DeclarativeTabModel *tabModel() const;
    void setTabModel(DeclarativeTabModel *model);

    bool completed() const;

    bool foreground() const;
    void setForeground(bool active);

    int maxLiveTabCount() const;
    void setMaxLiveTabCount(int count);

    bool privateMode() const;
    void setPrivateMode(bool);

    bool loading() const;

    int loadProgress() const;
    void setLoadProgress(int loadProgress);

    bool imOpened() const;

    QObject *chromeWindow() const;
    void setChromeWindow(QObject *chromeWindow);

    bool canGoForward() const;
    bool canGoBack() const;

    int tabId() const;
    QString title() const;
    QString url() const;
    QString thumbnailPath() const;

    bool isActiveTab(int tabId);
    bool activatePage(const Tab& tab, bool force = false, int parentId = 0);

    Q_INVOKABLE void load(QString url = "", QString title = "", bool force = false);
    Q_INVOKABLE void reload(bool force = true);
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goBack();

    Q_INVOKABLE void updatePageFocus(bool focus);

    Q_INVOKABLE bool alive(int tabId);

    Q_INVOKABLE void dumpPages() const;

    QObject *focusObject() const;

signals:
    void rotationHandlerChanged();
    void contentItemChanged();
    void tabModelChanged();
    void completedChanged();
    void enabledChanged();
    void foregroundChanged();
    void allowHidingChanged();
    void maxLiveTabCountChanged();
    void popupActiveChanged();
    void portraitChanged();
    void fullscreenModeChanged();
    void fullscreenHeightChanged();
    void imOpenedChanged();
    void toolbarHeightChanged();

    void faviconChanged();
    void loadingChanged();
    void loadProgressChanged();

    void tabIdChanged();
    void thumbnailPathChanged();
    void privateModeChanged();

    void webPageComponentChanged();
    void chromeWindowChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event);
    virtual void touchEvent(QTouchEvent *event);
    virtual QVariant inputMethodQuery(Qt::InputMethodQuery property) const;
    virtual void inputMethodEvent(QInputMethodEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void keyReleaseEvent(QKeyEvent *event);
    virtual void focusInEvent(QFocusEvent *event);
    virtual void focusOutEvent(QFocusEvent *event);
    virtual void timerEvent(QTimerEvent *event);

    virtual void classBegin();
    virtual void componentComplete();


public slots:
    void resetHeight(bool respectContentHeight = true);
    void updateContentOrientation(Qt::ScreenOrientation orientation);

private slots:
    void updateWindowState(Qt::WindowState windowState);
    void imeNotificationChanged(int state, bool open, int cause, int focusChange, const QString& type);
    void initialize();
    void onActiveTabChanged(int oldTabId, int activeTabId, bool loadActiveTab);
    void onDownloadStarted();
    void onNewTabRequested(QString url, QString title, int parentId);
    void releasePage(int tabId, bool virtualize = false);
    void closeWindow();
    void onPageUrlChanged();
    void onPageTitleChanged();
    void updateLoadProgress();
    void updateLoading();
    void setActiveTabData();

    void updateWindowFlags();

    // These are here to inform embedlite-components that keyboard is open or close
    // matching composition metrics.
    void sendVkbOpenCompositionMetrics();

    void createGLContext();

private:
    void setWebPage(DeclarativeWebPage *webPage);
    qreal contentHeight() const;
    int parentTabId(int tabId) const;
    void updateVkbHeight();
    bool canInitialize() const;
    void loadTab(const Tab& tab, bool force);
    void updateMode();

    QPointer<QQuickItem> m_rotationHandler;
    QPointer<DeclarativeWebPage> m_webPage;
    QPointer<QQuickView> m_chromeWindow;
    QOpenGLContext *m_context;

    QPointer<DeclarativeTabModel> m_model;
    QPointer<QQmlComponent> m_webPageComponent;
    QPointer<SettingManager> m_settingManager;
    QPointer<WebPages> m_webPages;
    QPointer<DeclarativeTabModel> m_persistentTabModel;
    QPointer<DeclarativeTabModel> m_privateTabModel;

    bool m_enabled;
    bool m_foreground;
    bool m_allowHiding;
    bool m_popupActive;
    bool m_portrait;
    bool m_fullScreenMode;
    qreal m_fullScreenHeight;
    bool m_imOpened;
    qreal m_inputPanelOpenHeight;
    qreal m_toolbarHeight;

    QString m_favicon;

    // See DeclarativeWebContainer::load (line 283) as load need to "work" even if engine, model,
    // or qml component is not yet completed (completed property is still false). So cache url/title for later use.
    // Problem is visible with a download url as it does not trigger urlChange for the loaded page (correct behavior).
    // Once downloading has been started and if we have existing tabs we reset
    // back to the active tab and load it. In case we did not have tabs open when downloading was
    // triggered we just clear these.
    int m_tabId;
    QString m_initialUrl;

    bool m_loading;
    int m_loadProgress;

    bool m_completed;
    bool m_initialized;

    bool m_privateMode;
    bool m_hasBeenExposed;

    QMutex m_exposedMutex;
    QWaitCondition m_windowExposed;

    friend class tst_webview;
};

QML_DECLARE_TYPE(DeclarativeWebContainer)

#endif // DECLARATIVEWEBCONTAINER_H
