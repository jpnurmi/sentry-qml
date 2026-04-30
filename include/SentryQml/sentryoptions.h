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
    Q_PROPERTY(double sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(int shutdownTimeout READ shutdownTimeout WRITE setShutdownTimeout NOTIFY shutdownTimeoutChanged)
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

    double sampleRate() const;
    void setSampleRate(double sampleRate);

    int shutdownTimeout() const;
    void setShutdownTimeout(int shutdownTimeout);

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
    void sampleRateChanged();
    void shutdownTimeoutChanged();
    void beforeSendChanged();
    void onCrashChanged();

private:
    QString m_dsn;
    QString m_databasePath;
    QString m_release;
    QString m_environment;
    QString m_dist;
    bool m_debug = false;
    double m_sampleRate = 1.0;
    int m_shutdownTimeout = 2000;
    QJSValue m_beforeSend;
    QJSValue m_onCrash;
};
