/****************************************************************************
**
** Copyright (C) 2014 Jolla Ltd.
** Contact: Raine Makelainen <raine.makelainen@jolla.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DECLARATIVEWEBUTILS_H
#define DECLARATIVEWEBUTILS_H

#include <QObject>
#include <QString>
#include <QColor>
#include <qqml.h>

class DeclarativeWebUtils : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool firstUseDone READ firstUseDone NOTIFY firstUseDoneChanged FINAL)

public:
    explicit DeclarativeWebUtils(QObject *parent = 0);

    Q_INVOKABLE int getLightness(QColor color) const;
    Q_INVOKABLE QString displayableUrl(QString fullUrl) const;

    static DeclarativeWebUtils *instance();

    bool firstUseDone() const;
    void setFirstUseDone(bool firstUseDone);
    qreal silicaPixelRatio() const;
    QString downloadDir() const;

signals:
    void firstUseDoneChanged();

private slots:
    void updateWebEngineSettings();

private:
    bool m_firstUseDone;
};

QML_DECLARE_TYPE(DeclarativeWebUtils)

#endif
