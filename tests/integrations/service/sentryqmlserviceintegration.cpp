#include <SentryQml/sentryintegrationcontext.h>
#include <SentryQml/sentryintegrationplugin.h>

#include <QtCore/qfile.h>
#include <QtCore/qobject.h>

class SentryQmlServiceIntegration final : public QObject, public SentryIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SENTRY_QML_INTEGRATION_IID FILE "sentryqmlserviceintegration.json")
    Q_INTERFACES(SentryIntegrationPlugin)

public:
    SentryQmlServiceIntegration()
    {
        const QString marker = qEnvironmentVariable("SENTRY_QML_SERVICE_CONSTRUCTION_MARKER");
        if (!marker.isEmpty()) {
            QFile file(marker);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            }
        }
    }

    bool prepare(SentryIntegrationContext *context, const QVariantMap &, QString *error) override
    {
        if (context->service(QStringLiteral("io.sentry.qml.missing-test-service/1"))) {
            return true;
        }
        *error = QStringLiteral("required test service was not provided");
        return false;
    }

    bool start(QString *) override
    {
        return true;
    }
    bool flush(int, QString *) override
    {
        return true;
    }
    void stop() noexcept override
    {
    }
};

#include "sentryqmlserviceintegration.moc"
