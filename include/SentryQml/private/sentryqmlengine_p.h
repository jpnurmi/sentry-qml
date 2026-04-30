#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qstringlist.h>
#include <QtQml/qjsvalue.h>

class QQmlError;
class QQmlEngine;
class Sentry;

class SentryQmlEngine : public QObject
{
    Q_OBJECT

public:
    explicit SentryQmlEngine(QQmlEngine *engine, Sentry *sentry);
    ~SentryQmlEngine() override;

    void installWarningHandler();
    void uninstallWarningHandler();
    void rememberThrownStackTrace(const QStringList &stackTrace);
    QString captureException(const QJSValue &exception);

private:
    void installThrowHook();
    QStringList takeThrownStackTrace(const QQmlError &error);

    QPointer<QQmlEngine> m_engine;
    QPointer<Sentry> m_sentry;
    QMetaObject::Connection m_warningsConnection;
    QList<QStringList> m_pendingStackTraces;
};
