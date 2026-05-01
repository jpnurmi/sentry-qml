#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#include <memory>

class Sentry;
struct SentryNativeEventHookState;
class SentryOptions;
union sentry_value_u;
typedef sentry_value_u sentry_value_t;

enum class SentryNativeCaptureMode
{
    Manual,
    Automatic
};

class SentryNativeSdk : public QObject
{
    Q_OBJECT

public:
    static SentryNativeSdk *instance();

    bool isInitialized() const;
    bool init(Sentry *sentry, SentryOptions *options);
    bool flush(int timeoutMs);
    bool close();
    void detachSentry(Sentry *sentry);

    bool setRelease(Sentry *sentry, const QString &release);
    bool setUser(Sentry *sentry, const QVariantMap &user);
    bool removeUser(Sentry *sentry);
    bool setTag(Sentry *sentry, const QString &key, const QString &value);
    bool removeTag(Sentry *sentry, const QString &key);
    bool setContext(Sentry *sentry, const QString &key, const QVariantMap &context);
    bool removeContext(Sentry *sentry, const QString &key);
    bool addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb);
    bool log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes);
    bool count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes);
    bool gauge(Sentry *sentry,
               const QString &name,
               double value,
               const QString &unit,
               const QVariantMap &attributes);
    bool distribution(Sentry *sentry,
                      const QString &name,
                      double value,
                      const QString &unit,
                      const QVariantMap &attributes);
    QString captureMessage(Sentry *sentry, const QString &message, const QString &level);
    QString captureEvent(Sentry *sentry, sentry_value_t event, SentryNativeCaptureMode mode);

signals:
    void initializedChanged();

private:
    explicit SentryNativeSdk(QObject *parent = nullptr);
    ~SentryNativeSdk() override;

    void closeBeforeApplicationShutdown();
    void connectToApplicationShutdown();
    void setInitialized(bool initialized);

    std::unique_ptr<SentryNativeEventHookState> m_beforeSendState;
    std::unique_ptr<SentryNativeEventHookState> m_beforeBreadcrumbState;
    std::unique_ptr<SentryNativeEventHookState> m_beforeSendLogState;
    std::unique_ptr<SentryNativeEventHookState> m_beforeSendMetricState;
    std::unique_ptr<SentryNativeEventHookState> m_onCrashState;
    QMetaObject::Connection m_applicationShutdownConnection;
    bool m_initialized = false;
};
