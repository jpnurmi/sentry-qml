#include <QtCore/qcoreapplication.h>
#include <QtCore/qobject.h>
#include <QtGui/qguiapplication.h>
#include <QtQml/qqmlapplicationengine.h>
#include <QtQml/qqmlcontext.h>

#include <csignal>
#include <cstring>

#if defined(__SANITIZE_ADDRESS__)
#    define SENTRY_QML_EXAMPLE_ASAN_ACTIVE 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define SENTRY_QML_EXAMPLE_ASAN_ACTIVE 1
#    endif
#endif

namespace {

void triggerCrash()
{
#if defined(SENTRY_QML_EXAMPLE_ASAN_ACTIVE)
    std::raise(SIGSEGV);
#else
    static void *invalidMemory = reinterpret_cast<void *>(1);
    std::memset(static_cast<char *>(invalidMemory), 1, 100);
#endif
}

class ExampleActions : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void crash()
    {
        triggerCrash();
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Sentry"));
    QCoreApplication::setApplicationName(QStringLiteral("Sentry QML Example"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(SENTRY_QML_IMPORT_PATH));

    ExampleActions exampleActions;
    engine.rootContext()->setContextProperty(QStringLiteral("exampleActions"), &exampleActions);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []
        { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("SentryExample"), QStringLiteral("Main"));

    return app.exec();
}

#include "main.moc"
