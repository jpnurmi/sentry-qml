#include <SentryQml/sentryoptions.h>

#include <QtCore/qglobal.h>

SentryOptions::SentryOptions(QObject *parent)
    : QObject(parent)
{
}

QString SentryOptions::dsn() const
{
    return m_dsn;
}

void SentryOptions::setDsn(const QString &dsn)
{
    if (m_dsn == dsn) {
        return;
    }

    m_dsn = dsn;
    emit dsnChanged();
}

QString SentryOptions::databasePath() const
{
    return m_databasePath;
}

void SentryOptions::setDatabasePath(const QString &databasePath)
{
    if (m_databasePath == databasePath) {
        return;
    }

    m_databasePath = databasePath;
    emit databasePathChanged();
}

QString SentryOptions::release() const
{
    return m_release;
}

void SentryOptions::setRelease(const QString &release)
{
    if (m_release == release) {
        return;
    }

    m_release = release;
    emit releaseChanged();
}

QString SentryOptions::environment() const
{
    return m_environment;
}

void SentryOptions::setEnvironment(const QString &environment)
{
    if (m_environment == environment) {
        return;
    }

    m_environment = environment;
    emit environmentChanged();
}

QString SentryOptions::dist() const
{
    return m_dist;
}

void SentryOptions::setDist(const QString &dist)
{
    if (m_dist == dist) {
        return;
    }

    m_dist = dist;
    emit distChanged();
}

bool SentryOptions::debug() const
{
    return m_debug;
}

void SentryOptions::setDebug(bool debug)
{
    if (m_debug == debug) {
        return;
    }

    m_debug = debug;
    emit debugChanged();
}

double SentryOptions::sampleRate() const
{
    return m_sampleRate;
}

void SentryOptions::setSampleRate(double sampleRate)
{
    if (qFuzzyCompare(m_sampleRate, sampleRate)) {
        return;
    }

    m_sampleRate = sampleRate;
    emit sampleRateChanged();
}

int SentryOptions::shutdownTimeout() const
{
    return m_shutdownTimeout;
}

void SentryOptions::setShutdownTimeout(int shutdownTimeout)
{
    if (m_shutdownTimeout == shutdownTimeout) {
        return;
    }

    m_shutdownTimeout = shutdownTimeout;
    emit shutdownTimeoutChanged();
}

QJSValue SentryOptions::beforeSend() const
{
    return m_beforeSend;
}

void SentryOptions::setBeforeSend(const QJSValue &beforeSend)
{
    m_beforeSend = beforeSend;
    emit beforeSendChanged();
}

QJSValue SentryOptions::onCrash() const
{
    return m_onCrash;
}

void SentryOptions::setOnCrash(const QJSValue &onCrash)
{
    m_onCrash = onCrash;
    emit onCrashChanged();
}
