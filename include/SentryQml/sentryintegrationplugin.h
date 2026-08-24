#pragma once

#include <QtCore/qplugin.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

#define SENTRY_QML_INTEGRATION_IID "io.sentry.qml.Integration/1.0"
#define SENTRY_QML_INTEGRATION_API 1

class SentryIntegrationContext;

// This interface is experimental until Sentry QML has a stable C++ ABI. Plugin
// methods run on the SDK lifecycle caller's thread and must be bounded and must
// not throw. prepare/start/flush run in descriptor order and stop runs in
// reverse order. stop is also called after prepare fails or throws so a
// partially prepared plugin can release its resources. The context and
// configuration remain owned by Sentry QML.
class SentryIntegrationPlugin
{
public:
    virtual ~SentryIntegrationPlugin() = default;

    virtual bool prepare(SentryIntegrationContext *context, const QVariantMap &configuration, QString *error) = 0;
    virtual bool start(QString *error) = 0;
    virtual bool flush(int timeoutMs, QString *error) = 0;
    virtual void stop() noexcept = 0;
};

Q_DECLARE_INTERFACE(SentryIntegrationPlugin, SENTRY_QML_INTEGRATION_IID)
