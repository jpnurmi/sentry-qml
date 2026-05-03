#include "httpbody_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qtemporarydir.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtTest/qtest.h>

#include <algorithm>
#include <memory>

#if defined(SENTRY_QML_TEST_SDK_COCOA)
#define SENTRY_QML_SKIP_COCOA(reason) QSKIP(reason)
#else
#define SENTRY_QML_SKIP_COCOA(reason) do {} while (false)
#endif

class SentryQmlIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void capturesSdkFeaturesThroughHttpTransport();
};

class IntegrationEnvelopeServer : public QTcpServer
{
    Q_OBJECT

public:
    using QTcpServer::QTcpServer;

    QList<QByteArray> bodies() const { return m_bodies; }

    QByteArray combinedBody() const
    {
        QByteArray body;
        for (const QByteArray &item : m_bodies) {
            body += item;
            body += '\n';
        }
        return body;
    }

    bool contains(const QByteArray &needle) const { return combinedBody().contains(needle); }

signals:
    void received();

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }

        auto request = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, request]
                {
                    request->append(socket->readAll());

                    const qsizetype headerEnd = request->indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }

                    const QByteArray headers = request->left(headerEnd);
                    const QByteArray body = request->mid(headerEnd + 4);
                    const qsizetype contentLength =
                        SentryQmlTest::httpHeaderValue(headers, QByteArrayLiteral("content-length")).toLongLong();

                    if (body.size() < contentLength) {
                        return;
                    }

                    m_bodies.append(SentryQmlTest::decodedHttpBody(headers, body.left(contentLength)));
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    emit received();
                });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    QList<QByteArray> m_bodies;
};

struct EnvelopeItem
{
    QJsonObject headers;
    QByteArray payload;
};

QList<EnvelopeItem> parseEnvelopeItems(const QByteArray &body)
{
    QList<EnvelopeItem> items;
    qsizetype pos = body.indexOf('\n');
    if (pos < 0) {
        return items;
    }
    ++pos;

    while (pos < body.size()) {
        const qsizetype headerEnd = body.indexOf('\n', pos);
        if (headerEnd < 0) {
            break;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(body.mid(pos, headerEnd - pos), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            break;
        }
        pos = headerEnd + 1;

        EnvelopeItem item;
        item.headers = document.object();
        const int length = item.headers.value(QStringLiteral("length")).toInt(-1);
        if (length >= 0 && pos + length <= body.size()) {
            item.payload = body.mid(pos, length);
            pos += length;
            if (pos < body.size() && body.at(pos) == '\n') {
                ++pos;
            }
        } else {
            const qsizetype payloadEnd = body.indexOf('\n', pos);
            if (payloadEnd < 0) {
                item.payload = body.mid(pos);
                pos = body.size();
            } else {
                item.payload = body.mid(pos, payloadEnd - pos);
                pos = payloadEnd + 1;
            }
        }
        items.append(item);
    }

    return items;
}

QList<EnvelopeItem> findEnvelopeItems(const QList<QByteArray> &bodies, const QByteArray &needle)
{
    for (const QByteArray &body : bodies) {
        if (body.contains(needle)) {
            return parseEnvelopeItems(body);
        }
    }
    return {};
}

EnvelopeItem findItem(const QList<EnvelopeItem> &items, const QString &type, const QByteArray &needle = {})
{
    const auto item = std::find_if(items.cbegin(), items.cend(),
                                   [&type, &needle](const EnvelopeItem &candidate)
                                   {
                                       return candidate.headers.value(QStringLiteral("type")).toString() == type
                                           && (needle.isEmpty() || candidate.payload.contains(needle));
                                   });
    return item != items.cend() ? *item : EnvelopeItem {};
}

void SentryQmlIntegrationTest::capturesSdkFeaturesThroughHttpTransport()
{
    SENTRY_QML_SKIP_COCOA("SentryCocoa integration coverage still depends on skipped log and metric envelope paths.");

    IntegrationEnvelopeServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString attachmentPath = QDir(temporaryDir.path()).filePath(QStringLiteral("diagnostic.log"));
    QFile attachmentFile(attachmentPath);
    QVERIFY(attachmentFile.open(QIODevice::WriteOnly));
    QCOMPARE(attachmentFile.write("integration file payload"), qint64(24));
    attachmentFile.close();

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDsn"), QStringLiteral("http://public@127.0.0.1:%1/42").arg(server.serverPort()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));
    engine.rootContext()->setContextProperty(QStringLiteral("testAttachmentPath"), attachmentPath);

    const QUrl fixtureUrl = QUrl::fromLocalFile(
        QDir(QStringLiteral(SENTRY_QML_TEST_QML_DIR)).filePath(QStringLiteral("IntegrationTest.qml")));
    QQmlComponent component(&engine, fixtureUrl);

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("releaseSet").toBool(), true);
    QCOMPARE(object->property("environmentSet").toBool(), true);
    QCOMPARE(object->property("userSet").toBool(), true);
    QCOMPARE(object->property("tagSet").toBool(), true);
    QCOMPARE(object->property("removedTagSet").toBool(), true);
    QCOMPARE(object->property("tagRemoved").toBool(), true);
    QCOMPARE(object->property("contextSet").toBool(), true);
    QCOMPARE(object->property("removedContextSet").toBool(), true);
    QCOMPARE(object->property("contextRemoved").toBool(), true);
    QCOMPARE(object->property("attributeSet").toBool(), true);
    QCOMPARE(object->property("attributeWithUnitSet").toBool(), true);
    QCOMPARE(object->property("removedAttributeSet").toBool(), true);
    QCOMPARE(object->property("attributeRemoved").toBool(), true);
    QCOMPARE(object->property("fingerprintSet").toBool(), true);
    QCOMPARE(object->property("breadcrumbAdded").toBool(), true);
    QCOMPARE(object->property("fileAttached").toBool(), true);
    QCOMPARE(object->property("bytesAttached").toBool(), true);
    QCOMPARE(object->property("feedbackFileAttached").toBool(), true);
    QCOMPARE(object->property("feedbackBytesAttached").toBool(), true);
    QCOMPARE(object->property("feedbackAttachmentCount").toInt(), 2);
    QCOMPARE(object->property("bareFeedbackCaptured").toBool(), true);
    QCOMPARE(object->property("feedbackCaptured").toBool(), true);
    QCOMPARE(object->property("sessionStarted").toBool(), true);
    QCOMPARE(object->property("sessionEnded").toBool(), true);
    QCOMPARE(object->property("genericMetricCaptured").toBool(), true);
    QCOMPARE(object->property("countCaptured").toBool(), true);
    QCOMPARE(object->property("gaugeCaptured").toBool(), true);
    QCOMPARE(object->property("distributionCaptured").toBool(), true);
    QCOMPARE(object->property("logCaptured").toBool(), true);
    QCOMPARE(object->property("enumLogCaptured").toBool(), true);
    QCOMPARE(object->property("declarativeEventId").toString().size(), 36);
    QCOMPARE(object->property("messageEventId").toString().size(), 36);
    QCOMPARE(object->property("exceptionEventId").toString().size(), 36);

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral(".*missingIntegrationFunction is not defined")));
    QMetaObject::invokeMethod(object.get(), "triggerUncaughtQmlError");
    QCOMPARE(object->property("uncaughtTriggered").toBool(), true);

    QVERIFY(QMetaObject::invokeMethod(object.get(), "finish"));
    QCOMPARE(object->property("flushed").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);
    QCOMPARE(object->property("fileInvalidated").toBool(), true);
    QCOMPARE(object->property("bytesInvalidated").toBool(), true);
    QCOMPARE(object->property("beforeBreadcrumbCalled").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("beforeSendLogCalled").toBool(), true);
    QCOMPARE(object->property("beforeSendMetricCalled").toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Declarative options integration message"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Integration message"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Integration exception"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Integration bare feedback"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Integration feedback"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("missingIntegrationFunction"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("Integration log"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("qml.integration.duration"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.contains("\"type\":\"session\""), 5000);

    const QByteArray allBodies = server.combinedBody();
    QVERIFY(allBodies.contains("Integration enum log"));
    QVERIFY(allBodies.contains("qml.integration.before_send_log"));
    QVERIFY(allBodies.contains("qml.integration.generic"));
    QVERIFY(allBodies.contains("qml.integration.clicks"));
    QVERIFY(allBodies.contains("qml.integration.active_items"));
    QVERIFY(allBodies.contains("qml.integration.global"));
    QVERIFY(allBodies.contains("integration-global"));
    QVERIFY(allBodies.contains("qml.integration.global_duration"));
    QVERIFY(!allBodies.contains("qml.integration.removed_global"));
    QVERIFY(allBodies.contains("millisecond"));
    QVERIFY(allBodies.contains("qml.integration.before_send_metric"));
    QVERIFY(allBodies.contains("\"status\":\"exited\""));

    const QList<EnvelopeItem> declarativeItems =
        findEnvelopeItems(server.bodies(), "Declarative options integration message");
    QVERIFY(!declarativeItems.isEmpty());

    const EnvelopeItem declarativeEvent =
        findItem(declarativeItems, QStringLiteral("event"), "Declarative options integration message");
    QVERIFY(!declarativeEvent.payload.isEmpty());
    QVERIFY(declarativeEvent.payload.contains("\"release\":\"sentry-qml@declarative\""));
    QVERIFY(declarativeEvent.payload.contains("\"environment\":\"declarative\""));
    QVERIFY(declarativeEvent.payload.contains("\"dist\":\"42\""));
    QVERIFY(declarativeEvent.payload.contains("\"id\":\"declarative-user\""));
    QVERIFY(declarativeEvent.payload.contains("\"username\":\"ada\""));
    QVERIFY(declarativeEvent.payload.contains("\"email\":\"ada@example.com\""));
    QVERIFY(declarativeEvent.payload.contains("\"ip_address\":\"127.0.0.1\""));
    QVERIFY(declarativeEvent.payload.contains("\"role\":\"admin\""));
    QVERIFY(declarativeEvent.payload.contains("\"before_send\":\"qml\""));

    const QList<EnvelopeItem> messageItems = findEnvelopeItems(server.bodies(), "Integration message");
    QVERIFY(!messageItems.isEmpty());

    const EnvelopeItem event = findItem(messageItems, QStringLiteral("event"), "Integration message");
    QVERIFY(!event.payload.isEmpty());
    QVERIFY(event.payload.contains("\"level\":\"warning\""));
    QVERIFY(event.payload.contains("\"release\":\"sentry-qml@runtime\""));
    QVERIFY(event.payload.contains("\"environment\":\"runtime\""));
    QVERIFY(event.payload.contains("\"dist\":\"42\""));
    QVERIFY(event.payload.contains("\"id\":\"integration-user\""));
    QVERIFY(event.payload.contains("\"username\":\"grace\""));
    QVERIFY(event.payload.contains("\"email\":\"grace@example.com\""));
    QVERIFY(event.payload.contains("\"ip_address\":\"127.0.0.2\""));
    QVERIFY(event.payload.contains("\"role\":\"operator\""));
    QVERIFY(event.payload.contains("\"screen\":\"integration\""));
    QVERIFY(event.payload.contains("\"before_send\":\"qml\""));
    QVERIFY(event.payload.contains("\"retries\":3"));
    QVERIFY(event.payload.contains("\"enabled\":true"));
    QVERIFY(event.payload.contains("\"fingerprint\":[\"{{ default }}\",\"integration\"]"));
    QVERIFY(event.payload.contains("integration breadcrumb"));
    QVERIFY(event.payload.contains("beforeBreadcrumb"));
    QVERIFY(!event.payload.contains("removed_tag"));
    QVERIFY(!event.payload.contains("removed_context"));

    const EnvelopeItem fileAttachment = findItem(messageItems, QStringLiteral("attachment"), "integration file payload");
    QVERIFY(!fileAttachment.payload.isEmpty());
    QCOMPARE(fileAttachment.headers.value(QStringLiteral("filename")).toString(), QStringLiteral("diagnostic.log"));
    QCOMPARE(fileAttachment.headers.value(QStringLiteral("content_type")).toString(), QStringLiteral("text/plain"));
    QCOMPARE(fileAttachment.payload, QByteArrayLiteral("integration file payload"));

    const EnvelopeItem byteAttachment = findItem(messageItems, QStringLiteral("attachment"), "integration bytes payload");
    QVERIFY(!byteAttachment.payload.isEmpty());
    QCOMPARE(byteAttachment.headers.value(QStringLiteral("filename")).toString(), QStringLiteral("inline.txt"));
    QCOMPARE(byteAttachment.headers.value(QStringLiteral("content_type")).toString(), QStringLiteral("text/plain"));
    QCOMPARE(byteAttachment.payload, QByteArrayLiteral("integration bytes payload"));

    const QList<EnvelopeItem> bareFeedbackItems = findEnvelopeItems(server.bodies(), "Integration bare feedback");
    QVERIFY(!bareFeedbackItems.isEmpty());

    const EnvelopeItem bareFeedback =
        findItem(bareFeedbackItems, QStringLiteral("feedback"), "Integration bare feedback");
    QVERIFY(!bareFeedback.payload.isEmpty());
    QVERIFY(bareFeedback.payload.contains("\"message\":\"Integration bare feedback\""));
    QVERIFY(bareFeedback.payload.contains("\"contact_email\":\"bare-feedback@example.com\""));
    QVERIFY(findItem(bareFeedbackItems, QStringLiteral("attachment")).payload.isEmpty());

    const QList<EnvelopeItem> feedbackItems = findEnvelopeItems(server.bodies(), "Integration feedback");
    QVERIFY(!feedbackItems.isEmpty());

    const EnvelopeItem feedback = findItem(feedbackItems, QStringLiteral("feedback"), "Integration feedback");
    QVERIFY(!feedback.payload.isEmpty());
    QVERIFY(feedback.payload.contains("\"message\":\"Integration feedback\""));
    QVERIFY(feedback.payload.contains("\"contact_email\":\"feedback@example.com\""));
    QVERIFY(feedback.payload.contains("\"name\":\"Feedback User\""));

    QString normalizedAssociatedEventId = object->property("messageEventId").toString();
    normalizedAssociatedEventId.remove(QLatin1Char('-'));
    const QByteArray expectedAssociatedEventId =
        QByteArrayLiteral("\"associated_event_id\":\"") + normalizedAssociatedEventId.toUtf8() + QByteArrayLiteral("\"");
    QVERIFY(feedback.payload.contains(expectedAssociatedEventId));

    const EnvelopeItem feedbackFileAttachment =
        findItem(feedbackItems, QStringLiteral("attachment"), "integration file payload");
    QVERIFY(!feedbackFileAttachment.payload.isEmpty());
    QCOMPARE(feedbackFileAttachment.headers.value(QStringLiteral("filename")).toString(),
             QStringLiteral("feedback-diagnostic.log"));
    QCOMPARE(feedbackFileAttachment.headers.value(QStringLiteral("content_type")).toString(),
             QStringLiteral("text/plain"));
    QCOMPARE(feedbackFileAttachment.payload, QByteArrayLiteral("integration file payload"));

    const EnvelopeItem feedbackByteAttachment =
        findItem(feedbackItems, QStringLiteral("attachment"), "integration feedback bytes payload");
    QVERIFY(!feedbackByteAttachment.payload.isEmpty());
    QCOMPARE(feedbackByteAttachment.headers.value(QStringLiteral("filename")).toString(),
             QStringLiteral("feedback-inline.txt"));
    QCOMPARE(feedbackByteAttachment.headers.value(QStringLiteral("content_type")).toString(),
             QStringLiteral("text/plain"));
    QCOMPARE(feedbackByteAttachment.payload, QByteArrayLiteral("integration feedback bytes payload"));
}

QTEST_MAIN(SentryQmlIntegrationTest)

#include "tst_integration.moc"
