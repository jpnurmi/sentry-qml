#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qurl.h>
#include <QtCore/quuid.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>
#include <QtTest/qtest.h>

#include <memory>

class SentryQmlE2ETest : public QObject
{
    Q_OBJECT

private slots:
    void capturesMessageWithRealSentry();
};

QString environmentVariable(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

void SentryQmlE2ETest::capturesMessageWithRealSentry()
{
    const QString dsn = environmentVariable("SENTRY_QML_E2E_DSN");
    if (dsn.isEmpty()) {
        QSKIP("SENTRY_QML_E2E_DSN is not set.");
    }

    QString runId = environmentVariable("SENTRY_QML_E2E_RUN_ID");
    if (runId.isEmpty()) {
        runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));
    engine.rootContext()->setContextProperty(QStringLiteral("testDsn"), dsn);
    engine.rootContext()->setContextProperty(QStringLiteral("testRunId"), runId);
    engine.rootContext()->setContextProperty(
        QStringLiteral("testDatabasePath"), QDir(temporaryDir.path()).filePath(QStringLiteral("sentry")));

    const QUrl fixtureUrl =
        QUrl::fromLocalFile(QDir(QStringLiteral(SENTRY_QML_TEST_QML_DIR)).filePath(QStringLiteral("E2ETest.qml")));
    QQmlComponent component(&engine, fixtureUrl);

    if (component.isLoading()) {
        QTRY_VERIFY_WITH_TIMEOUT(!component.isLoading(), 5000);
    }
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    const std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("initialized").toBool(), true);
    QCOMPARE(object->property("flushed").toBool(), true);
    QCOMPARE(object->property("closed").toBool(), true);

    const QString eventId = object->property("eventId").toString();
    const QString message = object->property("message").toString();
    QVERIFY2(!eventId.isEmpty(), "Expected Sentry to return an event ID.");

    const QString eventFilePath = environmentVariable("SENTRY_QML_E2E_EVENT_FILE");
    if (!eventFilePath.isEmpty()) {
        QJsonObject result;
        result.insert(QStringLiteral("event_id"), eventId);
        result.insert(QStringLiteral("message"), message);
        result.insert(QStringLiteral("run_id"), runId);

        QFile eventFile(eventFilePath);
        QVERIFY2(eventFile.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(eventFile.errorString()));
        eventFile.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
        eventFile.write("\n");
    }

    qInfo().noquote() << "Sentry QML E2E event:" << eventId << message;
}

QTEST_MAIN(SentryQmlE2ETest)

#include "tst_e2e.moc"
