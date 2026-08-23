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
        if (context) {
            return true;
        }
        *error = QStringLiteral("the integration context is unavailable");
        return false;
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
