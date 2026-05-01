#include <SentryQml/sentry.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qdir.h>
#include <QtCore/qtemporarydir.h>
#include <QtNetwork/qhostaddress.h>
#include <QtNetwork/qtcpserver.h>
#include <QtNetwork/qtcpsocket.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtTest/qsignalspy.h>
#include <QtTest/qtest.h>

#include <memory>

class SentryQmlTest : public QObject
{
    Q_OBJECT

private slots:
    void importsQmlModule();
    void initializesAndCapturesMessage();
    void sendsEnvelopeWithQtTransport();
    void capturesManualException();
    void capturesUncaughtQmlError();
    void beforeSendCanDropMessage();
    void beforeSendCannotCaptureMessage();
};

class EnvelopeServer : public QTcpServer
{
    Q_OBJECT

public:
    using QTcpServer::QTcpServer;

    QByteArray request() const { return m_request; }
    QByteArray body() const { return m_body; }
    QString path() const { return m_path; }
    bool receivedRequest() const { return m_receivedRequest; }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }

        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket]
                {
                    m_request += socket->readAll();

                    const qsizetype headerEnd = m_request.indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }

                    const QByteArray headers = m_request.left(headerEnd);
                    const QByteArray body = m_request.mid(headerEnd + 4);
                    qsizetype contentLength = 0;
                    const QList<QByteArray> lines = headers.split('\n');
                    for (const QByteArray &line : lines) {
                        const qsizetype separator = line.indexOf(':');
                        if (separator < 0) {
                            continue;
                        }
                        if (line.left(separator).trimmed().compare("content-length", Qt::CaseInsensitive) == 0) {
                            contentLength = line.mid(separator + 1).trimmed().toLongLong();
                        }
                    }

                    if (body.size() < contentLength) {
                        return;
                    }

                    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                    m_path = QString::fromUtf8(requestLine.value(1));
                    m_body = body.left(contentLength);
                    m_receivedRequest = true;

                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    emit received();
                });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

signals:
    void received();

private:
    QByteArray m_request;
    QByteArray m_body;
    QString m_path;
    bool m_receivedRequest = false;
};

void SentryQmlTest::importsQmlModule()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property SentryOptions options: SentryOptions {
                debug: true
                sampleRate: 1.0
                onCrash: function(event) { return event }
            }
            property bool ready: !Sentry.initialized && options.debug

            Component.onCompleted: {
                options.shutdownTimeout = 100
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryImportTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    QCOMPARE(component.status(), QQmlComponent::Ready);
    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
}

void SentryQmlTest::initializesAndCapturesMessage()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    Sentry sentry;
    QSignalSpy initializedSpy(&sentry, &Sentry::initializedChanged);

    SentryOptions options;
    options.setDatabasePath(QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));
    options.setDebug(true);
    options.setSampleRate(1.0);
    options.setShutdownTimeout(2000);

    QVERIFY(sentry.init(&options));
    QCOMPARE(sentry.isInitialized(), true);
    QCOMPARE(initializedSpy.count(), 1);

    const QString eventId = sentry.captureMessage(QStringLiteral("Hello from tst_sentryqml"));
    QCOMPARE(eventId.size(), 36);

    QVERIFY(sentry.flush(2000));
    QVERIFY(sentry.close());
    QCOMPARE(sentry.isInitialized(), false);
}

void SentryQmlTest::sendsEnvelopeWithQtTransport()
{
    EnvelopeServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    Sentry sentry;
    SentryOptions options;
    options.setDsn(QStringLiteral("http://public@127.0.0.1:%1/42").arg(server.serverPort()));
    options.setDatabasePath(QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));
    options.setShutdownTimeout(2000);

    QVERIFY(sentry.init(&options));

    const QString eventId = sentry.captureMessage(QStringLiteral("Sent through QtNetwork"));
    QCOMPARE(eventId.size(), 36);

    QTRY_VERIFY_WITH_TIMEOUT(server.receivedRequest(), 5000);
    QVERIFY(server.path().endsWith(QStringLiteral("/api/42/envelope/")));
    const QByteArray request = server.request().toLower();
    QVERIFY(request.contains("x-sentry-auth:"));
    QVERIFY(request.contains("application/x-sentry-envelope"));
    QVERIFY(server.body().contains("Sent through QtNetwork"));

    QVERIFY(sentry.close());
}

void SentryQmlTest::capturesManualException()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool initialized: false
            property bool closed: false
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                try {
                    throw new Error("Caught by tst_sentryqml")
                } catch (exception) {
                    eventId = Sentry.captureException(exception)
                }
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryCaptureExceptionTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("eventId").toString().size(), 36);
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::capturesUncaughtQmlError()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool callbackCalled: false
            property int frameCount: 0
            property string eventPlatform: ""
            property string framePlatform: ""
            property bool frameInApp: false
            property bool frameHasAbsolutePath: false
            property bool initialized: false
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    const exception = event.exception && event.exception.values
                        ? event.exception.values[0]
                        : null
                    if (exception && exception.type === "QmlError"
                            && exception.mechanism && exception.mechanism.handled === false) {
                        const frames = exception.stacktrace && exception.stacktrace.frames
                            ? exception.stacktrace.frames
                            : []
                        frameCount = frames.length
                        eventPlatform = event.platform || ""
                        if (frames.length > 0) {
                            framePlatform = frames[0].platform || ""
                            frameInApp = frames[0].in_app === true
                            frameHasAbsolutePath = !!frames[0].abs_path
                        }
                        callbackCalled = true
                        return null
                    }
                    return event
                }
            }

            function callMissingQmlFunction() {
                missingQmlFunction()
            }

            function triggerUncaughtError() {
                callMissingQmlFunction()
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                triggerUncaughtErrorTimer.start()
            }

            property Timer triggerUncaughtErrorTimer: Timer {
                interval: 0
                repeat: false
                onTriggered: triggerUncaughtError()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryUncaughtQmlErrorTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(object->property("callbackCalled").toBool(), true, 5000);
#if defined(SENTRY_QML_HAS_QML_PRIVATE)
    QVERIFY(object->property("frameCount").toInt() > 1);
#else
    QCOMPARE(object->property("frameCount").toInt(), 1);
#endif
    QCOMPARE(object->property("eventPlatform").toString(), QStringLiteral("javascript"));
    QCOMPARE(object->property("framePlatform").toString(), QStringLiteral("javascript"));
    QCOMPARE(object->property("frameInApp").toBool(), true);
    QCOMPARE(object->property("frameHasAbsolutePath").toBool(), true);

    Sentry sentry;
    QVERIFY(sentry.close());
}

void SentryQmlTest::beforeSendCanDropMessage()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool callbackCalled: false
            property bool initialized: false
            property bool closed: false
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    callbackCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                eventId = Sentry.captureMessage("Dropped by beforeSend")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryBeforeSendTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("callbackCalled").toBool(), true);
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::beforeSendCannotCaptureMessage()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool callbackCalled: false
            property bool initialized: false
            property bool closed: false
            property string eventId: ""
            property string nestedEventId: ""
            property string errorMessage: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    callbackCalled = true
                    nestedEventId = Sentry.captureMessage("Nested message")
                    return null
                }
            }

            property Connections sentryConnections: Connections {
                target: Sentry

                function onErrorOccurred(message) {
                    errorMessage = message
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                eventId = Sentry.captureMessage("Message from beforeSend test")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryBeforeSendNestedCaptureTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("callbackCalled").toBool(), true);
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("nestedEventId").toString(), QString());
    QCOMPARE(object->property("errorMessage").toString(),
             QStringLiteral("Sentry.capture* cannot be called from beforeSend or onCrash."));
    QCOMPARE(object->property("closed").toBool(), true);
}

QTEST_MAIN(SentryQmlTest)

#include "tst_sentryqml.moc"
