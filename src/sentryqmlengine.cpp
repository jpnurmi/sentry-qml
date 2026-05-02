#include <SentryQml/private/sentryqmlengine_p.h>

#include <SentryQml/private/sentryevent_p.h>

#include <SentryQml/sentry.h>

#include <SentryQml/private/sentrysdk_p.h>

#include <QtCore/qlist.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlerror.h>

#if defined(SENTRY_QML_HAS_QML_PRIVATE)
#    include <QtQml/private/qqmlengine_p.h>
#    include <QtQml/private/qv4debugging_p.h>
#    include <QtQml/private/qv4engine_p.h>
#endif

namespace {

constexpr qsizetype MaxPendingStackTraces = 32;

bool parseStackFrameLocation(const QString &frame, QString *fileName, int *lineNumber, int *columnNumber)
{
    QString location = frame;
    const int atSeparator = location.indexOf(QLatin1Char('@'));
    if (atSeparator >= 0) {
        location = location.mid(atSeparator + 1);
    }

    const int columnSeparator = location.lastIndexOf(QLatin1Char(':'));
    if (columnSeparator <= 0) {
        return false;
    }

    bool ok = false;
    const int parsedColumn = location.mid(columnSeparator + 1).toInt(&ok);
    if (!ok) {
        return false;
    }

    const int lineSeparator = location.lastIndexOf(QLatin1Char(':'), columnSeparator - 1);
    if (lineSeparator <= 0) {
        return false;
    }

    const int parsedLine = location.mid(lineSeparator + 1, columnSeparator - lineSeparator - 1).toInt(&ok);
    if (!ok) {
        return false;
    }

    *fileName = location.left(lineSeparator);
    *lineNumber = parsedLine;
    *columnNumber = parsedColumn;
    return !fileName->isEmpty();
}

bool stackTraceMatchesError(const QStringList &stackTrace, const QQmlError &error)
{
    if (stackTrace.isEmpty() || !error.isValid()) {
        return false;
    }

    QString fileName;
    int lineNumber = 0;
    int columnNumber = 0;
    if (!parseStackFrameLocation(stackTrace.constFirst(), &fileName, &lineNumber, &columnNumber)) {
        return false;
    }

    if (QUrl(fileName) != error.url() && fileName != error.url().toString()) {
        return false;
    }

    if (lineNumber != qAbs(error.line())) {
        return false;
    }

    return error.column() <= 0 || columnNumber <= 0 || columnNumber == error.column();
}

#if defined(SENTRY_QML_HAS_QML_PRIVATE) && QT_CONFIG(qml_debug)
class SentryQmlDebugger final : public QV4::Debugging::Debugger
{
public:
    SentryQmlDebugger(SentryQmlEngine *qmlEngine, QV4::ExecutionEngine *v4Engine)
        : m_qmlEngine(qmlEngine)
        , m_v4Engine(v4Engine)
    {
    }

    bool pauseAtNextOpportunity() const override { return false; }
    void maybeBreakAtInstruction() override {}
    void enteringFunction() override {}
    void leavingFunction(const QV4::ReturnedValue &) override {}

    void aboutToThrow() override
    {
        if (!m_qmlEngine || !m_v4Engine) {
            return;
        }

        QStringList stackTrace;
        const QV4::StackTrace frames = m_v4Engine->exceptionStackTrace;
        for (const QV4::StackFrame &frame : frames) {
            if (frame.source.isEmpty()) {
                continue;
            }

            stackTrace.append(QStringLiteral("%1@%2:%3:%4")
                                  .arg(frame.function,
                                       frame.source,
                                       QString::number(qAbs(frame.line)),
                                       QString::number(frame.column)));
        }

        m_qmlEngine->rememberThrownStackTrace(stackTrace);
    }

private:
    QPointer<SentryQmlEngine> m_qmlEngine;
    QV4::ExecutionEngine *m_v4Engine = nullptr;
};
#endif

} // namespace

SentryQmlEngine::SentryQmlEngine(QQmlEngine *engine, Sentry *sentry)
    : QObject(sentry)
    , m_engine(engine)
    , m_sentry(sentry)
{
}

SentryQmlEngine::~SentryQmlEngine()
{
    uninstallWarningHandler();
}

void SentryQmlEngine::installWarningHandler()
{
    if (!m_engine || m_warningsConnection) {
        return;
    }

    installThrowHook();

    m_warningsConnection = QObject::connect(
        m_engine,
        &QQmlEngine::warnings,
        this,
        [this](const QList<QQmlError> &warnings)
        {
            for (const QQmlError &warning : warnings) {
                if (!warning.isValid()) {
                    continue;
                }

                const QStringList stackTrace = takeThrownStackTrace(warning);
                const QVariantMap event =
                    SentryEvent::exceptionEvent(QStringLiteral("QmlError"),
                                                warning.description(),
                                                SentryEvent::stacktraceFromQmlError(warning, stackTrace),
                                                stackTrace.join(QLatin1Char('\n')),
                                                false);
                SentrySdk::instance()->captureEvent(nullptr, event, SentrySdkCaptureMode::Automatic);
            }
        });
}

void SentryQmlEngine::uninstallWarningHandler()
{
    QObject::disconnect(m_warningsConnection);
    m_warningsConnection = {};
}

void SentryQmlEngine::rememberThrownStackTrace(const QStringList &stackTrace)
{
    if (stackTrace.isEmpty()) {
        return;
    }

    m_pendingStackTraces.append(stackTrace);
    while (m_pendingStackTraces.size() > MaxPendingStackTraces) {
        m_pendingStackTraces.removeFirst();
    }
}

QString SentryQmlEngine::captureException(const QJSValue &exception)
{
    QString type = QStringLiteral("Error");
    QString value = exception.toString();
    QString stack;

    if (exception.isObject()) {
        const QJSValue nameValue = exception.property(QStringLiteral("name"));
        if (nameValue.isString()) {
            type = nameValue.toString();
        }

        const QJSValue messageValue = exception.property(QStringLiteral("message"));
        if (messageValue.isString()) {
            value = messageValue.toString();
        }

        const QJSValue stackValue = exception.property(QStringLiteral("stack"));
        if (stackValue.isString()) {
            stack = stackValue.toString();
        }
    }

    const QVariantMap event = SentryEvent::exceptionEvent(
        type, value, SentryEvent::stacktraceFromQmlStack(stack), stack, true);
    return SentrySdk::instance()->captureEvent(m_sentry, event, SentrySdkCaptureMode::Manual);
}

void SentryQmlEngine::installThrowHook()
{
#if defined(SENTRY_QML_HAS_QML_PRIVATE) && QT_CONFIG(qml_debug)
    QQmlEnginePrivate *enginePrivate = m_engine ? QQmlEnginePrivate::get(m_engine) : nullptr;
    if (!enginePrivate) {
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    QV4::ExecutionEngine *v4Engine = enginePrivate->v4Engine.get();
#else
    QV4::ExecutionEngine *v4Engine = QQmlEnginePrivate::getV4Engine(m_engine);
#endif
    if (!v4Engine) {
        return;
    }

    if (v4Engine->debugger()) {
        return;
    }

    v4Engine->setDebugger(new SentryQmlDebugger(this, v4Engine));
#endif
}

QStringList SentryQmlEngine::takeThrownStackTrace(const QQmlError &error)
{
    for (qsizetype i = 0; i < m_pendingStackTraces.size(); ++i) {
        if (stackTraceMatchesError(m_pendingStackTraces.at(i), error)) {
            return m_pendingStackTraces.takeAt(i);
        }
    }

    return {};
}
