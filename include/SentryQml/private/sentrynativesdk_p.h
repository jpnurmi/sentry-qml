#pragma once

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

#include <memory>

class Sentry;
struct SentryNativeEventHookState;
class SentryOptions;
union sentry_value_u;
typedef sentry_value_u sentry_value_t;

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

    QString captureMessage(Sentry *sentry, const QString &message, const QString &level);
    QString captureEvent(sentry_value_t event);

signals:
    void initializedChanged();

private:
    explicit SentryNativeSdk(QObject *parent = nullptr);
    ~SentryNativeSdk() override;

    void setInitialized(bool initialized);

    std::unique_ptr<SentryNativeEventHookState> m_beforeSendState;
    std::unique_ptr<SentryNativeEventHookState> m_onCrashState;
    bool m_initialized = false;
};
