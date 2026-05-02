#pragma once

#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>
#include <QtQml/qjsengine.h>

class QQmlError;

namespace SentryEvent {

QJSValue toScriptValue(QJSEngine *engine, const QVariant &value);
QString levelNameFromString(const QString &level);
QVariantMap stacktraceFromQmlStack(const QString &stack);
QVariantMap stacktraceFromQmlError(const QQmlError &error, const QStringList &stack = {});
QVariantMap exceptionEvent(const QString &type,
                           const QString &value,
                           const QVariantMap &stacktrace,
                           const QString &rawStack,
                           bool handled);

} // namespace SentryEvent
