#include <SentryQml/private/sentryevent_p.h>

#include <include/sentry.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlist.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qurl.h>
#include <QtQml/qqmlerror.h>

#include <cstring>

namespace {

bool isSupportedInteger(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
        return true;
    default:
        return false;
    }
}

void setStringValue(sentry_value_t object, const char *key, const QString &value)
{
    if (value.isEmpty()) {
        return;
    }

    const QByteArray utf8 = value.toUtf8();
    sentry_value_set_by_key_n(
        object, key, std::strlen(key), sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size())));
}

void setIntValue(sentry_value_t object, const char *key, int value)
{
    if (value <= 0) {
        return;
    }

    sentry_value_set_by_key(object, key, sentry_value_new_int32(value));
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

sentry_value_t stackFrameFromLocation(const QString &functionName,
                                      const QString &fileName,
                                      int lineNumber,
                                      int columnNumber)
{
    sentry_value_t frame = sentry_value_new_object();
    setStringValue(frame, "function", functionName);
    setStringValue(frame, "filename", fileName);
    setStringValue(frame, "abs_path", fileName);
    setStringValue(frame, "platform", QStringLiteral("javascript"));
    setIntValue(frame, "lineno", lineNumber);
    setIntValue(frame, "colno", columnNumber);
    sentry_value_set_by_key(frame, "in_app", sentry_value_new_bool(1));
    return frame;
}

} // namespace

namespace SentryEvent {

sentry_value_t fromVariant(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return sentry_value_new_null();
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return sentry_value_new_bool(value.toBool() ? 1 : 0);
    case QMetaType::LongLong:
        return sentry_value_new_int64(value.toLongLong());
    case QMetaType::ULongLong:
        return sentry_value_new_uint64(value.toULongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return sentry_value_new_double(value.toDouble());
    case QMetaType::QString: {
        const QByteArray utf8 = value.toString().toUtf8();
        return sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size()));
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        sentry_value_t list = sentry_value_new_list();
        const QVariantList values = value.toList();
        for (const QVariant &item : values) {
            sentry_value_append(list, fromVariant(item));
        }
        return list;
    }
    case QMetaType::QVariantMap: {
        sentry_value_t object = sentry_value_new_object();
        const QVariantMap values = value.toMap();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            const QByteArray key = it.key().toUtf8();
            sentry_value_set_by_key_n(
                object, key.constData(), static_cast<size_t>(key.size()), fromVariant(it.value()));
        }
        return object;
    }
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return sentry_value_new_int64(value.toLongLong());
    }

    const QByteArray utf8 = value.toString().toUtf8();
    return sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size()));
}

QJSValue toScriptValue(QJSEngine *engine, sentry_value_t event)
{
    if (!engine) {
        return {};
    }

    char *json = sentry_value_to_json(event);
    if (!json) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json), &error);
    sentry_free(json);

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    return engine->toScriptValue(document.object());
}

int levelFromString(const QString &level)
{
    const QString normalized = level.trimmed().toLower();
    if (normalized == QLatin1String("trace")) {
        return SENTRY_LEVEL_TRACE;
    }
    if (normalized == QLatin1String("debug")) {
        return SENTRY_LEVEL_DEBUG;
    }
    if (normalized == QLatin1String("warning") || normalized == QLatin1String("warn")) {
        return SENTRY_LEVEL_WARNING;
    }
    if (normalized == QLatin1String("error")) {
        return SENTRY_LEVEL_ERROR;
    }
    if (normalized == QLatin1String("fatal")) {
        return SENTRY_LEVEL_FATAL;
    }
    return SENTRY_LEVEL_INFO;
}

sentry_value_t stacktraceFromQmlStack(const QString &stack)
{
    if (stack.isEmpty()) {
        return sentry_value_new_null();
    }

    QList<sentry_value_t> parsedFrames;
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
        return sentry_value_new_null();
    }

    sentry_value_t frames = sentry_value_new_list();
    for (auto it = parsedFrames.crbegin(); it != parsedFrames.crend(); ++it) {
        sentry_value_append(frames, *it);
    }

    sentry_value_t stacktrace = sentry_value_new_object();
    sentry_value_set_by_key(stacktrace, "frames", frames);
    return stacktrace;
}

sentry_value_t stacktraceFromQmlError(const QQmlError &error, const QStringList &stack)
{
    if (!stack.isEmpty()) {
        sentry_value_t stacktrace = stacktraceFromQmlStack(stack.join(QLatin1Char('\n')));
        if (!sentry_value_is_null(stacktrace)) {
            return stacktrace;
        }
    }

    if (!error.isValid() || error.url().isEmpty()) {
        return sentry_value_new_null();
    }

    sentry_value_t frames = sentry_value_new_list();
    sentry_value_append(frames, stackFrameFromLocation(QString(), error.url().toString(), error.line(), error.column()));

    sentry_value_t stacktrace = sentry_value_new_object();
    sentry_value_set_by_key(stacktrace, "frames", frames);
    return stacktrace;
}

sentry_value_t exceptionEvent(const QString &type,
                              const QString &value,
                              sentry_value_t stacktrace,
                              const QString &rawStack,
                              bool handled)
{
    const QByteArray typeUtf8 = (type.isEmpty() ? QStringLiteral("Error") : type).toUtf8();
    const QByteArray valueUtf8 = value.toUtf8();

    sentry_value_t exception = sentry_value_new_exception_n(
        typeUtf8.constData(),
        static_cast<size_t>(typeUtf8.size()),
        valueUtf8.constData(),
        static_cast<size_t>(valueUtf8.size()));

    if (!sentry_value_is_null(stacktrace)) {
        sentry_value_set_by_key(exception, "stacktrace", stacktrace);
    }

    sentry_value_t mechanism = sentry_value_new_object();
    sentry_value_set_by_key(mechanism, "type", sentry_value_new_string("qml"));
    sentry_value_set_by_key(mechanism, "handled", sentry_value_new_bool(handled ? 1 : 0));
    sentry_value_set_by_key(exception, "mechanism", mechanism);

    sentry_value_t event = sentry_value_new_event();
    sentry_value_set_by_key(event, "platform", sentry_value_new_string("javascript"));
    sentry_value_set_by_key(event, "level", sentry_value_new_string("error"));
    sentry_value_set_by_key(event, "logger", sentry_value_new_string("qml"));
    sentry_event_add_exception(event, exception);

    if (!rawStack.isEmpty()) {
        QVariantMap extra;
        extra.insert(QStringLiteral("qml_stack"), rawStack);
        sentry_value_set_by_key(event, "extra", fromVariant(extra));
    }

    return event;
}

QString eventIdFromUuid(const sentry_uuid_t &uuid)
{
    if (sentry_uuid_is_nil(&uuid)) {
        return {};
    }

    char uuidString[37] = {};
    sentry_uuid_as_string(&uuid, uuidString);
    return QString::fromLatin1(uuidString);
}

} // namespace SentryEvent
