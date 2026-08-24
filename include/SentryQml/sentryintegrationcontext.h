#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <memory>

class QCoreApplication;
class QThread;
class SentryIntegrationContextPrivate;
class SentryIntegrationManager;
class SentryIntegrationManagerPrivate;

// SDK-owned context kept alive with the plugin root. Scope operations are
// available from successful backend initialization until integration stop;
// declared services are also available during prepare.
class SENTRYQML_EXPORT SentryIntegrationContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString sdkVersion READ sdkVersion CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QString backend READ backend CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum State
    {
        Inactive,
        Preparing,
        Running,
        Flushing,
        Stopping
    };
    Q_ENUM(State)

    ~SentryIntegrationContext() override;

    QString sdkVersion() const;
    QString platform() const;
    QString backend() const;
    QString databasePath() const;
    QCoreApplication *application() const;
    QThread *guiThread() const;
    State state() const;

    void reportWarning(const QString &message) const;
    void reportError(const QString &message) const;
    bool setContext(const QString &key, const QVariantMap &context);
    bool removeContext(const QString &key);
    bool setAttribute(const QString &key, const QVariant &value);
    bool removeAttribute(const QString &key);
    bool addBreadcrumb(const QVariantMap &breadcrumb);
    QObject *service(const QString &iid) const;

signals:
    void stateChanged();

private:
    friend class SentryIntegrationManager;
    friend class SentryIntegrationManagerPrivate;

    explicit SentryIntegrationContext(std::unique_ptr<SentryIntegrationContextPrivate> d, QObject *parent = nullptr);

    std::unique_ptr<SentryIntegrationContextPrivate> d;
};
