#include <SentryQml/sentryintegrationcontext.h>
#include <SentryQml/sentryintegrationplugin.h>

#include <QtCore/qobject.h>

class InstalledSmokeIntegration final : public QObject, public SentryIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SENTRY_QML_INTEGRATION_IID FILE "installedsmokeintegration.json")
    Q_INTERFACES(SentryIntegrationPlugin)

public:
    bool prepare(SentryIntegrationContext *context, const QVariantMap &configuration, QString *error) override
    {
        Q_UNUSED(configuration);
        Q_UNUSED(error);
        return context && context->backend() == QLatin1String("native");
    }

    bool start(QString *error) override
    {
        Q_UNUSED(error);
        return true;
    }

    bool flush(int timeoutMs, QString *error) override
    {
        Q_UNUSED(timeoutMs);
        Q_UNUSED(error);
        return true;
    }

    void stop() noexcept override
    {
    }
};

#include "installedsmokeintegration.moc"
