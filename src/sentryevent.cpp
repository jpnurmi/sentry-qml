#include <SentryQml/private/sentryevent_p.h>

#include <QtCore/qlist.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlerror.h>

namespace {

void setStringValue(QVariantMap *object, const QString &key, const QString &value)
{
    if (!value.isEmpty()) {
        object->insert(key, value);
    }
}

void setIntValue(QVariantMap *object, const QString &key, int value)
{
    if (value > 0) {
        object->insert(key, value);
    }
}

bool parseStackLocation(const QString &location, QString *fileName, int *lineNumber, int *columnNumber)
{
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

QVariantMap stackFrameFromLocation(const QString &functionName,
                                   const QString &fileName,
                                   int lineNumber,
                                   int columnNumber)
{
    QVariantMap frame;
    setStringValue(&frame, QStringLiteral("function"), functionName);
    setStringValue(&frame, QStringLiteral("filename"), fileName);
    setStringValue(&frame, QStringLiteral("abs_path"), fileName);
    setStringValue(&frame, QStringLiteral("platform"), QStringLiteral("javascript"));
    setIntValue(&frame, QStringLiteral("lineno"), lineNumber);
    setIntValue(&frame, QStringLiteral("colno"), columnNumber);
    frame.insert(QStringLiteral("in_app"), true);
    return frame;
}

} // namespace

namespace SentryEvent {

QJSValue toScriptValue(QJSEngine *engine, const QVariant &value)
{
    if (!engine) {
        return {};
    }

    return engine->toScriptValue(value);
}

QString levelNameFromString(const QString &level)
{
    const QString normalized = level.trimmed().toLower();
    if (normalized == QLatin1String("trace")) {
        return QStringLiteral("trace");
    }
    if (normalized == QLatin1String("debug")) {
        return QStringLiteral("debug");
    }
    if (normalized == QLatin1String("warning") || normalized == QLatin1String("warn")) {
        return QStringLiteral("warning");
    }
    if (normalized == QLatin1String("error")) {
        return QStringLiteral("error");
    }
    if (normalized == QLatin1String("fatal")) {
        return QStringLiteral("fatal");
    }
    return QStringLiteral("info");
}

QVariantMap stacktraceFromQmlStack(const QString &stack)
{
    if (stack.isEmpty()) {
        return {};
    }

    QList<QVariantMap> parsedFrames;
    const QStringList lines = stack.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1String("at "))) {
            line = line.mid(3).trimmed();
        }

        QString functionName;
        QString location = line;
        const int atSeparator = line.indexOf(QLatin1Char('@'));
        if (atSeparator >= 0) {
            functionName = line.left(atSeparator).trimmed();
            location = line.mid(atSeparator + 1).trimmed();
        } else {
            const int openParen = line.indexOf(QLatin1Char('('));
            const int closeParen = line.lastIndexOf(QLatin1Char(')'));
            if (openParen > 0 && closeParen > openParen) {
                functionName = line.left(openParen).trimmed();
                location = line.mid(openParen + 1, closeParen - openParen - 1).trimmed();
            }
        }

        QString fileName;
        int lineNumber = 0;
        int columnNumber = 0;
        if (parseStackLocation(location, &fileName, &lineNumber, &columnNumber)) {
            parsedFrames.append(stackFrameFromLocation(functionName, fileName, lineNumber, columnNumber));
        }
    }

    if (parsedFrames.isEmpty()) {
        return {};
    }

    QVariantList frames;
    for (auto it = parsedFrames.crbegin(); it != parsedFrames.crend(); ++it) {
        frames.append(*it);
    }

    return {
        {QStringLiteral("frames"), frames},
    };
}

QVariantMap stacktraceFromQmlError(const QQmlError &error, const QStringList &stack)
{
    if (!stack.isEmpty()) {
        const QVariantMap stacktrace = stacktraceFromQmlStack(stack.join(QLatin1Char('\n')));
        if (!stacktrace.isEmpty()) {
            return stacktrace;
        }
    }

    if (!error.isValid() || error.url().isEmpty()) {
        return {};
    }

    return {
        {QStringLiteral("frames"),
         QVariantList{stackFrameFromLocation(QString(), error.url().toString(), error.line(), error.column())}},
    };
}

QVariantMap exceptionEvent(const QString &type,
                           const QString &value,
                           const QVariantMap &stacktrace,
                           const QString &rawStack,
                           bool handled)
{
    QVariantMap mechanism = {
        {QStringLiteral("type"), QStringLiteral("qml")},
        {QStringLiteral("handled"), handled},
    };

    QVariantMap exception = {
        {QStringLiteral("type"), type.isEmpty() ? QStringLiteral("Error") : type},
        {QStringLiteral("value"), value},
        {QStringLiteral("mechanism"), mechanism},
    };

    if (!stacktrace.isEmpty()) {
        exception.insert(QStringLiteral("stacktrace"), stacktrace);
    }

    QVariantMap event = {
        {QStringLiteral("platform"), QStringLiteral("javascript")},
        {QStringLiteral("level"), QStringLiteral("error")},
        {QStringLiteral("logger"), QStringLiteral("qml")},
        {QStringLiteral("exception"),
         QVariantMap{
             {QStringLiteral("values"), QVariantList{exception}},
         }},
    };

    if (!rawStack.isEmpty()) {
        event.insert(QStringLiteral("extra"),
                     QVariantMap{
                         {QStringLiteral("qml_stack"), rawStack},
                     });
    }

    return event;
}

} // namespace SentryEvent
