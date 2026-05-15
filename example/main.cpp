#include <QtGui/qguiapplication.h>
#include <QtQml/qqmlapplicationengine.h>
#include <QtQuickControls2/qquickstyle.h>

#include <csignal>

static void triggerCrash()
{
    std::raise(SIGSEGV);
}

class Native : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void crash()
    {
        triggerCrash();
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Violet"));

    QCoreApplication::setOrganizationName(QStringLiteral("Sentry"));
    QCoreApplication::setApplicationName(QStringLiteral("Sentry QML Example"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(":/"));
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));

    qmlRegisterSingletonType<Native>("SentryQmlExample", 1, 0, "Native", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
        return new Native(engine);
    });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("SentryQmlExample"), QStringLiteral("Main"));

    return app.exec();
}

#include "main.moc"
