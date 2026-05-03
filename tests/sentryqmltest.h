#pragma once

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtTest/qtest.h>

#if defined(SENTRY_QML_TEST_HAS_ZLIB)
#include <zlib.h>

#include <limits>
#endif

#if defined(SENTRY_QML_TEST_SDK_COCOA)
#define SENTRY_QML_EXPECT_FAIL_COCOA(reason) QEXPECT_FAIL("", reason, Continue)
#define SENTRY_QML_SKIP_COCOA(reason) QSKIP(reason)
#else
#define SENTRY_QML_EXPECT_FAIL_COCOA(reason) do {} while (false)
#define SENTRY_QML_SKIP_COCOA(reason) do {} while (false)
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

} // namespace SentryQmlTest
