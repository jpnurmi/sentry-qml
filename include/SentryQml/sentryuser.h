#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtQml/qqmlengine.h>

class SENTRYQML_EXPORT SentryUser : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentryUser)

    Q_PROPERTY(QString userId READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString email READ email WRITE setEmail NOTIFY emailChanged)
    Q_PROPERTY(QString ipAddress READ ipAddress WRITE setIpAddress NOTIFY ipAddressChanged)
    Q_PROPERTY(QVariantMap data READ data WRITE setData NOTIFY dataChanged)

public:
    explicit SentryUser(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString username() const;
    void setUsername(const QString &username);

    QString email() const;
    void setEmail(const QString &email);

    QString ipAddress() const;
    void setIpAddress(const QString &ipAddress);

    QVariantMap data() const;
    void setData(const QVariantMap &data);

    bool isEmpty() const;
    QVariantMap toVariantMap() const;

signals:
    void idChanged();
    void usernameChanged();
    void emailChanged();
    void ipAddressChanged();
    void dataChanged();

private:
    QString m_id;
    QString m_username;
    QString m_email;
    QString m_ipAddress;
    QVariantMap m_data;
};
