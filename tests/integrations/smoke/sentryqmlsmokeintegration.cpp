#include <SentryQml/sentryintegrationcontext.h>
#include <SentryQml/sentryintegrationplugin.h>

#include <QtCore/qfile.h>
#include <QtCore/qobject.h>
#include <QtCore/qtextstream.h>

class SentryQmlSmokeIntegration final : public QObject, public SentryIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SENTRY_QML_INTEGRATION_IID FILE "sentryqmlsmokeintegration.json")
    Q_INTERFACES(SentryIntegrationPlugin)

public:
    bool prepare(SentryIntegrationContext *context, const QVariantMap &configuration, QString *error) override
    {
        m_context = context;
        m_tracePath = configuration.value(QStringLiteral("tracePath")).toString();
        m_marker = configuration.value(QStringLiteral("marker")).toString();
        m_failStart = configuration.value(QStringLiteral("failStart")).toBool();
        m_failFlush = configuration.value(QStringLiteral("failFlush")).toBool();
        append(QStringLiteral("prepare:%1").arg(m_marker));
        if (configuration.value(QStringLiteral("failPrepare")).toBool()) {
            *error = QStringLiteral("configured prepare failure");
            return false;
        }
        return true;
    }

    bool start(QString *error) override
    {
        append(QStringLiteral("start:%1").arg(m_marker));
        if (m_failStart) {
            *error = QStringLiteral("configured start failure");
            return false;
        }
        if (!m_context->setContext(QStringLiteral("smoke"), {{QStringLiteral("marker"), m_marker}})) {
            *error = QStringLiteral("context operations are unavailable during start");
            return false;
        }
        return true;
    }

    bool flush(int timeoutMs, QString *error) override
    {
        append(QStringLiteral("flush:%1").arg(timeoutMs));
        if (m_failFlush) {
            *error = QStringLiteral("configured flush failure");
            return false;
        }
        return true;
    }

    void stop() noexcept override
    {
        append(QStringLiteral("stop:%1").arg(m_marker));
        m_context = nullptr;
        m_marker.clear();
        m_failStart = false;
        m_failFlush = false;
    }

private:
    void append(const QString &line) const
    {
        if (m_tracePath.isEmpty()) {
            return;
        }
        QFile file(m_tracePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream(&file) << line << '\n';
        }
    }

    SentryIntegrationContext *m_context = nullptr;
    QString m_tracePath;
    QString m_marker;
    bool m_failStart = false;
    bool m_failFlush = false;
};

#include "sentryqmlsmokeintegration.moc"
