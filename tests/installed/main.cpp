#include <SentryQml/sentry.h>
#include <SentryQml/sentryintegration.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qtemporarydir.h>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir database;
    if (!database.isValid()) {
        return 1;
    }

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
