#include "sentryobjcbridge_p.h"

#import <SentryObjC/SentryAttachment.h>
#import <SentryObjC/SentryAttribute.h>
#import <SentryObjC/SentryBreadcrumb.h>
#import <SentryObjC/SentryEvent.h>
#import <SentryObjC/SentryException.h>
#import <SentryObjC/SentryFeedback.h>
#import <SentryObjC/SentryFeedbackSource.h>
#import <SentryObjC/SentryFrame.h>
#import <SentryObjC/SentryId.h>
#import <SentryObjC/SentryLevel.h>
#import <SentryObjC/SentryLog.h>
#import <SentryObjC/SentryLogLevel.h>
#import <SentryObjC/SentryLogger.h>
#import <SentryObjC/SentryMechanism.h>
#import <SentryObjC/SentryMessage.h>
#import <SentryObjC/SentryMetricsApi.h>
#import <SentryObjC/SentryObjCAttributeContent.h>
#import <SentryObjC/SentryObjCMetric.h>
#import <SentryObjC/SentryObjCMetricValue.h>
#import <SentryObjC/SentryOptions.h>
#import <SentryObjC/SentrySDK.h>
#import <SentryObjC/SentryScope.h>
#import <SentryObjC/SentryStacktrace.h>
#import <SentryObjC/SentryUser.h>

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

QString qtSentryIdString(SentryId *eventId)
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

SentryLevel levelFromString(const QString &level)
{
    const QString normalized = level.trimmed().toLower();
    if (normalized == QLatin1String("debug") || normalized == QLatin1String("trace")) {
        return kSentryLevelDebug;
    }
    if (normalized == QLatin1String("warning") || normalized == QLatin1String("warn")) {
        return kSentryLevelWarning;
    }
    if (normalized == QLatin1String("error")) {
        return kSentryLevelError;
    }
    if (normalized == QLatin1String("fatal")) {
        return kSentryLevelFatal;
    }
    return kSentryLevelInfo;
}

SentryLogLevel logLevelFromInt(int level)
{
    switch (level) {
    case -2:
        return SentryLogLevelTrace;
    case -1:
        return SentryLogLevelDebug;
    case 1:
        return SentryLogLevelWarn;
    case 2:
        return SentryLogLevelError;
    case 3:
        return SentryLogLevelFatal;
    case 0:
    default:
        return SentryLogLevelInfo;
    }
}

QString logLevelName(SentryLogLevel level)
{
    switch (level) {
    case SentryLogLevelTrace:
        return QStringLiteral("trace");
    case SentryLogLevelDebug:
        return QStringLiteral("debug");
    case SentryLogLevelWarn:
        return QStringLiteral("warning");
    case SentryLogLevelError:
        return QStringLiteral("error");
    case SentryLogLevelFatal:
        return QStringLiteral("fatal");
    case SentryLogLevelInfo:
    default:
        return QStringLiteral("info");
    }
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

SentryFrame *frameFromVariantMap(const QVariantMap &map)
{
    SentryFrame *frame = [[SentryFrame alloc] init];
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

SentryStacktrace *stacktraceFromVariantMap(const QVariantMap &map)
{
    const QVariantList frameValues = map.value(QStringLiteral("frames")).toList();
    if (frameValues.isEmpty()) {
        return nil;
    }

    NSMutableArray<SentryFrame *> *frames = [NSMutableArray arrayWithCapacity:frameValues.size()];
    for (const QVariant &frameValue : frameValues) {
        const QVariantMap frameMap = frameValue.toMap();
        if (!frameMap.isEmpty()) {
            [frames addObject:frameFromVariantMap(frameMap)];
        }
    }

    if (frames.count == 0) {
        return nil;
    }

    return [[SentryStacktrace alloc] initWithFrames:frames registers:@{}];
}

SentryMechanism *mechanismFromVariantMap(const QVariantMap &map)
{
    SentryMechanism *mechanism = [[SentryMechanism alloc]
        initWithType:nsString(map.value(QStringLiteral("type"), QStringLiteral("generic")).toString())];
    if (map.contains(QStringLiteral("handled"))) {
        mechanism.handled = @(map.value(QStringLiteral("handled")).toBool());
    }
    if (map.contains(QStringLiteral("data"))) {
        mechanism.data = dictionaryFromVariantMap(map.value(QStringLiteral("data")).toMap());
    }
    return mechanism;
}

SentryException *exceptionFromVariantMap(const QVariantMap &map)
{
    SentryException *exception =
        [[SentryException alloc] initWithValue:stringValue(map, QStringLiteral("value"))
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

NSArray<SentryException *> *exceptionsFromVariant(const QVariant &value)
{
    QVariantList values = value.toMap().value(QStringLiteral("values")).toList();
    if (values.isEmpty()) {
        values = value.toList();
    }

    NSMutableArray<SentryException *> *exceptions = [NSMutableArray arrayWithCapacity:values.size()];
    for (const QVariant &exceptionValue : values) {
        const QVariantMap exceptionMap = exceptionValue.toMap();
        if (!exceptionMap.isEmpty()) {
            [exceptions addObject:exceptionFromVariantMap(exceptionMap)];
        }
    }
    return exceptions;
}

void applyVariantMapToBreadcrumb(SentryBreadcrumb *breadcrumb, const QVariantMap &map)
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

SentryBreadcrumb *breadcrumbFromVariantMap(const QVariantMap &map)
{
    SentryBreadcrumb *breadcrumb =
        [[SentryBreadcrumb alloc] initWithLevel:levelFromString(map.value(QStringLiteral("level")).toString())
                                       category:nsString(map.value(QStringLiteral("category")).toString())];
    applyVariantMapToBreadcrumb(breadcrumb, map);
    return breadcrumb;
}

SentryMessage *messageFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QString formatted = value.toMap().value(QStringLiteral("formatted")).toString();
        if (!formatted.isEmpty()) {
            return [[SentryMessage alloc] initWithFormatted:nsString(formatted)];
        }
    }

    const QString message = value.toString();
    return message.isEmpty() ? nil : [[SentryMessage alloc] initWithFormatted:nsString(message)];
}

void applyVariantMapToEvent(SentryEvent *event, const QVariantMap &map)
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

SentryEvent *eventFromVariantMap(const QVariantMap &map)
{
    SentryEvent *event = [[SentryEvent alloc] initWithLevel:levelFromString(map.value(QStringLiteral("level")).toString())];
    applyVariantMapToEvent(event, map);
    return event;
}

SentryUser *userFromVariantMap(const QVariantMap &map)
{
    SentryUser *user = [[SentryUser alloc] init];
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

SentryAttribute *logAttributeFromVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("value"))) {
            return logAttributeFromVariant(map.value(QStringLiteral("value")));
        }
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return [[SentryAttribute alloc] initWithBoolean:value.toBool()];
    case QMetaType::Float:
    case QMetaType::Double:
        return [[SentryAttribute alloc] initWithDouble:value.toDouble()];
    case QMetaType::QString:
        return [[SentryAttribute alloc] initWithString:nsString(value.toString())];
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return [[SentryAttribute alloc] initWithInteger:static_cast<NSInteger>(value.toLongLong())];
    }

    return [[SentryAttribute alloc] initWithString:nsString(value.toString())];
}

NSDictionary<NSString *, SentryAttribute *> *logAttributesFromVariantMap(const QVariantMap &attributes)
{
    NSMutableDictionary<NSString *, SentryAttribute *> *dictionary =
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
        return [SentryObjCAttributeContent booleanWithValue:value.toBool()];
    case QMetaType::Float:
    case QMetaType::Double:
        return [SentryObjCAttributeContent doubleWithValue:value.toDouble()];
    case QMetaType::QString:
        return [SentryObjCAttributeContent stringWithValue:nsString(value.toString())];
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        if (list.isEmpty()) {
            return [SentryObjCAttributeContent stringArrayWithValue:@[]];
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
            return [SentryObjCAttributeContent stringArrayWithValue:array];
        }
        if (homogeneous && firstType == QMetaType::Bool) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toBool())];
            }
            return [SentryObjCAttributeContent booleanArrayWithValue:array];
        }
        if (homogeneous && (firstType == QMetaType::Float || firstType == QMetaType::Double)) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toDouble())];
            }
            return [SentryObjCAttributeContent doubleArrayWithValue:array];
        }
        if (homogeneous && isSupportedInteger(list.first())) {
            NSMutableArray<NSNumber *> *array = [NSMutableArray arrayWithCapacity:list.size()];
            for (const QVariant &item : list) {
                [array addObject:@(item.toLongLong())];
            }
            return [SentryObjCAttributeContent integerArrayWithValue:array];
        }

        NSString *json = jsonStringFromObject(arrayFromVariantList(list));
        return json ? [SentryObjCAttributeContent stringWithValue:json] : nil;
    }
    default:
        break;
    }

    if (isSupportedInteger(value)) {
        return [SentryObjCAttributeContent integerWithValue:static_cast<NSInteger>(value.toLongLong())];
    }

    if (value.metaType().id() == QMetaType::QVariantMap) {
        NSString *json = jsonStringFromObject(dictionaryFromVariantMap(value.toMap()));
        return json ? [SentryObjCAttributeContent stringWithValue:json] : nil;
    }

    return [SentryObjCAttributeContent stringWithValue:nsString(value.toString())];
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

QVariantMap logToVariantMap(SentryLog *log)
{
    return {
        {QStringLiteral("level"), logLevelName(log.level)},
        {QStringLiteral("message"), qtString(log.body)},
        {QStringLiteral("attributes"), variantFromObject(log.attributes)},
    };
}

void applyVariantMapToLog(SentryLog *log, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("level"))) {
        log.level = logLevelFromInt(map.value(QStringLiteral("level")).toInt());
    }
    if (map.contains(QStringLiteral("message"))) {
        log.body = nsString(map.value(QStringLiteral("message")).toString());
    }
    if (map.contains(QStringLiteral("attributes"))) {
        log.attributes = logAttributesFromVariantMap(map.value(QStringLiteral("attributes")).toMap());
    }
}

QVariantMap metricToVariantMap(SentryObjCMetric *metric)
{
    QVariantMap map = {
        {QStringLiteral("name"), qtString(metric.name)},
        {QStringLiteral("unit"), qtString(metric.unit)},
        {QStringLiteral("attributes"), variantFromObject(metric.attributes)},
    };

    if (metric.value) {
        map.insert(QStringLiteral("value"), variantFromObject(metric.value));
    }

    return map;
}

void applyVariantMapToMetric(SentryObjCMetric *metric, const QVariantMap &map)
{
    if (map.contains(QStringLiteral("name"))) {
        metric.name = nsString(map.value(QStringLiteral("name")).toString());
    }
    if (map.contains(QStringLiteral("unit"))) {
        metric.unit = nsStringOrNil(map.value(QStringLiteral("unit")).toString());
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

SentryEvent *runEventHook(SentryEvent *event, const SentryObjCBridge::Hook &hook)
{
    if (!currentRelease.isEmpty()) {
        event.releaseName = nsString(currentRelease);
    }

    const SentryObjCBridge::HookResult result = runHook(hook, variantFromObject([event serialize]));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToEvent(event, result.value.toMap());
    }
    return event;
}

SentryBreadcrumb *runBreadcrumbHook(SentryBreadcrumb *breadcrumb, const SentryObjCBridge::Hook &hook)
{
    const SentryObjCBridge::HookResult result = runHook(hook, variantFromObject([breadcrumb serialize]));
    if (result.action == SentryObjCBridge::HookResult::Drop) {
        return nil;
    }
    if (result.action == SentryObjCBridge::HookResult::Replace) {
        applyVariantMapToBreadcrumb(breadcrumb, result.value.toMap());
    }
    return breadcrumb;
}

SentryLog *runLogHook(SentryLog *log, const SentryObjCBridge::Hook &hook)
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

SentryAttachment *nativeAttachment(const SentryObjCBridge::Attachment &attachment)
{
    NSString *filename = nsStringOrNil(attachment.filename);
    NSString *contentType = nsStringOrNil(attachment.contentType);

    if (attachment.type == SentryObjCBridge::Attachment::Bytes) {
        NSData *data = [NSData dataWithBytes:attachment.bytes.constData()
                                      length:static_cast<NSUInteger>(attachment.bytes.size())];
        return [[SentryAttachment alloc] initWithData:data filename:filename ?: @"" contentType:contentType];
    }

    const QString nativePath = QDir::toNativeSeparators(attachment.path);
    if (!filename) {
        filename = nsString(QFileInfo(nativePath).fileName());
    }
    return [[SentryAttachment alloc] initWithPath:nsString(nativePath) filename:filename contentType:contentType];
}

NSArray<SentryAttachment *> *nativeAttachments(const QList<SentryObjCBridge::Attachment> &attachments)
{
    NSMutableArray<SentryAttachment *> *array = [NSMutableArray arrayWithCapacity:attachments.size()];
    for (const SentryObjCBridge::Attachment &attachment : attachments) {
        SentryAttachment *native = nativeAttachment(attachment);
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
        return [SentrySDK isEnabled];
    }
}

bool start(const Options &options)
{
    @autoreleasepool {
        currentRelease = options.release;

        SentryOptions *nativeOptions = [[SentryOptions alloc] init];
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
        nativeOptions.sampleRate = @(options.sampleRate);
        nativeOptions.maxBreadcrumbs = static_cast<NSUInteger>(options.maxBreadcrumbs);
        nativeOptions.shutdownTimeInterval = static_cast<NSTimeInterval>(options.shutdownTimeout) / 1000.0;

        Hook beforeSend = options.beforeSend;
        nativeOptions.beforeSend = ^SentryEvent *_Nullable(SentryEvent *event) {
            return runEventHook(event, beforeSend);
        };

        if (options.beforeBreadcrumb) {
            Hook beforeBreadcrumb = options.beforeBreadcrumb;
            nativeOptions.beforeBreadcrumb = ^SentryBreadcrumb *_Nullable(SentryBreadcrumb *breadcrumb) {
                return runBreadcrumbHook(breadcrumb, beforeBreadcrumb);
            };
        }

        if (options.beforeSendLog) {
            Hook beforeSendLog = options.beforeSendLog;
            nativeOptions.beforeSendLog = ^SentryLog *_Nullable(SentryLog *log) {
                return runLogHook(log, beforeSendLog);
            };
        }

        if (options.beforeSendMetric) {
            Hook beforeSendMetric = options.beforeSendMetric;
            nativeOptions.beforeSendMetric = ^SentryObjCMetric *_Nullable(SentryObjCMetric *metric) {
                return runMetricHook(metric, beforeSendMetric);
            };
        }

        if (options.onCrash) {
            Hook onCrash = options.onCrash;
            nativeOptions.onCrashedLastRun = ^(SentryEvent *event) {
                runEventHook(event, onCrash);
            };
        }

        [SentrySDK startWithOptions:nativeOptions];
        if (!options.user.isEmpty()) {
            [SentrySDK setUser:userFromVariantMap(options.user)];
        }
        return [SentrySDK isEnabled];
    }
}

void flush(int timeoutMs)
{
    @autoreleasepool {
        [SentrySDK flush:static_cast<NSTimeInterval>(timeoutMs < 0 ? 0 : timeoutMs) / 1000.0];
    }
}

void close()
{
    @autoreleasepool {
        [SentrySDK close];
    }
}

void setRelease(const QString &release)
{
    currentRelease = release;
}

void setEnvironment(const QString &environment)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setEnvironment:nsStringOrNil(environment)];
        }];
    }
}

void setUser(const QVariantMap &user)
{
    @autoreleasepool {
        SentryUser *nativeUser = userFromVariantMap(user);
        [SentrySDK setUser:nativeUser];
    }
}

void removeUser()
{
    @autoreleasepool {
        [SentrySDK setUser:nil];
    }
}

void setTag(const QString &key, const QString &value)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setTagValue:nsString(value) forKey:nsString(key)];
        }];
    }
}

void removeTag(const QString &key)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope removeTagForKey:nsString(key)];
        }];
    }
}

void setContext(const QString &key, const QVariantMap &context)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setContextValue:dictionaryFromVariantMap(context) forKey:nsString(key)];
        }];
    }
}

void removeContext(const QString &key)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope removeContextForKey:nsString(key)];
        }];
    }
}

void setAttribute(const QString &key, const QVariant &value)
{
    @autoreleasepool {
        id nativeValue = objectFromVariant(value);
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setAttributeValue:nativeValue forKey:nsString(key)];
        }];
    }
}

void removeAttribute(const QString &key)
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope removeAttributeForKey:nsString(key)];
        }];
    }
}

void setFingerprint(const QStringList &fingerprint)
{
    @autoreleasepool {
        NSArray<NSString *> *nativeFingerprint = stringArrayFromStringList(fingerprint);
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setFingerprint:nativeFingerprint];
        }];
    }
}

void clearFingerprint()
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope setFingerprint:nil];
        }];
    }
}

void setAttachments(const QList<Attachment> &attachments)
{
    @autoreleasepool {
        NSArray<SentryAttachment *> *native = nativeAttachments(attachments);
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope clearAttachments];
            for (SentryAttachment *attachment in native) {
                [scope addAttachment:attachment];
            }
        }];
    }
}

void clearAttachments()
{
    @autoreleasepool {
        [SentrySDK configureScope:^(SentryScope *scope) {
            [scope clearAttachments];
        }];
    }
}

void startSession()
{
    @autoreleasepool {
        [SentrySDK startSession];
    }
}

void endSession()
{
    @autoreleasepool {
        [SentrySDK endSession];
    }
}

void addBreadcrumb(const QVariantMap &breadcrumb)
{
    @autoreleasepool {
        [SentrySDK addBreadcrumb:breadcrumbFromVariantMap(breadcrumb)];
    }
}

void log(int level, const QString &message, const QVariantMap &attributes)
{
    @autoreleasepool {
        NSDictionary<NSString *, SentryAttribute *> *nativeAttributes = logAttributesFromVariantMap(attributes);
        SentryLogger *logger = [SentrySDK logger];
        switch (logLevelFromInt(level)) {
        case SentryLogLevelTrace:
            [logger trace:nsString(message) attributes:nativeAttributes];
            break;
        case SentryLogLevelDebug:
            [logger debug:nsString(message) attributes:nativeAttributes];
            break;
        case SentryLogLevelWarn:
            [logger warn:nsString(message) attributes:nativeAttributes];
            break;
        case SentryLogLevelError:
            [logger error:nsString(message) attributes:nativeAttributes];
            break;
        case SentryLogLevelFatal:
            [logger fatal:nsString(message) attributes:nativeAttributes];
            break;
        case SentryLogLevelInfo:
        default:
            [logger info:nsString(message) attributes:nativeAttributes];
            break;
        }
    }
}

void count(const QString &name, quint64 value, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentrySDK metrics] countWithKey:nsString(name)
                                    value:static_cast<NSUInteger>(value)
                               attributes:metricAttributesFromVariantMap(attributes)];
    }
}

void gauge(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentrySDK metrics] gaugeWithKey:nsString(name)
                                    value:value
                                     unit:nsStringOrNil(unit)
                               attributes:metricAttributesFromVariantMap(attributes)];
    }
}

void distribution(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    @autoreleasepool {
        [[SentrySDK metrics] distributionWithKey:nsString(name)
                                           value:value
                                            unit:nsStringOrNil(unit)
                                      attributes:metricAttributesFromVariantMap(attributes)];
    }
}

QString captureEvent(const QVariantMap &event, const QStringList &fingerprint)
{
    @autoreleasepool {
        SentryEvent *nativeEvent = eventFromVariantMap(event);
        SentryId *eventId = nil;
        if (fingerprint.isEmpty()) {
            eventId = [SentrySDK captureEvent:nativeEvent];
        } else {
            NSArray<NSString *> *nativeFingerprint = stringArrayFromStringList(fingerprint);
            eventId = [SentrySDK captureEvent:nativeEvent
                               withScopeBlock:^(SentryScope *scope) {
                                   [scope setFingerprint:nativeFingerprint];
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
        SentryId *nativeAssociatedEventId =
            associatedEventId ? [[SentryId alloc] initWithUUIDString:associatedEventId] : nil;
        SentryFeedback *nativeFeedback = [[SentryFeedback alloc] initWithMessage:message ?: @""
                                                                           name:name
                                                                          email:email
                                                                         source:SentryFeedbackSourceCustom
                                                              associatedEventId:nativeAssociatedEventId
                                                                    attachments:nativeAttachments(attachments)];
        [SentrySDK captureFeedback:nativeFeedback];
    }
}

} // namespace SentryObjCBridge
