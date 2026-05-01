extern "C" {
#include <include/sentry.h>
}

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qobject.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qurl.h>
#include <QtNetwork/qnetworkaccessmanager.h>
#include <QtNetwork/qnetworkreply.h>
#include <QtNetwork/qnetworkrequest.h>

#include <limits>
#include <memory>
#include <new>

namespace {

constexpr int RequestTimeoutMs = 15000;

class SentryQtTransportState
{
public:
    bool start(const sentry_options_t *options)
    {
        if (!ensureApplication()) {
            return false;
        }

        const char *dsnString = sentry_options_get_dsn(options);
        if (!dsnString || !*dsnString) {
            return true;
        }

        const QUrl dsn(QString::fromUtf8(dsnString));
        if (!dsn.isValid() || dsn.scheme().isEmpty() || dsn.host().isEmpty() || dsn.userName().isEmpty()) {
            return true;
        }

        QStringList pathSegments = dsn.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (pathSegments.isEmpty()) {
            return true;
        }

        const QString projectId = pathSegments.takeLast();
        QString envelopePath = QLatin1String("/");
        if (!pathSegments.isEmpty()) {
            envelopePath += pathSegments.join(QLatin1Char('/'));
            envelopePath += QLatin1Char('/');
        }
        envelopePath += QLatin1String("api/") + projectId + QLatin1String("/envelope/");

        m_envelopeUrl.setScheme(dsn.scheme());
        m_envelopeUrl.setHost(dsn.host());
        m_envelopeUrl.setPort(dsn.port());
        m_envelopeUrl.setPath(envelopePath);

        const char *userAgent = sentry_options_get_user_agent(options);
        m_userAgent = userAgent ? QByteArray(userAgent) : QByteArray();
        m_authHeader = QByteArrayLiteral("Sentry sentry_key=") + dsn.userName().toUtf8()
            + QByteArrayLiteral(", sentry_version=7");
        if (!dsn.password().isEmpty()) {
            m_authHeader += QByteArrayLiteral(", sentry_secret=") + dsn.password().toUtf8();
        }
        if (!m_userAgent.isEmpty()) {
            m_authHeader += QByteArrayLiteral(", sentry_client=") + m_userAgent;
        }
        return true;
    }

    void send(const QByteArray &body)
    {
        if (!QCoreApplication::instance() || QCoreApplication::closingDown()) {
            return;
        }

        if (!m_envelopeUrl.isValid() || body.isEmpty()) {
            return;
        }

        QNetworkRequest request(m_envelopeUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-sentry-envelope"));
        request.setRawHeader(QByteArrayLiteral("x-sentry-auth"), m_authHeader);
        if (!m_userAgent.isEmpty()) {
            request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromUtf8(m_userAgent));
        }
        request.setTransferTimeout(RequestTimeoutMs);

        QNetworkAccessManager manager;
        QNetworkReply *reply = manager.post(request, body);
        if (!reply) {
            return;
        }

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        delete reply;
    }

private:
    bool ensureApplication()
    {
        if (QCoreApplication::instance()) {
            return true;
        }

        m_argv[0] = m_applicationName.data();
        m_application = std::make_unique<QCoreApplication>(m_argc, m_argv);
        return QCoreApplication::instance();
    }

    int m_argc = 1;
    QByteArray m_applicationName = QByteArrayLiteral("sentry-crash");
    char *m_argv[2] = {nullptr, nullptr};
    std::unique_ptr<QCoreApplication> m_application;
    QUrl m_envelopeUrl;
    QByteArray m_authHeader;
    QByteArray m_userAgent;
};

int startupTransport(const sentry_options_t *options, void *state)
{
    return static_cast<SentryQtTransportState *>(state)->start(options) ? 0 : 1;
}

int flushTransport(uint64_t, void *)
{
    return 0;
}

int shutdownTransport(uint64_t, void *)
{
    return 0;
}

void sendEnvelope(sentry_envelope_t *envelope, void *state)
{
    size_t size = 0;
    char *serialized = sentry_envelope_serialize(envelope, &size);
    sentry_envelope_free(envelope);

    if (!serialized) {
        return;
    }

    if (size <= static_cast<size_t>(std::numeric_limits<qsizetype>::max())) {
        static_cast<SentryQtTransportState *>(state)->send(
            QByteArray(serialized, static_cast<qsizetype>(size)));
    }

    sentry_free(serialized);
}

void freeTransport(void *state)
{
    delete static_cast<SentryQtTransportState *>(state);
}

} // namespace

extern "C" sentry_transport_t *sentry__transport_new_default(void)
{
    auto *state = new (std::nothrow) SentryQtTransportState;
    if (!state) {
        return nullptr;
    }

    sentry_transport_t *transport = sentry_transport_new(sendEnvelope);
    if (!transport) {
        delete state;
        return nullptr;
    }

    sentry_transport_set_state(transport, state);
    sentry_transport_set_free_func(transport, freeTransport);
    sentry_transport_set_startup_func(transport, startupTransport);
    sentry_transport_set_flush_func(transport, flushTransport);
    sentry_transport_set_shutdown_func(transport, shutdownTransport);
    return transport;
}
