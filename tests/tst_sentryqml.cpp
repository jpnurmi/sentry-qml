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
    void addsBreadcrumbs();
    void sendsLogs();
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
            property bool levelsReady: Sentry.Trace === -2
                && Sentry.Debug === -1
                && Sentry.Info === 0
                && Sentry.Warning === 1
                && Sentry.Error === 2
                && Sentry.Fatal === 3

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
    QCOMPARE(object->property("levelsReady").toBool(), true);
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

void SentryQmlTest::addsBreadcrumbs()
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
            property bool firstAdded: false
            property bool secondAdded: false
            property bool beforeBreadcrumbCalled: false
            property bool beforeSendCalled: false
            property bool closed: false
            property int breadcrumbCount: -1
            property string breadcrumbMessage: ""
            property string breadcrumbCategory: ""
            property string breadcrumbType: ""
            property string breadcrumbLevel: ""
            property string breadcrumbScreen: ""
            property bool breadcrumbTouched: false
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                maxBreadcrumbs: 10
                shutdownTimeout: 2000
                beforeBreadcrumb: function(breadcrumb) {
                    beforeBreadcrumbCalled = true
                    breadcrumb.data = breadcrumb.data || {}
                    breadcrumb.data.touched = true
                    return breadcrumb
                }
                beforeSend: function(event) {
                    const values = JSON.parse(JSON.stringify(event.breadcrumbs || []))
                    breadcrumbCount = values.length
                    if (values.length > 0) {
                        const breadcrumb = values[values.length - 1]
                        breadcrumbMessage = breadcrumb.message || ""
                        breadcrumbCategory = breadcrumb.category || ""
                        breadcrumbType = breadcrumb.type || ""
                        breadcrumbLevel = breadcrumb.level || ""
                        breadcrumbScreen = breadcrumb.data && breadcrumb.data.screen
                            ? breadcrumb.data.screen
                            : ""
                        breadcrumbTouched = !!(breadcrumb.data && breadcrumb.data.touched)
                    }
                    beforeSendCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                firstAdded = Sentry.addBreadcrumb({
                    message: "first",
                    category: "ui",
                    type: "default",
                    level: "info"
                })
                secondAdded = Sentry.addBreadcrumb({
                    message: "second",
                    category: "navigation",
                    type: "user",
                    level: "warning",
                    data: { screen: "settings" }
                })
                eventId = Sentry.captureMessage("Breadcrumb event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryBreadcrumbTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("firstAdded").toBool(), true);
    QCOMPARE(object->property("secondAdded").toBool(), true);
    QCOMPARE(object->property("beforeBreadcrumbCalled").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("breadcrumbCount").toInt(), 2);
    QCOMPARE(object->property("breadcrumbMessage").toString(), QStringLiteral("second"));
    QCOMPARE(object->property("breadcrumbCategory").toString(), QStringLiteral("navigation"));
    QCOMPARE(object->property("breadcrumbType").toString(), QStringLiteral("user"));
    QCOMPARE(object->property("breadcrumbLevel").toString(), QStringLiteral("warning"));
    QCOMPARE(object->property("breadcrumbScreen").toString(), QStringLiteral("settings"));
    QCOMPARE(object->property("breadcrumbTouched").toBool(), true);
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::sendsLogs()
{
    EnvelopeServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDsn"), QStringLiteral("http://public@127.0.0.1:%1/42").arg(server.serverPort()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool initialized: false
            property bool beforeSendLogCalled: false
            property bool logged: false
            property bool loggedWithLevel: false
            property bool closed: false
            property SentryOptions options: SentryOptions {
                dsn: testDsn
                databasePath: testDatabasePath
                enableLogs: true
                shutdownTimeout: 2000
                beforeSendLog: function(log) {
                    beforeSendLogCalled = true
                    log.body = log.body + " through beforeSendLog"
                    log.attributes["qml.test.hook"] = {
                        type: "boolean",
                        value: true
                    }
                    return log
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                logged = Sentry.warn("Structured QML log", {
                    "qml.test.screen": "settings",
                    "qml.test.duration": {
                        value: 12.5,
                        unit: "millisecond"
                    }
                })
                loggedWithLevel = Sentry.log(Sentry.Info, "Structured QML log with enum level", {
                    "qml.test.screen": "details"
                })
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryLogTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("beforeSendLogCalled").toBool(), true);
    QCOMPARE(object->property("logged").toBool(), true);
    QCOMPARE(object->property("loggedWithLevel").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(server.receivedRequest(), 5000);
    QVERIFY(server.body().contains("Structured QML log through beforeSendLog"));
    QVERIFY(server.body().contains("Structured QML log with enum level through beforeSendLog"));
    QVERIFY(server.body().contains("qml.test.screen"));
    QVERIFY(server.body().contains("settings"));
    QVERIFY(server.body().contains("qml.test.duration"));
    QVERIFY(server.body().contains("millisecond"));
    QVERIFY(server.body().contains("qml.test.hook"));
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
             QStringLiteral("Sentry.capture* cannot be called from Sentry event hooks."));
    QCOMPARE(object->property("closed").toBool(), true);
}

QTEST_MAIN(SentryQmlTest)

#include "tst_sentryqml.moc"
