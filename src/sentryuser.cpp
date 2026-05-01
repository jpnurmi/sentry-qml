#include <SentryQml/sentryuser.h>

SentryUser::SentryUser(QObject *parent)
    : QObject(parent)
{
}

QString SentryUser::id() const
{
    return m_id;
}

void SentryUser::setId(const QString &id)
{
    if (m_id == id) {
        return;
    }

    m_id = id;
    emit idChanged();
}

QString SentryUser::username() const
{
    return m_username;
}

void SentryUser::setUsername(const QString &username)
{
    if (m_username == username) {
        return;
    }

    m_username = username;
    emit usernameChanged();
}

QString SentryUser::email() const
{
    return m_email;
}

void SentryUser::setEmail(const QString &email)
{
    if (m_email == email) {
        return;
    }

    m_email = email;
    emit emailChanged();
}

QString SentryUser::ipAddress() const
{
    return m_ipAddress;
}

void SentryUser::setIpAddress(const QString &ipAddress)
{
    if (m_ipAddress == ipAddress) {
        return;
    }

    m_ipAddress = ipAddress;
    emit ipAddressChanged();
}

QVariantMap SentryUser::data() const
{
    return m_data;
}

void SentryUser::setData(const QVariantMap &data)
{
    if (m_data == data) {
        return;
    }

    m_data = data;
    emit dataChanged();
}

bool SentryUser::isEmpty() const
{
    return m_id.isEmpty() && m_username.isEmpty() && m_email.isEmpty() && m_ipAddress.isEmpty() && m_data.isEmpty();
}

QVariantMap SentryUser::toVariantMap() const
{
    QVariantMap user = m_data;
    if (!m_id.isEmpty()) {
        user.insert(QStringLiteral("id"), m_id);
    }
    if (!m_username.isEmpty()) {
        user.insert(QStringLiteral("username"), m_username);
    }
    if (!m_email.isEmpty()) {
        user.insert(QStringLiteral("email"), m_email);
    }
    if (!m_ipAddress.isEmpty()) {
        user.insert(QStringLiteral("ip_address"), m_ipAddress);
    }
    return user;
}
