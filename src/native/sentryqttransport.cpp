extern "C" {
#include <include/sentry.h>
}

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qfile.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qmutex.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qurl.h>
#include <QtNetwork/qnetworkaccessmanager.h>
#include <QtNetwork/qnetworkreply.h>
#include <QtNetwork/qnetworkrequest.h>

#include <limits>
#include <memory>
#include <new>

namespace {

constexpr int RequestTimeoutMs = 15000;

class SentryQtHttpClient
{
public:
    int send(sentry_http_request_t *httpRequest, sentry_http_response_t *httpResponse)
    {
        if (!ensureApplication() || QCoreApplication::closingDown()) {
            return 0;
        }

        const char *url = sentry_http_request_get_url(httpRequest);
        const char *method = sentry_http_request_get_method(httpRequest);
        if (!url || !method) {
            return 0;
        }

        QNetworkRequest request(QUrl(QString::fromUtf8(url)));
        const size_t headerCount = sentry_http_request_get_header_count(httpRequest);
        for (size_t i = 0; i < headerCount; ++i) {
            const char *key = nullptr;
            const char *value = nullptr;
            if (sentry_http_request_get_header(httpRequest, i, &key, &value)) {
                request.setRawHeader(QByteArray(key), QByteArray(value));
            }
        }
        request.setTransferTimeout(RequestTimeoutMs);

        QNetworkAccessManager &manager = networkAccessManager();
        QNetworkReply *reply = nullptr;
        size_t bodyLength = 0;
        const char *body = sentry_http_request_get_body(httpRequest, &bodyLength);
        if (body) {
            if (bodyLength > static_cast<size_t>(std::numeric_limits<qsizetype>::max())) {
                return 0;
            }
            reply = manager.sendCustomRequest(request, QByteArray(method),
                QByteArray(body, static_cast<qsizetype>(bodyLength)));
        } else {
            QFile bodyFile;
#ifdef Q_OS_WIN
            const wchar_t *bodyPath = sentry_http_request_get_body_file_pathw(httpRequest, &bodyLength);
            if (bodyPath) {
                bodyFile.setFileName(QString::fromWCharArray(bodyPath));
            }
#else
            const char *bodyPath = sentry_http_request_get_body_file_path(httpRequest, &bodyLength);
            if (bodyPath) {
                bodyFile.setFileName(QString::fromUtf8(bodyPath));
            }
#endif
            if (!bodyFile.fileName().isEmpty()) {
                if (!bodyFile.open(QIODevice::ReadOnly)) {
                    return 0;
                }
                reply = manager.sendCustomRequest(request, QByteArray(method), &bodyFile);
            } else {
                reply = manager.sendCustomRequest(request, QByteArray(method), QByteArray());
            }

            return finishRequest(reply, httpResponse);
        }

        return finishRequest(reply, httpResponse);
    }

    void shutdown()
    {
        QMutexLocker locker(&m_replyMutex);
        if (m_reply) {
            QMetaObject::invokeMethod(m_reply, &QNetworkReply::abort, Qt::QueuedConnection);
        }
    }

private:
    int finishRequest(QNetworkReply *reply, sentry_http_response_t *httpResponse)
    {
        if (!reply) {
            return 0;
        }

        {
            QMutexLocker locker(&m_replyMutex);
            m_reply = reply;
        }

        if (!reply->isFinished()) {
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
        }

        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (statusCode.isValid()) {
            sentry_http_response_set_status_code(httpResponse, statusCode.toInt());
        }
        const auto responseHeaders = reply->rawHeaderPairs();
        for (const auto &header : responseHeaders) {
            sentry_http_response_set_header(httpResponse, header.first.constData(), header.second.constData());
        }

        {
            QMutexLocker locker(&m_replyMutex);
            m_reply = nullptr;
            delete reply;
        }

        return statusCode.isValid() ? 1 : 0;
    }

    bool ensureApplication()
    {
        if (QCoreApplication::instance()) {
            return true;
        }

        m_argv[0] = m_applicationName.data();
        m_application = std::make_unique<QCoreApplication>(m_argc, m_argv);
        return QCoreApplication::instance();
    }

    static QNetworkAccessManager &networkAccessManager()
    {
        thread_local QNetworkAccessManager manager;
        return manager;
    }

    int m_argc = 1;
    QByteArray m_applicationName = QByteArrayLiteral("sentry-crash");
    char *m_argv[2] = {nullptr, nullptr};
    std::unique_ptr<QCoreApplication> m_application;
    QMutex m_replyMutex;
    QNetworkReply *m_reply = nullptr;
};

sentry_http_client_t *createClient(void *)
{
    return new (std::nothrow) SentryQtHttpClient;
}

int sendRequest(sentry_http_client_t *client, sentry_http_request_t *request,
    sentry_http_response_t *response)
{
    return static_cast<SentryQtHttpClient *>(client)->send(request, response);
}

void shutdownClient(sentry_http_client_t *client)
{
    static_cast<SentryQtHttpClient *>(client)->shutdown();
}

void freeClient(sentry_http_client_t *client)
{
    delete static_cast<SentryQtHttpClient *>(client);
}

} // namespace

extern "C" sentry_transport_t *sentry__transport_new_default(void)
{
    sentry_transport_t *transport = sentry_http_transport_new(createClient, nullptr, sendRequest, freeClient);
    if (transport) {
        sentry_http_transport_set_client_shutdown_func(transport, shutdownClient);
    }
    return transport;
}
