#include <SentryQml/sentry.h>
#include <SentryQml/sentryintegration.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qtemporarydir.h>
#include <QtQml/qqmlcomponent.h>
#include <QtQml/qqmlengine.h>

#include <memory>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir database;
    if (!database.isValid()) {
        return 1;
    }

#if !defined(SENTRYQML_STATIC)
    QQmlEngine engine;
    engine.addImportPath(qEnvironmentVariable("SENTRY_QML_INSTALLED_IMPORT_PATH"));
    QQmlComponent component(&engine);
    component.setData("import Sentry 1.0\nSentryOptions {}", QUrl());
    const std::unique_ptr<QObject> object(component.create());
    if (!object) {
        qCritical().noquote() << component.errorString();
        return 1;
    }
#endif

    Sentry sentry;
    QObject::connect(&sentry, &Sentry::errorOccurred,
                     [](const QString &message) { qCritical().noquote() << message; });
    SentryOptions options;
    options.setDatabasePath(database.path());
    SentryIntegration integration;
    integration.setName(QStringLiteral("installed-smoke"));
    integration.setRequired(true);
    options.addIntegration(&integration);

    return sentry.init(&options) && sentry.close() ? 0 : 1;
}
