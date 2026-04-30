#include <SentryQml/sentry.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qdir.h>
#include <QtCore/qtemporarydir.h>
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
    void capturesManualException();
    void capturesUncaughtQmlError();
    void beforeSendCanDropMessage();
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

QTEST_MAIN(SentryQmlTest)

#include "tst_sentryqml.moc"
