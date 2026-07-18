#include "sentryobjcbridge_p.h"

#import <SentryObjC/SentryObjC.h>

#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qurl.h>

#include <cmath>
#include <cstring>

namespace {

QString currentRelease;

NSString *nsString(const QString &value)
{
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

NSString *nsStringOrNil(const QString &value)
{
    return value.isEmpty() ? nil : nsString(value);
}

QString qtString(NSString *value)
{
    return value ? QString::fromUtf8(value.UTF8String) : QString();
}

QString qtSentryIdString(SentryObjCId *eventId)
{
    QString value = qtString(eventId.sentryIdString);
    if (value.size() == 32) {
        value.insert(20, QLatin1Char('-'));
        value.insert(16, QLatin1Char('-'));
        value.insert(12, QLatin1Char('-'));
        value.insert(8, QLatin1Char('-'));
    }
    return value;
}

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
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return true;
    default:
        return false;
    }
}

id objectFromVariant(const QVariant &value);

NSDictionary<NSString *, id> *dictionaryFromVariantMap(const QVariantMap &map)
{
    NSMutableDictionary<NSString *, id> *dictionary = [NSMutableDictionary dictionaryWithCapacity:map.size()];
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        dictionary[nsString(it.key())] = objectFromVariant(it.value()) ?: NSNull.null;
    }
    return dictionary;
}

NSArray *arrayFromVariantList(const QVariantList &list)
{
    NSMutableArray *array = [NSMutableArray arrayWithCapacity:list.size()];
    for (const QVariant &item : list) {
        [array addObject:objectFromVariant(item) ?: NSNull.null];
    }
    return array;
}

id objectFromVariant(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return NSNull.null;
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return @(value.toBool());
    case QMetaType::Float:
    case QMetaType::Double:
        return @(value.toDouble());
    case QMetaType::QString:
        return nsString(value.toString());
    case QMetaType::QVariantList:
        return arrayFromVariantList(value.toList());
    case QMetaType::QStringList: {
        NSMutableArray<NSString *> *array = [NSMutableArray arrayWithCapacity:value.toStringList().size()];
        for (const QString &item : value.toStringList()) {
            [array addObject:nsString(item)];
        }
        return array;
    }
    case QMetaType::QVariantMap:
        return dictionaryFromVariantMap(value.toMap());
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return @(value.toLongLong());
    }

    return nsString(value.toString());
}

QVariant variantFromObject(id object);

QVariantMap variantMapFromDictionary(NSDictionary *dictionary)
{
    QVariantMap map;
    for (id key in dictionary) {
        if (![key isKindOfClass:NSString.class]) {
            continue;
        }
        map.insert(qtString(key), variantFromObject(dictionary[key]));
    }
    return map;
}

QVariantList variantListFromArray(NSArray *array)
{
    QVariantList list;
    for (id item in array) {
        list.append(variantFromObject(item));
    }
    return list;
}

QVariant variantFromObject(id object)
{
    if (!object || object == NSNull.null) {
        return {};
    }
    if ([object isKindOfClass:NSString.class]) {
        return qtString(object);
    }
    if ([object isKindOfClass:NSNumber.class]) {
        const char *type = [object objCType];
        if (std::strcmp(type, @encode(BOOL)) == 0) {
            return QVariant(static_cast<bool>([object boolValue]));
        }
        if (std::strcmp(type, @encode(float)) == 0 || std::strcmp(type, @encode(double)) == 0) {
            return QVariant([object doubleValue]);
        }
        return QVariant::fromValue([object longLongValue]);
    }
    if ([object isKindOfClass:NSArray.class]) {
        return variantListFromArray(object);
    }
    if ([object isKindOfClass:NSDictionary.class]) {
        return variantMapFromDictionary(object);
    }
    if ([object respondsToSelector:@selector(serialize)]) {
        id serialized = [object serialize];
        return variantFromObject(serialized);
    }
    return qtString([object description]);
}

NSString *stringValue(const QVariantMap &map, const QString &key, const QString &fallback = {})
{
    const QString value = map.value(key, fallback).toString();
    return value.isEmpty() ? nil : nsString(value);
}

NSString *feedbackValue(const QVariantMap &feedback,
                        const QString &key,
                        const QString &fallbackKey = {},
                        const QString &secondFallbackKey = {})
{
    if (feedback.contains(key)) {
        return nsStringOrNil(feedback.value(key).toString());
    }
    if (!fallbackKey.isEmpty() && feedback.contains(fallbackKey)) {
        return nsStringOrNil(feedback.value(fallbackKey).toString());
    }
    if (!secondFallbackKey.isEmpty() && feedback.contains(secondFallbackKey)) {
        return nsStringOrNil(feedback.value(secondFallbackKey).toString());
    }
    return nil;
}

SentryObjCLevel levelFromString(const QString &level)
{
    const QString normalized = level.trimmed().toLower();
    if (normalized == QLatin1String("debug") || normalized == QLatin1String("trace")) {
        return SentryObjCLevelDebug;
    }
    if (normalized == QLatin1String("warning") || normalized == QLatin1String("warn")) {
        return SentryObjCLevelWarning;
    }
    if (normalized == QLatin1String("error")) {
        return SentryObjCLevelError;
    }
    if (normalized == QLatin1String("fatal")) {
        return SentryObjCLevelFatal;
    }
    return SentryObjCLevelInfo;
}

SentryObjCLogLevel logLevelFromInt(int level)
{
    switch (level) {
    case -2:
        return SentryObjCLogLevelTrace;
    case -1:
        return SentryObjCLogLevelDebug;
    case 1:
        return SentryObjCLogLevelWarn;
    case 2:
        return SentryObjCLogLevelError;
    case 3:
        return SentryObjCLogLevelFatal;
    case 0:
    default:
        return SentryObjCLogLevelInfo;
    }
}

QString logLevelName(SentryObjCLogLevel level)
{
    switch (level) {
    case SentryObjCLogLevelTrace:
        return QStringLiteral("trace");
    case SentryObjCLogLevelDebug:
        return QStringLiteral("debug");
    case SentryObjCLogLevelWarn:
        return QStringLiteral("warning");
    case SentryObjCLogLevelError:
        return QStringLiteral("error");
    case SentryObjCLogLevelFatal:
        return QStringLiteral("fatal");
    case SentryObjCLogLevelInfo:
    default:
        return QStringLiteral("info");
    }
}

QString levelName(SentryObjCLevel level)
{
    switch (level) {
    case SentryObjCLevelDebug:
        return QStringLiteral("debug");
    case SentryObjCLevelWarning:
        return QStringLiteral("warning");
    case SentryObjCLevelError:
        return QStringLiteral("error");
    case SentryObjCLevelFatal:
        return QStringLiteral("fatal");
    case SentryObjCLevelInfo:
    case SentryObjCLevelNone:
    default:
        return QStringLiteral("info");
    }
}

SentryObjCLogLevel logLevelFromVariant(const QVariant &level)
{
    if (level.metaType().id() == QMetaType::QString) {
        const QString normalized = level.toString().trimmed().toLower();
        if (normalized == QLatin1String("trace")) {
            return SentryObjCLogLevelTrace;
        }
        if (normalized == QLatin1String("debug")) {
            return SentryObjCLogLevelDebug;
        }
        if (normalized == QLatin1String("warning") || normalized == QLatin1String("warn")) {
            return SentryObjCLogLevelWarn;
        }
        if (normalized == QLatin1String("error")) {
            return SentryObjCLogLevelError;
        }
        if (normalized == QLatin1String("fatal")) {
            return SentryObjCLogLevelFatal;
        }
        return SentryObjCLogLevelInfo;
    }

    return logLevelFromInt(level.toInt());
}

SentryObjCSpanStatus spanStatusFromString(const QString &status)
{
    const QString normalized = status.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("ok")) {
        return SentryObjCSpanStatusOk;
    }
    if (normalized == QLatin1String("deadline_exceeded")) {
        return SentryObjCSpanStatusDeadlineExceeded;
    }
    if (normalized == QLatin1String("unauthenticated")) {
        return SentryObjCSpanStatusUnauthenticated;
    }
    if (normalized == QLatin1String("permission_denied")) {
        return SentryObjCSpanStatusPermissionDenied;
    }
    if (normalized == QLatin1String("not_found")) {
        return SentryObjCSpanStatusNotFound;
    }
    if (normalized == QLatin1String("resource_exhausted")) {
        return SentryObjCSpanStatusResourceExhausted;
    }
    if (normalized == QLatin1String("invalid_argument")) {
        return SentryObjCSpanStatusInvalidArgument;
    }
    if (normalized == QLatin1String("unimplemented")) {
        return SentryObjCSpanStatusUnimplemented;
    }
    if (normalized == QLatin1String("unavailable")) {
        return SentryObjCSpanStatusUnavailable;
    }
    if (normalized == QLatin1String("internal_error")) {
        return SentryObjCSpanStatusInternalError;
    }
    if (normalized == QLatin1String("unknown_error")) {
        return SentryObjCSpanStatusUnknownError;
    }
    if (normalized == QLatin1String("cancelled") || normalized == QLatin1String("canceled")) {
        return SentryObjCSpanStatusCancelled;
    }
    if (normalized == QLatin1String("already_exists")) {
        return SentryObjCSpanStatusAlreadyExists;
    }
    if (normalized == QLatin1String("failed_precondition")) {
        return SentryObjCSpanStatusFailedPrecondition;
    }
    if (normalized == QLatin1String("aborted")) {
        return SentryObjCSpanStatusAborted;
    }
    if (normalized == QLatin1String("out_of_range")) {
        return SentryObjCSpanStatusOutOfRange;
    }
    if (normalized == QLatin1String("data_loss")) {
        return SentryObjCSpanStatusDataLoss;
    }
    return SentryObjCSpanStatusUndefined;
}

NSDictionary<NSString *, NSString *> *stringDictionaryFromVariantMap(const QVariantMap &map)
{
    NSMutableDictionary<NSString *, NSString *> *dictionary = [NSMutableDictionary dictionaryWithCapacity:map.size()];
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!it.key().isEmpty()) {
            dictionary[nsString(it.key())] = nsString(it.value().toString());
        }
    }
    return dictionary;
}

NSArray<NSString *> *stringArrayFromStringList(const QStringList &values)
{
    NSMutableArray<NSString *> *array = [NSMutableArray arrayWithCapacity:values.size()];
    for (const QString &value : values) {
        [array addObject:nsString(value)];
    }
    return array;
}

NSArray<NSString *> *stringArrayFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QStringList) {
        return stringArrayFromStringList(value.toStringList());
    }

    NSMutableArray<NSString *> *array = [NSMutableArray array];
    const QVariantList list = value.toList();
    for (const QVariant &item : list) {
        const QString string = item.toString();
        if (!string.isEmpty()) {
            [array addObject:nsString(string)];
        }
    }
    return array;
}

SentryObjCFrame *frameFromVariantMap(const QVariantMap &map)
{
    SentryObjCFrame *frame = [[SentryObjCFrame alloc] init];
    frame.function = stringValue(map, QStringLiteral("function"));
    frame.fileName = stringValue(map, QStringLiteral("filename"), map.value(QStringLiteral("abs_path")).toString());
    frame.platform = stringValue(map, QStringLiteral("platform"));
    if (map.contains(QStringLiteral("lineno"))) {
        frame.lineNumber = @(map.value(QStringLiteral("lineno")).toInt());
    }
    if (map.contains(QStringLiteral("colno"))) {
        frame.columnNumber = @(map.value(QStringLiteral("colno")).toInt());
    }
    if (map.contains(QStringLiteral("in_app"))) {
        frame.inApp = @(map.value(QStringLiteral("in_app")).toBool());
    }
    return frame;
}

SentryObjCStacktrace *stacktraceFromVariantMap(const QVariantMap &map)
{
    const QVariantList frameValues = map.value(QStringLiteral("frames")).toList();
    if (frameValues.isEmpty()) {
        return nil;
    }

    NSMutableArray<SentryObjCFrame *> *frames = [NSMutableArray arrayWithCapacity:frameValues.size()];
    for (const QVariant &frameValue : frameValues) {
        const QVariantMap frameMap = frameValue.toMap();
        if (!frameMap.isEmpty()) {
            [frames addObject:frameFromVariantMap(frameMap)];
        }
    }

    if (frames.count == 0) {
        return nil;
    }

    return [[SentryObjCStacktrace alloc] initWithFrames:frames registers:@{}];
}

SentryObjCMechanism *mechanismFromVariantMap(const QVariantMap &map)
{
    SentryObjCMechanism *mechanism = [[SentryObjCMechanism alloc]
        initWithType:nsString(map.value(QStringLiteral("type"), QStringLiteral("generic")).toString())];
    if (map.contains(QStringLiteral("handled"))) {
        mechanism.handled = @(map.value(QStringLiteral("handled")).toBool());
    }
    if (map.contains(QStringLiteral("data"))) {
        mechanism.data = dictionaryFromVariantMap(map.value(QStringLiteral("data")).toMap());
    }
    return mechanism;
}

SentryObjCException *exceptionFromVariantMap(const QVariantMap &map)
{
    SentryObjCException *exception =
        [[SentryObjCException alloc] initWithValue:stringValue(map, QStringLiteral("value"))
                                          type:nsString(map.value(QStringLiteral("type"), QStringLiteral("Error")).toString())];
    const QVariantMap mechanismMap = map.value(QStringLiteral("mechanism")).toMap();
    if (!mechanismMap.isEmpty()) {
        exception.mechanism = mechanismFromVariantMap(mechanismMap);
    }
    const QVariantMap stacktraceMap = map.value(QStringLiteral("stacktrace")).toMap();
    if (!stacktraceMap.isEmpty()) {
        exception.stacktrace = stacktraceFromVariantMap(stacktraceMap);
    }
    return exception;
}

NSArray<SentryObjCException *> *exceptionsFromVariant(const QVariant &value)
{
    QVariantList values = value.toMap().value(QStringLiteral("values")).toList();
    if (values.isEmpty()) {
        values = value.toList();
    }

    NSMutableArray<SentryObjCException *> *exceptions = [NSMutableArray arrayWithCapacity:values.size()];
    for (const QVariant &exceptionValue : values) {
        const QVariantMap exceptionMap = exceptionValue.toMap();
        if (!exceptionMap.isEmpty()) {
            [exceptions addObject:exceptionFromVariantMap(exceptionMap)];
        }
    }
    return exceptions;
}

void applyVariantMapToBreadcrumb(SentryObjCBreadcrumb *breadcrumb, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("level"))) {
        breadcrumb.level = levelFromString(map.value(QStringLiteral("level")).toString());
    }
    breadcrumb.category = stringValue(map, QStringLiteral("category"), breadcrumb.category ? qtString(breadcrumb.category) : QString());
    breadcrumb.type = stringValue(map, QStringLiteral("type"));
    breadcrumb.message = stringValue(map, QStringLiteral("message"));

    const QVariantMap data = map.value(QStringLiteral("data")).toMap();
    if (!data.isEmpty()) {
        breadcrumb.data = dictionaryFromVariantMap(data);
    }
}

SentryObjCBreadcrumb *breadcrumbFromVariantMap(const QVariantMap &map)
{
    SentryObjCBreadcrumb *breadcrumb =
        [[SentryObjCBreadcrumb alloc] initWithLevel:levelFromString(map.value(QStringLiteral("level")).toString())
                                       category:nsString(map.value(QStringLiteral("category")).toString())];
    applyVariantMapToBreadcrumb(breadcrumb, map);
    return breadcrumb;
}

SentryObjCMessage *messageFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QString formatted = value.toMap().value(QStringLiteral("formatted")).toString();
        if (!formatted.isEmpty()) {
            return [[SentryObjCMessage alloc] initWithFormatted:nsString(formatted)];
        }
    }

    const QString message = value.toString();
    return message.isEmpty() ? nil : [[SentryObjCMessage alloc] initWithFormatted:nsString(message)];
}

void applyVariantMapToEvent(SentryObjCEvent *event, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("level"))) {
        event.level = levelFromString(map.value(QStringLiteral("level")).toString());
    }
    if (map.contains(QStringLiteral("platform"))) {
        event.platform = nsString(map.value(QStringLiteral("platform")).toString());
    }
    event.logger = stringValue(map, QStringLiteral("logger"));

    if (map.contains(QStringLiteral("message"))) {
        event.message = messageFromVariant(map.value(QStringLiteral("message")));
    }
    if (map.contains(QStringLiteral("exception"))) {
        event.exceptions = exceptionsFromVariant(map.value(QStringLiteral("exception")));
    }
    if (map.contains(QStringLiteral("stacktrace"))) {
        event.stacktrace = stacktraceFromVariantMap(map.value(QStringLiteral("stacktrace")).toMap());
    }
    if (map.contains(QStringLiteral("extra"))) {
        event.extra = dictionaryFromVariantMap(map.value(QStringLiteral("extra")).toMap());
    }
    if (map.contains(QStringLiteral("tags"))) {
        event.tags = stringDictionaryFromVariantMap(map.value(QStringLiteral("tags")).toMap());
    }
    if (map.contains(QStringLiteral("contexts"))) {
        event.context = dictionaryFromVariantMap(map.value(QStringLiteral("contexts")).toMap());
    } else if (map.contains(QStringLiteral("context"))) {
        event.context = dictionaryFromVariantMap(map.value(QStringLiteral("context")).toMap());
    }
    if (map.contains(QStringLiteral("fingerprint"))) {
        event.fingerprint = stringArrayFromVariant(map.value(QStringLiteral("fingerprint")));
    }
    if (!currentRelease.isEmpty()) {
        event.releaseName = nsString(currentRelease);
    }
}

SentryObjCEvent *eventFromVariantMap(const QVariantMap &map)
{
    SentryObjCEvent *event = [[SentryObjCEvent alloc] initWithLevel:levelFromString(map.value(QStringLiteral("level")).toString())];
    applyVariantMapToEvent(event, map);
    return event;
}

QVariantMap frameToVariantMap(SentryObjCFrame *frame)
{
    QVariantMap map;
    if (frame.function) {
        map.insert(QStringLiteral("function"), qtString(frame.function));
    }
    if (frame.fileName) {
        map.insert(QStringLiteral("filename"), qtString(frame.fileName));
    }
    if (frame.platform) {
        map.insert(QStringLiteral("platform"), qtString(frame.platform));
    }
    if (frame.lineNumber) {
        map.insert(QStringLiteral("lineno"), variantFromObject(frame.lineNumber));
    }
    if (frame.columnNumber) {
        map.insert(QStringLiteral("colno"), variantFromObject(frame.columnNumber));
    }
    if (frame.inApp) {
        map.insert(QStringLiteral("in_app"), variantFromObject(frame.inApp));
    }
    return map;
}

QVariantMap stacktraceToVariantMap(SentryObjCStacktrace *stacktrace)
{
    QVariantList frames;
    for (SentryObjCFrame *frame in stacktrace.frames) {
        frames.append(frameToVariantMap(frame));
    }

    QVariantMap map;
    if (!frames.isEmpty()) {
        map.insert(QStringLiteral("frames"), frames);
    }
    return map;
}

QVariantMap mechanismToVariantMap(SentryObjCMechanism *mechanism)
{
    QVariantMap map;
    if (mechanism.type) {
        map.insert(QStringLiteral("type"), qtString(mechanism.type));
    }
    if (mechanism.handled) {
        map.insert(QStringLiteral("handled"), variantFromObject(mechanism.handled));
    }
    if (mechanism.data) {
        map.insert(QStringLiteral("data"), variantFromObject(mechanism.data));
    }
    return map;
}

QVariantMap exceptionToVariantMap(SentryObjCException *exception)
{
    QVariantMap map;
    if (exception.value) {
        map.insert(QStringLiteral("value"), qtString(exception.value));
    }
    if (exception.type) {
        map.insert(QStringLiteral("type"), qtString(exception.type));
    }
    if (exception.mechanism) {
        map.insert(QStringLiteral("mechanism"), mechanismToVariantMap(exception.mechanism));
    }
    if (exception.stacktrace) {
        map.insert(QStringLiteral("stacktrace"), stacktraceToVariantMap(exception.stacktrace));
    }
    return map;
}

QVariantList exceptionsToVariantList(NSArray<SentryObjCException *> *exceptions)
{
    QVariantList values;
    for (SentryObjCException *exception in exceptions) {
        values.append(exceptionToVariantMap(exception));
    }
    return values;
}

QVariantMap messageToVariantMap(SentryObjCMessage *message)
{
    QVariantMap map;
    map.insert(QStringLiteral("formatted"), qtString(message.formatted));
    if (message.message) {
        map.insert(QStringLiteral("message"), qtString(message.message));
    }
    if (message.params) {
        map.insert(QStringLiteral("params"), variantFromObject(message.params));
    }
    return map;
}

QVariantMap breadcrumbToVariantMap(SentryObjCBreadcrumb *breadcrumb)
{
    QVariantMap map;
    map.insert(QStringLiteral("level"), levelName(breadcrumb.level));
    map.insert(QStringLiteral("category"), qtString(breadcrumb.category));
    if (breadcrumb.type) {
        map.insert(QStringLiteral("type"), qtString(breadcrumb.type));
    }
    if (breadcrumb.message) {
        map.insert(QStringLiteral("message"), qtString(breadcrumb.message));
    }
    if (breadcrumb.data) {
        map.insert(QStringLiteral("data"), variantFromObject(breadcrumb.data));
    }
    return map;
}

QVariantMap eventToVariantMap(SentryObjCEvent *event)
{
    QVariantMap map;
    map.insert(QStringLiteral("level"), levelName(event.level));
    map.insert(QStringLiteral("platform"), qtString(event.platform));
    if (event.logger) {
        map.insert(QStringLiteral("logger"), qtString(event.logger));
    }
    if (event.message) {
        map.insert(QStringLiteral("message"), messageToVariantMap(event.message));
    }
    if (event.exceptions.count > 0) {
        map.insert(QStringLiteral("exception"), QVariantMap {
            {QStringLiteral("values"), exceptionsToVariantList(event.exceptions)},
        });
    }
    if (event.stacktrace) {
        map.insert(QStringLiteral("stacktrace"), stacktraceToVariantMap(event.stacktrace));
    }
    if (event.extra) {
        map.insert(QStringLiteral("extra"), variantFromObject(event.extra));
    }
    if (event.tags) {
        map.insert(QStringLiteral("tags"), variantFromObject(event.tags));
    }
    if (event.context) {
        map.insert(QStringLiteral("contexts"), variantFromObject(event.context));
    }
    if (event.fingerprint) {
        map.insert(QStringLiteral("fingerprint"), variantFromObject(event.fingerprint));
    }
    return map;
}

SentryObjCUser *userFromVariantMap(const QVariantMap &map)
{
    SentryObjCUser *user = [[SentryObjCUser alloc] init];
    user.userId = stringValue(map, QStringLiteral("id"), map.value(QStringLiteral("userId")).toString());
    user.email = stringValue(map, QStringLiteral("email"));
    user.username = stringValue(map, QStringLiteral("username"));
    user.ipAddress = stringValue(map, QStringLiteral("ip_address"), map.value(QStringLiteral("ipAddress")).toString());
    user.name = stringValue(map, QStringLiteral("name"));

    QVariantMap data = map;
    data.remove(QStringLiteral("id"));
    data.remove(QStringLiteral("userId"));
    data.remove(QStringLiteral("email"));
    data.remove(QStringLiteral("username"));
    data.remove(QStringLiteral("ip_address"));
    data.remove(QStringLiteral("ipAddress"));
    data.remove(QStringLiteral("name"));
    if (!data.isEmpty()) {
        user.data = dictionaryFromVariantMap(data);
    }
    return user;
}

SentryObjCAttribute *logAttributeFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("value"))) {
            return logAttributeFromVariant(map.value(QStringLiteral("value")));
        }
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return [[SentryObjCAttribute alloc] initWithBoolean:value.toBool()];
    case QMetaType::Float:
    case QMetaType::Double:
        return [[SentryObjCAttribute alloc] initWithDouble:value.toDouble()];
    case QMetaType::QString:
        return [[SentryObjCAttribute alloc] initWithString:nsString(value.toString())];
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return [[SentryObjCAttribute alloc] initWithInteger:static_cast<NSInteger>(value.toLongLong())];
    }

    return [[SentryObjCAttribute alloc] initWithString:nsString(value.toString())];
}

NSDictionary<NSString *, SentryObjCAttribute *> *logAttributesFromVariantMap(const QVariantMap &attributes)
{
    NSMutableDictionary<NSString *, SentryObjCAttribute *> *dictionary =
        [NSMutableDictionary dictionaryWithCapacity:attributes.size()];
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        if (!it.key().isEmpty()) {
            dictionary[nsString(it.key())] = logAttributeFromVariant(it.value());
        }
    }
    return dictionary;
}

NSString *jsonStringFromObject(id object)
{
    if (!object || ![NSJSONSerialization isValidJSONObject:object]) {
        return nil;
    }

    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:object options:0 error:&error];
    if (!data || error) {
        return nil;
    }

    return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

SentryObjCAttributeContent *metricAttributeFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("value"))) {
            return metricAttributeFromVariant(map.value(QStringLiteral("value")));
        }
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return [SentryObjCAttributeContent boolean:value.toBool()];
    case QMetaType::Float:
    case QMetaType::Double:
        return [SentryObjCAttributeContent double:value.toDouble()];
    case QMetaType::QString:
        return [SentryObjCAttributeContent string:nsString(value.toString())];
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        if (list.isEmpty()) {
            return [SentryObjCAttributeContent stringArray:@[]];
        }

        const int firstType = list.first().metaType().id();
        bool homogeneous = true;
        for (const QVariant &item : list) {
            if (item.metaType().id() != firstType) {
                homogeneous = false;
                break;
            }
        }

        if (homogeneous && firstType == QMetaType::QString) {
            NSMutableArray<NSString *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:nsString(item.toString())];
            }
            return [SentryObjCAttributeContent stringArray:array];
        }
        if (homogeneous && firstType == QMetaType::Bool) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toBool())];
            }
            return [SentryObjCAttributeContent booleanArray:array];
        }
        if (homogeneous && (firstType == QMetaType::Float || firstType == QMetaType::Double)) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toDouble())];
            }
            return [SentryObjCAttributeContent doubleArray:array];
        }
        if (homogeneous && isSupportedInteger(list.first())) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toLongLong())];
            }
            return [SentryObjCAttributeContent integerArray:array];
        }

        NSString *json = jsonStringFromObject(arrayFromVariantList(list));
        return json ? [SentryObjCAttributeContent string:json] : nil;
    }
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return [SentryObjCAttributeContent integer:static_cast<NSInteger>(value.toLongLong())];
    }

    if (value.metaType().id() == QMetaType::QVariantMap) {
        NSString *json = jsonStringFromObject(dictionaryFromVariantMap(value.toMap()));
        return json ? [SentryObjCAttributeContent string:json] : nil;
    }

    return [SentryObjCAttributeContent string:nsString(value.toString())];
}

NSDictionary<NSString *, SentryObjCAttributeContent *> *metricAttributesFromVariantMap(const QVariantMap &attributes)
{
    NSMutableDictionary<NSString *, SentryObjCAttributeContent *> *dictionary =
        [NSMutableDictionary dictionaryWithCapacity:attributes.size()];
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        SentryObjCAttributeContent *attribute = metricAttributeFromVariant(it.value());
        if (attribute) {
            dictionary[nsString(it.key())] = attribute;
        }
    }
    return dictionary;
}

QVariantMap logAttributesToVariantMap(NSDictionary<NSString *, SentryObjCAttribute *> *attributes)
{
    QVariantMap map;
    for (NSString *key in attributes) {
        map.insert(qtString(key), variantFromObject(attributes[key].value));
    }
    return map;
}

QVariantMap logToVariantMap(SentryObjCLog *log)
{
    return {
        {QStringLiteral("level"), logLevelName(log.level)},
        {QStringLiteral("message"), qtString(log.body)},
        {QStringLiteral("attributes"), logAttributesToVariantMap(log.attributes)},
    };
}

void applyVariantMapToLog(SentryObjCLog *log, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("level"))) {
        log.level = logLevelFromVariant(map.value(QStringLiteral("level")));
    }
    if (map.contains(QStringLiteral("message"))) {
        log.body = nsString(map.value(QStringLiteral("message")).toString());
    }
    if (map.contains(QStringLiteral("attributes"))) {
        log.attributes = logAttributesFromVariantMap(map.value(QStringLiteral("attributes")).toMap());
    }
}

QVariantMap metricAttributesToVariantMap(NSDictionary<NSString *, SentryObjCAttributeContent *> *attributes)
{
    QVariantMap map;
    for (NSString *key in attributes) {
        map.insert(qtString(key), variantFromObject(attributes[key].value));
    }
    return map;
}

QString metricValueTypeName(SentryObjCMetricValue *value)
{
    if (value.isCounter) {
        return QStringLiteral("counter");
    }
    if (value.isDistribution) {
        return QStringLiteral("distribution");
    }
    return QStringLiteral("gauge");
}

QVariant metricValueToVariant(SentryObjCMetricValue *value)
{
    if (value.isCounter) {
        return QVariant::fromValue(static_cast<qulonglong>(value.counterValue));
    }
    if (value.isDistribution) {
        return QVariant(value.distributionValue);
    }
    return QVariant(value.gaugeValue);
}

SentryObjCMetricValue *metricValueFromVariant(const QVariant &value,
                                              const QString &type,
                                              SentryObjCMetricValue *currentValue)
{
    QString normalized = type.trimmed().toLower();
    if (normalized.isEmpty() && currentValue) {
        normalized = metricValueTypeName(currentValue);
    }

    if (normalized == QLatin1String("counter") || (normalized.isEmpty() && currentValue && currentValue.isCounter)) {
        return [SentryObjCMetricValue counter:static_cast<NSUInteger>(value.toULongLong())];
    }
    if (normalized == QLatin1String("distribution")) {
        return [SentryObjCMetricValue distribution:value.toDouble()];
    }
    return [SentryObjCMetricValue gauge:value.toDouble()];
}

SentryObjCUnit *unitFromString(const QString &unit)
{
    return unit.isEmpty() ? nil : [[SentryObjCUnit alloc] initWithRawValue:nsString(unit)];
}

QVariantMap metricToVariantMap(SentryObjCMetric *metric)
{
    QVariantMap map = {
        {QStringLiteral("name"), qtString(metric.name)},
        {QStringLiteral("unit"), metric.unit ? qtString(metric.unit.rawValue) : QString()},
        {QStringLiteral("attributes"), metricAttributesToVariantMap(metric.attributes)},
    };

    if (metric.value) {
        map.insert(QStringLiteral("type"), metricValueTypeName(metric.value));
        map.insert(QStringLiteral("value"), metricValueToVariant(metric.value));
    }

    return map;
}

void applyVariantMapToMetric(SentryObjCMetric *metric, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("name"))) {
        metric.name = nsString(map.value(QStringLiteral("name")).toString());
    }
    if (map.contains(QStringLiteral("unit"))) {
        metric.unit = unitFromString(map.value(QStringLiteral("unit")).toString());
    }
    if (map.contains(QStringLiteral("value"))) {
        metric.value = metricValueFromVariant(map.value(QStringLiteral("value")),
                                              map.value(QStringLiteral("type")).toString(),
                                              metric.value);
    }
    if (map.contains(QStringLiteral("attributes"))) {
        metric.attributes = metricAttributesFromVariantMap(map.value(QStringLiteral("attributes")).toMap());
    }
}

SentryObjCBridge::HookResult runHook(const SentryObjCBridge::Hook &hook, const QVariant &value)
{
    if (!hook) {
        return {};
    }
    return hook(value);
}

SentryObjCSpan *spanFromHandle(void *handle)
{
    return handle ? (__bridge SentryObjCSpan *)handle : nil;
}

void *retainedHandleFromSpan(SentryObjCSpan *span)
{
    return span ? [span retain] : nullptr;
}

QVariantMap samplingContextMap(SentryObjCSamplingContext *context)
{
    QVariantMap map;
    map.insert(QStringLiteral("transactionContext"), variantFromObject(context.transactionContext));
    if (context.customSamplingContext) {
        map.insert(QStringLiteral("customSamplingContext"), variantFromObject(context.customSamplingContext));
    }
    return map;
}

SentryObjCEvent *runEventHook(SentryObjCEvent *event, const SentryObjCBridge::Hook &hook)
{
    if (!currentRelease.isEmpty()) {
        event.releaseName = nsString(currentRelease);
    }

    const SentryObjCBridge::HookResult result = runHook(hook, eventToVariantMap(event));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToEvent(event, result.value.toMap());
    }
    return event;
}

SentryObjCBreadcrumb *runBreadcrumbHook(SentryObjCBreadcrumb *breadcrumb, const SentryObjCBridge::Hook &hook)
{
    const SentryObjCBridge::HookResult result = runHook(hook, breadcrumbToVariantMap(breadcrumb));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToBreadcrumb(breadcrumb, result.value.toMap());
    }
    return breadcrumb;
}

SentryObjCLog *runLogHook(SentryObjCLog *log, const SentryObjCBridge::Hook &hook)
{
    const SentryObjCBridge::HookResult result = runHook(hook, logToVariantMap(log));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToLog(log, result.value.toMap());
    }
    return log;
}

SentryObjCMetric *runMetricHook(SentryObjCMetric *metric, const SentryObjCBridge::Hook &hook)
{
    const SentryObjCBridge::HookResult result = runHook(hook, metricToVariantMap(metric));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToMetric(metric, result.value.toMap());
    }
    return metric;
}

SentryObjCAttachmentType nativeAttachmentType(const SentryObjCBridge::Attachment &attachment)
{
    if (attachment.attachmentType == SentryObjCBridge::Attachment::ViewHierarchy) {
        return SentryObjCAttachmentTypeViewHierarchy;
    }
    return SentryObjCAttachmentTypeEventAttachment;
}

SentryObjCAttachment *nativeAttachment(const SentryObjCBridge::Attachment &attachment)
{
    NSString *filename = nsStringOrNil(attachment.filename);
    NSString *contentType = nsStringOrNil(attachment.contentType);
    const SentryObjCAttachmentType attachmentType = nativeAttachmentType(attachment);

    if (attachment.type == SentryObjCBridge::Attachment::Bytes) {
        NSData *data = [NSData dataWithBytes:attachment.bytes.constData()
                                      length:static_cast<NSUInteger>(attachment.bytes.size())];
        return [[SentryObjCAttachment alloc] initWithData:data
                                             filename:filename ?: @""
                                          contentType:contentType
                                       attachmentType:attachmentType];
    }

    const QString nativePath = QDir::toNativeSeparators(attachment.path);
    if (!filename) {
        filename = nsString(QFileInfo(nativePath).fileName());
    }
    return [[SentryObjCAttachment alloc] initWithPath:nsString(nativePath)
                                        filename:filename
                                     contentType:contentType
                                  attachmentType:attachmentType];
}

NSArray<SentryObjCAttachment *> *nativeAttachments(const QList<SentryObjCBridge::Attachment> &attachments)
{
    NSMutableArray<SentryObjCAttachment *> *array = [NSMutableArray arrayWithCapacity:attachments.size()];
    for (const SentryObjCBridge::Attachment &attachment : attachments) {
        SentryObjCAttachment *native = nativeAttachment(attachment);
        if (native) {
            [array addObject:native];
        }
    }
    return array;
}

} // namespace

namespace SentryObjCBridge {

bool isEnabled()
{
    @autoreleasepool {
        return [SentryObjCSDK isEnabled];
    }
}

bool start(const Options &options)
{
    @autoreleasepool {
        currentRelease = options.release;

        SentryObjCOptions *nativeOptions = [[SentryObjCOptions alloc] init];
        nativeOptions.dsn = nsStringOrNil(options.dsn);
        nativeOptions.cacheDirectoryPath = nsStringOrNil(options.databasePath);
        nativeOptions.releaseName = nsStringOrNil(options.release);
        if (!options.environment.isEmpty()) {
            nativeOptions.environment = nsString(options.environment);
        }
        nativeOptions.dist = nsStringOrNil(options.dist);
        nativeOptions.debug = options.debug;
        nativeOptions.enableLogs = options.enableLogs;
        nativeOptions.enableMetrics = options.enableMetrics;
        nativeOptions.enableAutoSessionTracking = options.autoSessionTracking;
#if SENTRY_OBJC_HAS_UIKIT
        nativeOptions.attachScreenshot = options.attachScreenshot;
#endif
        nativeOptions.sampleRate = @(options.sampleRate);
        if (options.tracesSampleRate >= 0.0) {
            nativeOptions.tracesSampleRate = @(options.tracesSampleRate);
        }
        if (!options.tracePropagationTargets.isEmpty()) {
            nativeOptions.tracePropagationTargets = stringArrayFromStringList(options.tracePropagationTargets);
        }
        nativeOptions.strictTraceContinuation = options.strictTraceContinuation;
        nativeOptions.orgId = nsStringOrNil(options.orgId);
        nativeOptions.maxBreadcrumbs = static_cast<NSUInteger>(options.maxBreadcrumbs);
        nativeOptions.shutdownTimeInterval = static_cast<NSTimeInterval>(options.shutdownTimeout) / 1000.0;

        Hook beforeSend = options.beforeSend;
        nativeOptions.beforeSend = ^SentryObjCEvent *_Nullable(SentryObjCEvent *event) {
            return runEventHook(event, beforeSend);
        };

        if (options.beforeBreadcrumb) {
            Hook beforeBreadcrumb = options.beforeBreadcrumb;
            nativeOptions.beforeBreadcrumb = ^SentryObjCBreadcrumb *_Nullable(SentryObjCBreadcrumb *breadcrumb) {
                return runBreadcrumbHook(breadcrumb, beforeBreadcrumb);
            };
        }

        if (options.beforeSendLog) {
            Hook beforeSendLog = options.beforeSendLog;
            nativeOptions.beforeSendLog = ^SentryObjCLog *_Nullable(SentryObjCLog *log) {
                return runLogHook(log, beforeSendLog);
            };
        }

        if (options.beforeSendMetric) {
            Hook beforeSendMetric = options.beforeSendMetric;
            nativeOptions.beforeSendMetric = ^SentryObjCMetric *_Nullable(SentryObjCMetric *metric) {
                return runMetricHook(metric, beforeSendMetric);
            };
        }

        if (options.tracesSampler) {
            Hook tracesSampler = options.tracesSampler;
            nativeOptions.tracesSampler = ^NSNumber *_Nullable(SentryObjCSamplingContext *context) {
                const HookResult result = runHook(tracesSampler, samplingContextMap(context));
                if (result.action == HookResult::Drop) {
                    return @0;
                }
                if (result.action == HookResult::Replace) {
                    const double sampleRate = result.value.toDouble();
                    return std::isfinite(sampleRate) ? @(sampleRate) : nil;
                }
                return nil;
            };
        }

        if (options.onCrash) {
            Hook onCrash = options.onCrash;
            nativeOptions.onLastRunStatusDetermined = ^(SentryObjCLastRunStatus status, SentryObjCEvent *_Nullable event) {
                if (status == SentryObjCLastRunStatusDidCrash && event) {
                    runEventHook(event, onCrash);
                }
            };
        }

        [SentryObjCSDK startWithOptions:nativeOptions];
        if (!options.user.isEmpty()) {
            [SentryObjCSDK setUser:userFromVariantMap(options.user)];
        }
        return [SentryObjCSDK isEnabled];
    }
}

void flush(int timeoutMs)
{
    @autoreleasepool {
        [SentryObjCSDK flush:static_cast<NSTimeInterval>(timeoutMs < 0 ? 0 : timeoutMs) / 1000.0];
    }
}

void close()
{
    @autoreleasepool {
        [SentryObjCSDK close];
    }
}

void setRelease(const QString &release)
{
    currentRelease = release;
}

void setEnvironment(const QString &environment)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setEnvironment:nsStringOrNil(environment)];
        }];
    }
}

void setLevel(const QString &level)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setLevel:levelFromString(level)];
        }];
    }
}

void setUser(const QVariantMap &user)
{
    @autoreleasepool {
        SentryObjCUser *nativeUser = userFromVariantMap(user);
        [SentryObjCSDK setUser:nativeUser];
    }
}

void removeUser()
{
    @autoreleasepool {
        [SentryObjCSDK setUser:nil];
    }
}

void setTag(const QString &key, const QString &value)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setTagValue:nsString(value) forKey:nsString(key)];
        }];
    }
}

void removeTag(const QString &key)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope removeTagForKey:nsString(key)];
        }];
    }
}

void setContext(const QString &key, const QVariantMap &context)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setContextValue:dictionaryFromVariantMap(context) forKey:nsString(key)];
        }];
    }
}

void removeContext(const QString &key)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope removeContextForKey:nsString(key)];
        }];
    }
}

void setAttribute(const QString &key, const QVariant &value)
{
    @autoreleasepool {
        id nativeValue = objectFromVariant(value);
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setAttributeValue:nativeValue forKey:nsString(key)];
        }];
    }
}

void removeAttribute(const QString &key)
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope removeAttributeForKey:nsString(key)];
        }];
    }
}

void setFingerprint(const QStringList &fingerprint)
{
    @autoreleasepool {
        NSArray<NSString *> *nativeFingerprint = stringArrayFromStringList(fingerprint);
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setFingerprint:nativeFingerprint];
        }];
    }
}

void clearFingerprint()
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope setFingerprint:nil];
        }];
    }
}

void setAttachments(const QList<Attachment> &attachments)
{
    @autoreleasepool {
        NSArray<SentryObjCAttachment *> *native = nativeAttachments(attachments);
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope clearAttachments];
            for (SentryObjCAttachment *attachment in native) {
                [scope addAttachment:attachment];
            }
        }];
    }
}

void clearAttachments()
{
    @autoreleasepool {
        [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
            [scope clearAttachments];
        }];
    }
}

void startSession()
{
    @autoreleasepool {
        [SentryObjCSDK startSession];
    }
}

void endSession()
{
    @autoreleasepool {
        [SentryObjCSDK endSession];
    }
}

void addBreadcrumb(const QVariantMap &breadcrumb)
{
    @autoreleasepool {
        [SentryObjCSDK addBreadcrumb:breadcrumbFromVariantMap(breadcrumb)];
    }
}

void log(int level, const QString &message, const QVariantMap &attributes)
{
    @autoreleasepool {
        NSDictionary<NSString *, SentryObjCAttribute *> *nativeAttributes = logAttributesFromVariantMap(attributes);
        SentryObjCLogger *logger = [SentryObjCSDK logger];
        switch (logLevelFromInt(level)) {
        case SentryObjCLogLevelTrace:
            [logger trace:nsString(message) attributes:nativeAttributes];
            break;
        case SentryObjCLogLevelDebug:
            [logger debug:nsString(message) attributes:nativeAttributes];
            break;
        case SentryObjCLogLevelWarn:
            [logger warn:nsString(message) attributes:nativeAttributes];
            break;
        case SentryObjCLogLevelError:
            [logger error:nsString(message) attributes:nativeAttributes];
            break;
        case SentryObjCLogLevelFatal:
            [logger fatal:nsString(message) attributes:nativeAttributes];
            break;
        case SentryObjCLogLevelInfo:
        default:
            [logger info:nsString(message) attributes:nativeAttributes];
            break;
        }
    }
}

void count(const QString &name, quint64 value, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentryObjCSDK metrics] countWithKey:nsString(name)
                                    value:static_cast<NSUInteger>(value)
                               attributes:metricAttributesFromVariantMap(attributes)];
    }
}

void gauge(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentryObjCSDK metrics] gaugeWithKey:nsString(name)
                                    value:value
                                     unit:unitFromString(unit)
                               attributes:metricAttributesFromVariantMap(attributes)];
    }
}

void distribution(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentryObjCSDK metrics] distributionWithKey:nsString(name)
                                           value:value
                                            unit:unitFromString(unit)
                                      attributes:metricAttributesFromVariantMap(attributes)];
    }
}

void *startTransaction(const QString &name,
                       const QString &operation,
                       const QString &description,
                       bool bindToScope,
                       const QVariantMap &customSamplingContext)
{
    @autoreleasepool {
        SentryObjCTransactionContext *context =
            [[SentryObjCTransactionContext alloc] initWithName:nsString(name) operation:nsString(operation)];
        SentryObjCSpan *span = [SentryObjCSDK startTransactionWithContext:context
                                                               bindToScope:bindToScope
                                                     customSamplingContext:dictionaryFromVariantMap(customSamplingContext)];
        if (!description.isEmpty()) {
            span.spanDescription = nsString(description);
        }
        return retainedHandleFromSpan(span);
    }
}

void *startSpan(void *parentSpan, const QString &operation, const QString &description, bool bindToScope)
{
    @autoreleasepool {
        SentryObjCSpan *parent = spanFromHandle(parentSpan);
        if (!parent) {
            parent = [SentryObjCSDK span];
        }
        if (!parent) {
            return nullptr;
        }

        SentryObjCSpan *span = [parent startChildWithOperation:nsString(operation)
                                                   description:nsStringOrNil(description)];
        if (bindToScope) {
            [SentryObjCSDK configureScope:^(SentryObjCScope *scope) {
                scope.span = span;
            }];
        }
        return retainedHandleFromSpan(span);
    }
}

void finishSpan(void *spanHandle, const QString &status)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (!span) {
            return;
        }
        if (status.isEmpty()) {
            [span finish];
        } else {
            [span finishWithStatus:spanStatusFromString(status)];
        }
    }
}

void setSpanStatus(void *spanHandle, const QString &status)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (span) {
            span.status = spanStatusFromString(status);
        }
    }
}

void setSpanData(void *spanHandle, const QString &key, const QVariant &value)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (span) {
            [span setDataValue:objectFromVariant(value) forKey:nsString(key)];
        }
    }
}

void removeSpanData(void *spanHandle, const QString &key)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (span) {
            [span removeDataForKey:nsString(key)];
        }
    }
}

void setSpanTag(void *spanHandle, const QString &key, const QString &value)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (span) {
            [span setTagValue:nsString(value) forKey:nsString(key)];
        }
    }
}

void removeSpanTag(void *spanHandle, const QString &key)
{
    @autoreleasepool {
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (span) {
            [span removeTagForKey:nsString(key)];
        }
    }
}

QVariantMap spanTraceHeaders(void *spanHandle)
{
    @autoreleasepool {
        QVariantMap headers;
        SentryObjCSpan *span = spanFromHandle(spanHandle);
        if (!span) {
            return headers;
        }

        SentryObjCTraceHeader *traceHeader = [span toTraceHeader];
        NSString *traceHeaderValue = [traceHeader value];
        if (traceHeaderValue) {
            headers.insert(QStringLiteral("sentry-trace"), qtString(traceHeaderValue));
        }

        NSString *baggage = [span baggageHttpHeader];
        if (baggage) {
            headers.insert(QStringLiteral("baggage"), qtString(baggage));
        }
        return headers;
    }
}

void releaseSpan(void *spanHandle)
{
    if (spanHandle) {
        [(id)spanHandle release];
    }
}

QString captureEvent(const QVariantMap &event, const QStringList &fingerprint)
{
    return captureEvent(event, fingerprint, {});
}

QString captureEvent(const QVariantMap &event,
                     const QStringList &fingerprint,
                     const QList<Attachment> &attachments)
{
    @autoreleasepool {
        SentryObjCEvent *nativeEvent = eventFromVariantMap(event);
        SentryObjCId *eventId = nil;
        if (fingerprint.isEmpty() && attachments.isEmpty()) {
            eventId = [SentryObjCSDK captureEvent:nativeEvent];
        } else {
            NSArray<NSString *> *nativeFingerprint = stringArrayFromStringList(fingerprint);
            NSArray<SentryObjCAttachment *> *nativeEventAttachments = nativeAttachments(attachments);
            eventId = [SentryObjCSDK captureEvent:nativeEvent
                               withScopeBlock:^(SentryObjCScope *scope) {
                                   if (nativeFingerprint.count > 0) {
                                       [scope setFingerprint:nativeFingerprint];
                                   }
                                   for (SentryObjCAttachment *attachment in nativeEventAttachments) {
                                       [scope addAttachment:attachment];
                                   }
                               }];
        }
        return qtSentryIdString(eventId);
    }
}

void captureFeedback(const QVariantMap &feedback, const QList<Attachment> &attachments)
{
    @autoreleasepool {
        NSString *message = feedbackValue(feedback, QStringLiteral("message"));
        NSString *email = feedbackValue(feedback,
                                        QStringLiteral("email"),
                                        QStringLiteral("contactEmail"),
                                        QStringLiteral("contact_email"));
        NSString *name = feedbackValue(feedback, QStringLiteral("name"));
        NSString *associatedEventId = feedbackValue(feedback,
                                                    QStringLiteral("associatedEventId"),
                                                    QStringLiteral("associated_event_id"),
                                                    QStringLiteral("eventId"));
        SentryObjCId *nativeAssociatedEventId =
            associatedEventId ? [[SentryObjCId alloc] initWithUUIDString:associatedEventId] : nil;
        [SentryObjCSDK captureFeedbackWithMessage:message ?: @""
                                             name:name
                                            email:email
                                           source:SentryObjCFeedbackSourceCustom
                                associatedEventId:nativeAssociatedEventId
                                      attachments:nativeAttachments(attachments)];
    }
}

} // namespace SentryObjCBridge
