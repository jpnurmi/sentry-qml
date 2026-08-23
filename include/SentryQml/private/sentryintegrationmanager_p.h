#pragma once

#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>

#include <memory>

class QObject;
class Sentry;
class SentryIntegrationManagerPrivate;
class SentryOptions;
class SentrySdk;

class SentryIntegrationManager
{
public:
    explicit SentryIntegrationManager(SentrySdk *sdk);
    ~SentryIntegrationManager();

    void beginInitialization(Sentry *sentry, SentryOptions *options, const QString &backend);
    bool prepare(SentryOptions *options);
    bool start();
    bool flush(int timeoutMs, int *remainingTimeoutMs);
    void stop() noexcept;

    QStringList preparedIntegrationIds() const;
    QStringList activeIntegrationIds() const;

    bool registerService(const QString &iid, QObject *service);
    QObject *service(const QString &iid) const;
    bool runtimeAvailable() const;

private:
    std::unique_ptr<SentryIntegrationManagerPrivate> d;
};
