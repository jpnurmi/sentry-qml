#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtQml/qqmlregistration.h>

// Configuration descriptor. SentryOptions observes C++ descriptors without
// taking ownership and snapshots their values synchronously during init.
class SENTRYQML_EXPORT SentryIntegration : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentryIntegration)

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool required READ required WRITE setRequired NOTIFY requiredChanged)
    Q_PROPERTY(QVariantMap configuration READ configuration WRITE setConfiguration NOTIFY configurationChanged)

public:
    explicit SentryIntegration(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString path() const;
    void setPath(const QString &path);

    bool enabled() const;
    void setEnabled(bool enabled);

    bool required() const;
    void setRequired(bool required);

    QVariantMap configuration() const;
    void setConfiguration(const QVariantMap &configuration);

signals:
    void idChanged();
    void pathChanged();
    void enabledChanged();
    void requiredChanged();
    void configurationChanged();

private:
    QString m_id;
    QString m_path;
    QVariantMap m_configuration;
    bool m_enabled = true;
    bool m_required = false;
};
