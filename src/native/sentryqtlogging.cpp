extern "C" {
#include <sentry.h>

void sentry__logger_log(sentry_level_t level, const char *message, ...);
}

#include <QtCore/qbytearray.h>
#include <QtCore/qconstructormacros.h>
#include <QtCore/qlogging.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qstring.h>

namespace {

sentry_level_t sentryLevelForMessage(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return SENTRY_LEVEL_DEBUG;
    case QtInfoMsg:
        return SENTRY_LEVEL_INFO;
    case QtWarningMsg:
        return SENTRY_LEVEL_WARNING;
    case QtCriticalMsg:
        return SENTRY_LEVEL_ERROR;
    case QtFatalMsg:
        return SENTRY_LEVEL_FATAL;
    }

    return SENTRY_LEVEL_DEBUG;
}

void sentryQtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static thread_local bool reentry = false;
    if (!reentry) {
        const QScopedValueRollback<bool> rollback(reentry, true);
        const QByteArray formatted = qFormatLogMessage(type, context, message).toUtf8();
        sentry__logger_log(sentryLevelForMessage(type), "%s", formatted.constData());
    }
}

class SentryQtMessageHandler
{
public:
    SentryQtMessageHandler()
        : m_previousMessageHandler(qInstallMessageHandler(sentryQtMessageHandler))
    {
    }

    ~SentryQtMessageHandler()
    {
        qInstallMessageHandler(m_previousMessageHandler);
    }

private:
    QtMessageHandler m_previousMessageHandler = nullptr;
};

void installSentryQtMessageHandler()
{
    static const SentryQtMessageHandler messageHandler;
}

} // namespace

Q_CONSTRUCTOR_FUNCTION(installSentryQtMessageHandler)
