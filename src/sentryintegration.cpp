#include <SentryQml/sentryintegration.h>

SentryIntegration::SentryIntegration(QObject *parent)
    : QObject(parent)
{
}

QString SentryIntegration::id() const
{
    return m_id;
}

void SentryIntegration::setId(const QString &id)
{
    if (m_id == id) {
        return;
    }

    m_id = id;
    emit idChanged();
}

QString SentryIntegration::path() const
{
    return m_path;
}

void SentryIntegration::setPath(const QString &path)
{
    if (m_path == path) {
        return;
    }

    m_path = path;
    emit pathChanged();
}

bool SentryIntegration::enabled() const
{
    return m_enabled;
}

void SentryIntegration::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    emit enabledChanged();
}

bool SentryIntegration::required() const
{
    return m_required;
}

void SentryIntegration::setRequired(bool required)
{
    if (m_required == required) {
        return;
    }

    m_required = required;
    emit requiredChanged();
}

QVariantMap SentryIntegration::configuration() const
{
    return m_configuration;
}

void SentryIntegration::setConfiguration(const QVariantMap &configuration)
{
    if (m_configuration == configuration) {
        return;
    }

    m_configuration = configuration;
    emit configurationChanged();
}
