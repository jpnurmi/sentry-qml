#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtQml/qjsvalue.h>
#include <QtQml/qqmlengine.h>

class SENTRYQML_EXPORT SentryOptions : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentryOptions)

    Q_PROPERTY(QString dsn READ dsn WRITE setDsn NOTIFY dsnChanged)
    Q_PROPERTY(QString databasePath READ databasePath WRITE setDatabasePath NOTIFY databasePathChanged)
    Q_PROPERTY(QString release READ release WRITE setRelease NOTIFY releaseChanged)
    Q_PROPERTY(QString environment READ environment WRITE setEnvironment NOTIFY environmentChanged)
    Q_PROPERTY(QString dist READ dist WRITE setDist NOTIFY distChanged)
    Q_PROPERTY(bool debug READ debug WRITE setDebug NOTIFY debugChanged)
    Q_PROPERTY(bool enableLogs READ enableLogs WRITE setEnableLogs NOTIFY enableLogsChanged)
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(int maxBreadcrumbs READ maxBreadcrumbs WRITE setMaxBreadcrumbs NOTIFY maxBreadcrumbsChanged)
    Q_PROPERTY(int shutdownTimeout READ shutdownTimeout WRITE setShutdownTimeout NOTIFY shutdownTimeoutChanged)
    Q_PROPERTY(QJSValue beforeBreadcrumb READ beforeBreadcrumb WRITE setBeforeBreadcrumb NOTIFY beforeBreadcrumbChanged)
    Q_PROPERTY(QJSValue beforeSendLog READ beforeSendLog WRITE setBeforeSendLog NOTIFY beforeSendLogChanged)
    Q_PROPERTY(QJSValue beforeSend READ beforeSend WRITE setBeforeSend NOTIFY beforeSendChanged)
    Q_PROPERTY(QJSValue onCrash READ onCrash WRITE setOnCrash NOTIFY onCrashChanged)

public:
    explicit SentryOptions(QObject *parent = nullptr);

    QString dsn() const;
    void setDsn(const QString &dsn);

    QString databasePath() const;
    void setDatabasePath(const QString &databasePath);

    QString release() const;
    void setRelease(const QString &release);

    QString environment() const;
    void setEnvironment(const QString &environment);

    QString dist() const;
    void setDist(const QString &dist);

    bool debug() const;
    void setDebug(bool debug);

    bool enableLogs() const;
    void setEnableLogs(bool enableLogs);

    double sampleRate() const;
    void setSampleRate(double sampleRate);

    int maxBreadcrumbs() const;
    void setMaxBreadcrumbs(int maxBreadcrumbs);

    int shutdownTimeout() const;
    void setShutdownTimeout(int shutdownTimeout);

    QJSValue beforeBreadcrumb() const;
    void setBeforeBreadcrumb(const QJSValue &beforeBreadcrumb);

    QJSValue beforeSendLog() const;
    void setBeforeSendLog(const QJSValue &beforeSendLog);

    QJSValue beforeSend() const;
    void setBeforeSend(const QJSValue &beforeSend);

    QJSValue onCrash() const;
    void setOnCrash(const QJSValue &onCrash);

signals:
    void dsnChanged();
    void databasePathChanged();
    void releaseChanged();
    void environmentChanged();
    void distChanged();
    void debugChanged();
    void enableLogsChanged();
    void sampleRateChanged();
    void maxBreadcrumbsChanged();
    void shutdownTimeoutChanged();
    void beforeBreadcrumbChanged();
    void beforeSendLogChanged();
    void beforeSendChanged();
    void onCrashChanged();

private:
    QString m_dsn;
    QString m_databasePath;
    QString m_release;
    QString m_environment;
    QString m_dist;
    bool m_debug = false;
    bool m_enableLogs = true;
    double m_sampleRate = 1.0;
    int m_maxBreadcrumbs = 100;
    int m_shutdownTimeout = 2000;
    QJSValue m_beforeBreadcrumb;
    QJSValue m_beforeSendLog;
    QJSValue m_beforeSend;
    QJSValue m_onCrash;
};
