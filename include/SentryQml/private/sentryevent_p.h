#pragma once

#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>
#include <QtQml/qjsengine.h>

class QQmlError;
union sentry_value_u;
typedef sentry_value_u sentry_value_t;
struct sentry_uuid_s;
typedef sentry_uuid_s sentry_uuid_t;

namespace SentryEvent {

sentry_value_t fromVariant(const QVariant &value);
sentry_value_t attributeFromVariant(const QVariant &value);
sentry_value_t attributesFromVariantMap(const QVariantMap &attributes);
QJSValue toScriptValue(QJSEngine *engine, sentry_value_t event);
int levelFromString(const QString &level);
sentry_value_t stacktraceFromQmlStack(const QString &stack);
sentry_value_t stacktraceFromQmlError(const QQmlError &error, const QStringList &stack = {});
sentry_value_t exceptionEvent(const QString &type,
                              const QString &value,
                              sentry_value_t stacktrace,
                              const QString &rawStack,
                              bool handled);
QString eventIdFromUuid(const sentry_uuid_t &uuid);

} // namespace SentryEvent
