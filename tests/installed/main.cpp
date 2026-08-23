#include <SentryQml/sentry.h>
#include <SentryQml/sentryintegration.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qtemporarydir.h>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir database;
    if (!database.isValid()) {
        return 1;
    }

    Sentry sentry;
    SentryOptions options;
    options.setDatabasePath(database.path());
    SentryIntegration integration;
    integration.setId(QStringLiteral("installed-smoke"));
    integration.setRequired(true);
    options.addIntegration(&integration);

    return sentry.init(&options) && sentry.close() ? 0 : 1;
}
