#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qtextstream.h>
#include <QtCore/qurl.h>
#include <QtCore/quuid.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlcontext.h>
#include <QtQml/qqmlengine.h>

#include <csignal>
#include <cstring>
#include <memory>

#if defined(__SANITIZE_ADDRESS__)
#    define SENTRY_QML_E2E_ASAN_ACTIVE 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define SENTRY_QML_E2E_ASAN_ACTIVE 1
#    endif
#endif

QString environmentVariable(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

void printMarker(const QString &name, const QString &value)
{
    QTextStream(stdout) << name << ": " << value << Qt::endl;
}

void printResult(const QString &action, bool success, const QString &eventId = {})
{
    QJsonObject result;
    result.insert(QStringLiteral("action"), action);
    result.insert(QStringLiteral("success"), success);
    if (!eventId.isEmpty()) {
        result.insert(QStringLiteral("event_id"), eventId);
    }
    printMarker(QStringLiteral("TEST_RESULT"),
                QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

void triggerSegfault()
{
#if defined(SENTRY_QML_E2E_ASAN_ACTIVE)
    std::raise(SIGSEGV);
#else
    std::memset(reinterpret_cast<void *>(1), 1, 100);
#endif
}

class CrashActions : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void crash() { triggerSegfault(); }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Sentry"));
    QCoreApplication::setApplicationName(QStringLiteral("Sentry QML E2E App"));

    const QStringList arguments = app.arguments();
    const QString action = arguments.value(1);
    if (action.isEmpty()) {
        qCritical("usage: sentry_qml_e2e_app <message-capture|feedback-capture|crash-capture|crash-send> [crash-id]");
        return 64;
    }

    const QString dsn = environmentVariable("SENTRY_QML_E2E_DSN");
    if (dsn.isEmpty()) {
        qCritical("SENTRY_QML_E2E_DSN is not set.");
        return 65;
    }

    QString runId = environmentVariable("SENTRY_QML_E2E_RUN_ID");
    if (runId.isEmpty()) {
        runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QTemporaryDir fallbackDatabaseDir;
    QString databasePath = environmentVariable("SENTRY_QML_E2E_DATABASE_PATH");
    if (databasePath.isEmpty()) {
        if (!fallbackDatabaseDir.isValid()) {
            qCritical("Could not create a temporary Sentry database directory.");
            return 66;
        }
        databasePath = QDir(fallbackDatabaseDir.path()).filePath(QStringLiteral("sentry"));
    }

    QString crashId = arguments.value(2);
    if (crashId.isEmpty()) {
        crashId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (action == QLatin1String("crash-capture")) {
        printMarker(QStringLiteral("CRASH_ID"), crashId);
    }

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));

    CrashActions crashActions;
    engine.rootContext()->setContextProperty(QStringLiteral("crashActions"), &crashActions);
    engine.rootContext()->setContextProperty(QStringLiteral("testAction"), action);
    engine.rootContext()->setContextProperty(QStringLiteral("testCrashId"), crashId);
    engine.rootContext()->setContextProperty(QStringLiteral("testDatabasePath"), databasePath);
    engine.rootContext()->setContextProperty(QStringLiteral("testDsn"), dsn);
    engine.rootContext()->setContextProperty(QStringLiteral("testRunId"), runId);

    const QUrl fixtureUrl =
        QUrl::fromLocalFile(QDir(QStringLiteral(SENTRY_QML_E2E_QML_DIR)).filePath(QStringLiteral("E2EApp.qml")));
    QQmlComponent component(&engine, fixtureUrl);
    if (component.isError()) {
        qCritical().noquote() << component.errorString();
        return 67;
    }

    const std::unique_ptr<QObject> object(component.create());
    if (!object) {
        qCritical().noquote() << component.errorString();
        return 68;
    }

    if (action == QLatin1String("message-capture") || action == QLatin1String("feedback-capture")) {
        const QString eventId = object->property("eventId").toString();
        const bool success = object->property("success").toBool() && !eventId.isEmpty();
        if (success) {
            printMarker(QStringLiteral("EVENT_CAPTURED"), eventId);
        }
        printResult(action, success, eventId);
        return success ? 0 : 1;
    }

    if (action == QLatin1String("crash-send")) {
        const bool success = object->property("success").toBool();
        printResult(action, success);
        return success ? 0 : 1;
    }

    if (action == QLatin1String("crash-capture")) {
        printResult(action, false);
        return 69;
    }

    qCritical().noquote() << "Unknown E2E action:" << action;
    return 70;
}

#include "e2e_app.moc"
