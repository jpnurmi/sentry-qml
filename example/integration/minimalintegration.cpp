#include <SentryQml/sentryintegrationcontext.h>
#include <SentryQml/sentryintegrationplugin.h>

#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>

class MinimalIntegration final : public QObject, public SentryIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SENTRY_QML_INTEGRATION_IID FILE "minimalintegration.json")
    Q_INTERFACES(SentryIntegrationPlugin)

public:
    bool prepare(SentryIntegrationContext *context, const QVariantMap &configuration, QString *) override
    {
        m_context = context;
        m_label = configuration.value(QStringLiteral("label"), QStringLiteral("minimal")).toString();
        return true;
    }

    bool start(QString *) override
    {
        m_context->reportWarning(QStringLiteral("Starting integration '%1'.").arg(m_label));
        return true;
    }

    bool flush(int, QString *) override
    {
        return true;
    }

    void stop() noexcept override
    {
        m_context = nullptr;
        m_label.clear();
    }

private:
    QPointer<SentryIntegrationContext> m_context;
    QString m_label;
};

#include "minimalintegration.moc"
