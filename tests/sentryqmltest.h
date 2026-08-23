#pragma once

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qmutex.h>
#include <QtCore/qthread.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtTest/qtest.h>

#include <memory>

#if defined(SENTRY_QML_TEST_HAS_ZLIB)
#include <zlib.h>

#include <limits>
#endif

#if defined(SENTRY_QML_SDK_COCOA)
#define SENTRY_QML_EXPECT_FAIL_COCOA(reason) QEXPECT_FAIL("", reason, Continue)
#define SENTRY_QML_SKIP_COCOA(reason) QSKIP(reason)
#else
#define SENTRY_QML_EXPECT_FAIL_COCOA(reason) do {} while (false)
#define SENTRY_QML_SKIP_COCOA(reason) do {} while (false)
#endif

#if defined(SENTRY_QML_BACKEND_CRASHPAD)
#define SENTRY_QML_SKIP_CRASHPAD(reason) QSKIP(reason)
#else
#define SENTRY_QML_SKIP_CRASHPAD(reason) do {} while (false)
#endif

#if defined(SENTRY_QML_SDK_WASM)
#define SENTRY_QML_SKIP_WASM(reason) QSKIP(reason)
#else
#define SENTRY_QML_SKIP_WASM(reason) do {} while (false)
#endif

namespace SentryQmlTest {

inline QByteArray httpHeaderValue(const QByteArray &headers, const QByteArray &name)
{
    const QList<QByteArray> lines = headers.split('\n');
    for (const QByteArray &line : lines) {
        const qsizetype separator = line.indexOf(':');
        if (separator < 0) {
            continue;
        }
        if (line.left(separator).trimmed().compare(name, Qt::CaseInsensitive) == 0) {
            return line.mid(separator + 1).trimmed();
        }
    }
    return {};
}

#if defined(SENTRY_QML_TEST_HAS_ZLIB)
inline QByteArray gunzipBody(const QByteArray &body)
{
    if (body.isEmpty() || body.size() > std::numeric_limits<uInt>::max()) {
        return body;
    }

    z_stream stream {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(body.constData()));
    stream.avail_in = static_cast<uInt>(body.size());

    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        return body;
    }

    QByteArray inflated;
    QByteArray chunk(16 * 1024, Qt::Uninitialized);
    int result = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());

        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return body;
        }

        inflated.append(chunk.constData(), chunk.size() - stream.avail_out);
    } while (result != Z_STREAM_END);

    inflateEnd(&stream);
    return inflated;
}
#endif

inline QByteArray decodedHttpBody(const QByteArray &headers, const QByteArray &body)
{
#if defined(SENTRY_QML_TEST_HAS_ZLIB)
    const QByteArray encoding = httpHeaderValue(headers, QByteArrayLiteral("content-encoding")).toLower();
    if (encoding.contains("gzip")) {
        return gunzipBody(body);
    }
#else
    Q_UNUSED(headers);
#endif
    return body;
}

class EnvelopeServer
{
public:
    EnvelopeServer()
        : m_worker(new Worker(this))
    {
        m_worker->moveToThread(&m_thread);
        QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        m_thread.start();
    }

    ~EnvelopeServer()
    {
        QMetaObject::invokeMethod(
            m_worker,
            [worker = m_worker]
            {
                worker->close();
            },
            Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }

    bool listen(const QHostAddress &address, quint16 port = 0)
    {
        bool listening = false;
        QMetaObject::invokeMethod(
            m_worker,
            [this, &listening, address, port]
            {
                listening = m_worker->listen(address, port);
                if (listening) {
                    m_serverPort = m_worker->serverPort();
                }
            },
            Qt::BlockingQueuedConnection);
        return listening;
    }

    quint16 serverPort() const { return m_serverPort; }

    QList<QByteArray> bodies() const
    {
        QMutexLocker locker(&m_mutex);
        return m_bodies;
    }

    QByteArray combinedBody() const
    {
        const QList<QByteArray> allBodies = bodies();
        QByteArray result;
        for (const QByteArray &body : allBodies) {
            result += body;
            result += '\n';
        }
        return result;
    }

    QByteArray body() const
    {
        QMutexLocker locker(&m_mutex);
        return m_body;
    }

    bool contains(const QByteArray &needle) const { return combinedBody().contains(needle); }

    QString path() const
    {
        QMutexLocker locker(&m_mutex);
        return m_path;
    }

    bool receivedRequest() const
    {
        QMutexLocker locker(&m_mutex);
        return !m_requests.isEmpty();
    }

    QByteArray request() const
    {
        QMutexLocker locker(&m_mutex);
        return m_request;
    }

private:
    class Worker : public QTcpServer
    {
    public:
        explicit Worker(EnvelopeServer *server)
            : m_server(server)
        {
        }

    protected:
        void incomingConnection(qintptr socketDescriptor) override
        {
            auto *socket = new QTcpSocket(this);
            if (!socket->setSocketDescriptor(socketDescriptor)) {
                socket->deleteLater();
                return;
            }

            struct RequestState
            {
                QByteArray data;
                bool complete = false;
            };
            auto state = std::make_shared<RequestState>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                [this, socket, state]
                {
                    if (state->complete) {
                        return;
                    }
                    state->data += socket->readAll();

                    const qsizetype headerEnd = state->data.indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }

                    const QByteArray headers = state->data.left(headerEnd);
                    const QByteArray body = state->data.mid(headerEnd + 4);
                    const qsizetype contentLength
                        = httpHeaderValue(headers, QByteArrayLiteral("content-length")).toLongLong();
                    if (body.size() < contentLength) {
                        return;
                    }

                    state->complete = true;
                    const QList<QByteArray> requestLine = headers.split('\n').value(0).trimmed().split(' ');
                    m_server->recordRequest(state->data,
                        QString::fromUtf8(requestLine.value(1)),
                        decodedHttpBody(headers, body.left(contentLength)));
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }

    private:
        EnvelopeServer *m_server;
    };

    void recordRequest(const QByteArray &request, const QString &path, const QByteArray &body)
    {
        QMutexLocker locker(&m_mutex);
        m_requests.append(request);
        m_request += request;
        m_path = path;
        m_bodies.append(body);
        if (!m_body.isEmpty()) {
            m_body += '\n';
        }
        m_body += body;
    }

    mutable QMutex m_mutex;
    QList<QByteArray> m_requests;
    QList<QByteArray> m_bodies;
    QByteArray m_request;
    QByteArray m_body;
    QString m_path;
    QThread m_thread;
    Worker *m_worker;
    quint16 m_serverPort = 0;
};

} // namespace SentryQmlTest
