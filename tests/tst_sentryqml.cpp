#include <SentryQml/sentry.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qmetaobject.h>
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
    void setsRelease();
    void setsEnvironment();
    void setsUser();
    void setsTags();
    void setsContexts();
    void setsFingerprint();
    void attachesFilesAndBytes();
    void tracksSessions();
    void addsBreadcrumbs();
    void sendsLogs();
    void sendsMetrics();
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
            property bool metricsReady: Sentry.Count === 0
                && Sentry.Gauge === 1
                && Sentry.Distribution === 2
            property bool sessionsReady: Sentry.SessionOk === 0
                && Sentry.SessionCrashed === 1
                && Sentry.SessionAbnormal === 2
                && Sentry.SessionExited === 3

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
    QCOMPARE(object->property("metricsReady").toBool(), true);
    QCOMPARE(object->property("sessionsReady").toBool(), true);
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

void SentryQmlTest::setsRelease()
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
            property bool releaseSet: false
            property bool beforeSendCalled: false
            property bool closed: false
            property string capturedRelease: ""
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    capturedRelease = event.release || ""
                    beforeSendCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                releaseSet = Sentry.setRelease("sentry-qml@1.2.3")
                eventId = Sentry.captureMessage("Release event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryReleaseTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("releaseSet").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("capturedRelease").toString(), QStringLiteral("sentry-qml@1.2.3"));
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::setsEnvironment()
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
            property bool environmentSet: false
            property bool beforeSendCalled: false
            property bool closed: false
            property string capturedEnvironment: ""
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    capturedEnvironment = event.environment || ""
                    beforeSendCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                environmentSet = Sentry.setEnvironment("staging")
                eventId = Sentry.captureMessage("Environment event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryEnvironmentTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("environmentSet").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("capturedEnvironment").toString(), QStringLiteral("staging"));
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::setsUser()
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
            property bool userSet: false
            property bool removed: false
            property bool secondHasUser: true
            property bool thirdHasUser: false
            property bool closed: false
            property int beforeSendCount: 0
            property string userId: ""
            property string username: ""
            property string email: ""
            property string ipAddress: ""
            property string role: ""
            property string dynamicUserId: ""
            property string dynamicIpAddress: ""
            property string dynamicRole: ""
            property string firstEventId: ""
            property string secondEventId: ""
            property string thirdEventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                user: SentryUser {
                    userId: "42"
                    username: "ada"
                    email: "ada@example.com"
                    ipAddress: "127.0.0.1"
                    data: ({ "role": "admin" })
                }
                beforeSend: function(event) {
                    beforeSendCount += 1
                    if (beforeSendCount === 1) {
                        const user = event.user || {}
                        userId = user.id || ""
                        username = user.username || ""
                        email = user.email || ""
                        ipAddress = user.ip_address || ""
                        role = user.role || ""
                    } else if (beforeSendCount === 2) {
                        secondHasUser = !!event.user
                    } else if (beforeSendCount === 3) {
                        const user = event.user || {}
                        thirdHasUser = !!event.user
                        dynamicUserId = user.id || ""
                        dynamicIpAddress = user.ip_address || ""
                        dynamicRole = user.role || ""
                    }
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                firstEventId = Sentry.captureMessage("Declarative user event")
                removed = Sentry.removeUser()
                secondEventId = Sentry.captureMessage("No user event")
                userSet = Sentry.setUser({
                    id: "43",
                    ipAddress: "127.0.0.2",
                    role: "operator"
                })
                thirdEventId = Sentry.captureMessage("Imperative user event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryUserTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("userSet").toBool(), true);
    QCOMPARE(object->property("removed").toBool(), true);
    QCOMPARE(object->property("beforeSendCount").toInt(), 3);
    QCOMPARE(object->property("userId").toString(), QStringLiteral("42"));
    QCOMPARE(object->property("username").toString(), QStringLiteral("ada"));
    QCOMPARE(object->property("email").toString(), QStringLiteral("ada@example.com"));
    QCOMPARE(object->property("ipAddress").toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(object->property("role").toString(), QStringLiteral("admin"));
    QCOMPARE(object->property("secondHasUser").toBool(), false);
    QCOMPARE(object->property("thirdHasUser").toBool(), true);
    QCOMPARE(object->property("dynamicUserId").toString(), QStringLiteral("43"));
    QCOMPARE(object->property("dynamicIpAddress").toString(), QStringLiteral("127.0.0.2"));
    QCOMPARE(object->property("dynamicRole").toString(), QStringLiteral("operator"));
    QCOMPARE(object->property("firstEventId").toString(), QString());
    QCOMPARE(object->property("secondEventId").toString(), QString());
    QCOMPARE(object->property("thirdEventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::setsTags()
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
            property bool screenSet: false
            property bool removedSet: false
            property bool removed: false
            property bool beforeSendCalled: false
            property bool closed: false
            property string screenTag: ""
            property string removedTag: ""
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    const tags = event.tags || {}
                    screenTag = tags.screen || ""
                    removedTag = tags.removed || ""
                    beforeSendCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                screenSet = Sentry.setTag("screen", "settings")
                removedSet = Sentry.setTag("removed", "yes")
                removed = Sentry.removeTag("removed")
                eventId = Sentry.captureMessage("Tagged event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryTagsTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("screenSet").toBool(), true);
    QCOMPARE(object->property("removedSet").toBool(), true);
    QCOMPARE(object->property("removed").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("screenTag").toString(), QStringLiteral("settings"));
    QCOMPARE(object->property("removedTag").toString(), QString());
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::setsContexts()
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
            property bool qmlSet: false
            property bool removedSet: false
            property bool removed: false
            property bool beforeSendCalled: false
            property bool closed: false
            property string screen: ""
            property int retries: -1
            property bool nested: false
            property bool removedContextPresent: false
            property string eventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    const contexts = event.contexts || {}
                    const qml = contexts.qml || {}
                    screen = qml.screen || ""
                    retries = qml.retries || 0
                    nested = !!(qml.nested && qml.nested.enabled)
                    removedContextPresent = !!contexts.removed
                    beforeSendCalled = true
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                qmlSet = Sentry.setContext("qml", {
                    screen: "settings",
                    retries: 3,
                    nested: { enabled: true }
                })
                removedSet = Sentry.setContext("removed", { value: true })
                removed = Sentry.removeContext("removed")
                eventId = Sentry.captureMessage("Context event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryContextsTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("qmlSet").toBool(), true);
    QCOMPARE(object->property("removedSet").toBool(), true);
    QCOMPARE(object->property("removed").toBool(), true);
    QCOMPARE(object->property("beforeSendCalled").toBool(), true);
    QCOMPARE(object->property("screen").toString(), QStringLiteral("settings"));
    QCOMPARE(object->property("retries").toInt(), 3);
    QCOMPARE(object->property("nested").toBool(), true);
    QCOMPARE(object->property("removedContextPresent").toBool(), false);
    QCOMPARE(object->property("eventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::setsFingerprint()
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
            property bool fingerprintSet: false
            property bool removed: false
            property bool secondHasFingerprint: true
            property bool closed: false
            property int beforeSendCount: 0
            property int fingerprintCount: -1
            property string defaultFingerprint: ""
            property string customFingerprint: ""
            property string lastFingerprint: ""
            property string firstEventId: ""
            property string secondEventId: ""
            property SentryOptions options: SentryOptions {
                databasePath: testDatabasePath
                shutdownTimeout: 2000
                beforeSend: function(event) {
                    beforeSendCount += 1
                    if (beforeSendCount === 1) {
                        const fingerprint = event.fingerprint || []
                        fingerprintCount = fingerprint.length
                        defaultFingerprint = fingerprint[0] || ""
                        customFingerprint = fingerprint[1] || ""
                        lastFingerprint = fingerprint[fingerprint.length - 1] || ""
                    } else if (beforeSendCount === 2) {
                        secondHasFingerprint = !!event.fingerprint
                    }
                    return null
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                fingerprintSet = Sentry.setFingerprint([
                    "{{ default }}",
                    "qml",
                    "part-3",
                    "part-4",
                    "part-5",
                    "part-6",
                    "part-7",
                    "part-8",
                    "part-9",
                    "part-10",
                    "part-11"
                ])
                firstEventId = Sentry.captureMessage("Fingerprint event")
                removed = Sentry.removeFingerprint()
                secondEventId = Sentry.captureMessage("No fingerprint event")
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryFingerprintTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("fingerprintSet").toBool(), true);
    QCOMPARE(object->property("removed").toBool(), true);
    QCOMPARE(object->property("beforeSendCount").toInt(), 2);
    QCOMPARE(object->property("fingerprintCount").toInt(), 11);
    QCOMPARE(object->property("defaultFingerprint").toString(), QStringLiteral("{{ default }}"));
    QCOMPARE(object->property("customFingerprint").toString(), QStringLiteral("qml"));
    QCOMPARE(object->property("lastFingerprint").toString(), QStringLiteral("part-11"));
    QCOMPARE(object->property("secondHasFingerprint").toBool(), false);
    QCOMPARE(object->property("firstEventId").toString(), QString());
    QCOMPARE(object->property("secondEventId").toString(), QString());
    QCOMPARE(object->property("closed").toBool(), true);
}

void SentryQmlTest::attachesFilesAndBytes()
{
    EnvelopeServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString attachmentPath = QDir(temporaryDir.path()).filePath(QStringLiteral("diagnostic.log"));
    QFile attachmentFile(attachmentPath);
    QVERIFY(attachmentFile.open(QIODevice::WriteOnly));
    QCOMPARE(attachmentFile.write("file attachment payload"), qint64(23));
    attachmentFile.close();

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDsn"), QStringLiteral("http://public@127.0.0.1:%1/42").arg(server.serverPort()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));
    engine.rootContext()->setContextProperty(QStringLiteral("testAttachmentPath"), attachmentPath);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import Sentry 1.0

        QtObject {
            property bool initialized: false
            property bool removedAttached: false
            property bool removed: false
            property bool removedInvalidated: false
            property bool clearedAttached: false
            property bool cleared: false
            property bool clearedInvalidated: false
            property bool fileAttached: false
            property bool bytesAttached: false
            property bool fileInvalidated: false
            property bool bytesInvalidated: false
            property bool flushed: false
            property bool closed: false
            property string eventId: ""
            property var removedAttachment: null
            property var clearedAttachment: null
            property var fileAttachment: null
            property var bytesAttachment: null
            property SentryOptions options: SentryOptions {
                dsn: testDsn
                databasePath: testDatabasePath
                autoSessionTracking: false
                shutdownTimeout: 2000
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                removedAttachment = Sentry.attachBytes("removed attachment payload", "removed.txt", "text/plain")
                removedAttached = !!removedAttachment && removedAttachment.valid
                removed = Sentry.removeAttachment(removedAttachment)
                removedInvalidated = !!removedAttachment && !removedAttachment.valid
                clearedAttachment = Sentry.attachBytes("cleared attachment payload", "cleared.txt", "text/plain")
                clearedAttached = !!clearedAttachment && clearedAttachment.valid
                cleared = Sentry.clearAttachments()
                clearedInvalidated = !!clearedAttachment && !clearedAttachment.valid
                fileAttachment = Sentry.attachFile(testAttachmentPath, "text/plain")
                fileAttached = !!fileAttachment && fileAttachment.valid
                bytesAttachment = Sentry.attachBytes("inline attachment payload", "inline.tmp", "application/octet-stream")
                bytesAttachment.filename = "inline.txt"
                bytesAttachment.contentType = "text/plain"
                bytesAttached = !!bytesAttachment && bytesAttachment.valid
                eventId = Sentry.captureMessage("Attachment event")
                flushed = Sentry.flush(2000)
                closed = Sentry.close()
                fileInvalidated = !!fileAttachment && !fileAttachment.valid
                bytesInvalidated = !!bytesAttachment && !bytesAttachment.valid
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryAttachmentTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("removedAttached").toBool(), true);
    QCOMPARE(object->property("removed").toBool(), true);
    QCOMPARE(object->property("removedInvalidated").toBool(), true);
    QCOMPARE(object->property("clearedAttached").toBool(), true);
    QCOMPARE(object->property("cleared").toBool(), true);
    QCOMPARE(object->property("clearedInvalidated").toBool(), true);
    QCOMPARE(object->property("fileAttached").toBool(), true);
    QCOMPARE(object->property("bytesAttached").toBool(), true);
    QCOMPARE(object->property("eventId").toString().size(), 36);
    QCOMPARE(object->property("flushed").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);
    QCOMPARE(object->property("fileInvalidated").toBool(), true);
    QCOMPARE(object->property("bytesInvalidated").toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(server.receivedRequest(), 5000);
    const QByteArray body = server.body();
    QVERIFY(body.contains("Attachment event"));
    QVERIFY(body.contains("\"type\":\"attachment\""));
    QVERIFY(body.contains("\"filename\":\"diagnostic.log\""));
    QVERIFY(body.contains("\"filename\":\"inline.txt\""));
    QVERIFY(body.contains("\"content_type\":\"text/plain\""));
    QVERIFY(body.contains("file attachment payload"));
    QVERIFY(body.contains("inline attachment payload"));
    QVERIFY(!body.contains("removed.txt"));
    QVERIFY(!body.contains("removed attachment payload"));
    QVERIFY(!body.contains("cleared.txt"));
    QVERIFY(!body.contains("cleared attachment payload"));
}

void SentryQmlTest::tracksSessions()
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
            property bool started: false
            property bool ended: false
            property bool flushed: false
            property bool closed: false
            property SentryOptions options: SentryOptions {
                dsn: testDsn
                databasePath: testDatabasePath
                autoSessionTracking: false
                release: "sentry-qml@1.2.3"
                environment: "test"
                shutdownTimeout: 2000
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                started = Sentry.startSession()
                ended = Sentry.endSession(Sentry.SessionExited)
            }

            function finish() {
                flushed = Sentry.flush(2000)
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentrySessionTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("started").toBool(), true);
    QCOMPARE(object->property("ended").toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(server.body().contains("\"type\":\"session\""), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(server.body().contains("\"status\":\"exited\""), 5000);
    QVERIFY(server.body().contains("\"release\":\"sentry-qml@1.2.3\""));
    QVERIFY(server.body().contains("\"environment\":\"test\""));
    QVERIFY(QMetaObject::invokeMethod(object.get(), "finish"));
    QCOMPARE(object->property("flushed").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);
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

void SentryQmlTest::sendsMetrics()
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
            property bool beforeSendMetricCalled: false
            property bool genericMetric: false
            property bool counted: false
            property bool gauged: false
            property bool distributed: false
            property bool closed: false
            property SentryOptions options: SentryOptions {
                dsn: testDsn
                databasePath: testDatabasePath
                enableMetrics: true
                shutdownTimeout: 2000
                beforeSendMetric: function(metric) {
                    beforeSendMetricCalled = true
                    metric.attributes["qml.test.hook"] = {
                        type: "boolean",
                        value: true
                    }
                    return metric
                }
            }

            Component.onCompleted: {
                initialized = Sentry.init(options)
                genericMetric = Sentry.metric(Sentry.Count, "qml.test.generic", 2, "", {
                    "qml.test.screen": "settings"
                })
                counted = Sentry.count("qml.test.clicks", 3, {
                    "qml.test.screen": "settings"
                })
                gauged = Sentry.gauge("qml.test.active_items", 4, "item", {
                    "qml.test.screen": "settings"
                })
                distributed = Sentry.distribution("qml.test.duration", 12.5, "millisecond", {
                    "qml.test.operation": "load"
                })
                closed = Sentry.close()
            }
        }
    )", QUrl(QStringLiteral("memory:/SentryMetricTest.qml")));

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("beforeSendMetricCalled").toBool(), true);
    QCOMPARE(object->property("genericMetric").toBool(), true);
    QCOMPARE(object->property("counted").toBool(), true);
    QCOMPARE(object->property("gauged").toBool(), true);
    QCOMPARE(object->property("distributed").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);

    QTRY_VERIFY_WITH_TIMEOUT(server.receivedRequest(), 5000);
    QVERIFY(server.body().contains("trace_metric"));
    QVERIFY(server.body().contains("qml.test.generic"));
    QVERIFY(server.body().contains("qml.test.clicks"));
    QVERIFY(server.body().contains("counter"));
    QVERIFY(server.body().contains("qml.test.active_items"));
    QVERIFY(server.body().contains("gauge"));
    QVERIFY(server.body().contains("item"));
    QVERIFY(server.body().contains("qml.test.duration"));
    QVERIFY(server.body().contains("distribution"));
    QVERIFY(server.body().contains("millisecond"));
    QVERIFY(server.body().contains("qml.test.operation"));
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
