#include <SentryQml/private/sentrysdk_p.h>

#include <SentryQml/private/sentryevent_p.h>
#include <SentryQml/private/sentryhint_p.h>

#include <SentryQml/sentry.h>
#include <SentryQml/sentryattachment.h>
#include <SentryQml/sentryhint.h>
#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryuser.h>

#include <include/sentry.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qpointer.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qthread.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef SENTRY_QML_SDK_NAME
#    define SENTRY_QML_SDK_NAME "sentry.native.qml"
#endif

extern "C" void sentry__hint_free(sentry_hint_t *hint);

struct SentrySdkEventHookState
{
    QPointer<Sentry> sentry;
    QPointer<QJSEngine> engine;
    QJSValue callback;
    QThread *thread = nullptr;
    QString propertyName;
};

struct SentrySdkCrashHookState
{
    SentrySdk *sdk = nullptr;
    SentrySdkEventHookState *qmlHook = nullptr;
};

namespace {

thread_local int hookDepth = 0;

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

sentry_value_t nativeValueFromVariant(const QVariant &value)
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
    case QMetaType::QVariantList: {
        sentry_value_t list = sentry_value_new_list();
        const QVariantList values = value.toList();
        for (const QVariant &item : values) {
            sentry_value_append(list, nativeValueFromVariant(item));
        }
        return list;
    }
    case QMetaType::QStringList: {
        sentry_value_t list = sentry_value_new_list();
        const QStringList values = value.toStringList();
        for (const QString &item : values) {
            sentry_value_append(list, nativeValueFromVariant(item));
        }
        return list;
    }
    case QMetaType::QVariantMap: {
        sentry_value_t object = sentry_value_new_object();
        const QVariantMap values = value.toMap();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            const QByteArray key = it.key().toUtf8();
            sentry_value_set_by_key_n(
                object, key.constData(), static_cast<size_t>(key.size()), nativeValueFromVariant(it.value()));
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

QVariant nativeValueToVariant(sentry_value_t value)
{
    char *json = sentry_value_to_json(value);
    if (!json) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json), &error);
    sentry_free(json);

    if (error.error != QJsonParseError::NoError) {
        return {};
    }

    return document.toVariant();
}

sentry_value_t nativeAttributeFromVariant(const QVariant &value)
{
    QVariant attributeValue = value;
    QString unit;

    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap object = value.toMap();
        if (object.contains(QStringLiteral("value")) || object.contains(QStringLiteral("unit"))) {
            attributeValue = object.value(QStringLiteral("value"));
            unit = object.value(QStringLiteral("unit")).toString();
        }
    }

    const QByteArray unitUtf8 = unit.toUtf8();
    return sentry_value_new_attribute_n(nativeValueFromVariant(attributeValue),
                                        unit.isEmpty() ? nullptr : unitUtf8.constData(),
                                        unit.isEmpty() ? 0 : static_cast<size_t>(unitUtf8.size()));
}

sentry_value_t nativeAttributesFromVariantMap(const QVariantMap &attributes)
{
    sentry_value_t nativeAttributes = sentry_value_new_object();
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }

        sentry_value_t attribute = nativeAttributeFromVariant(it.value());
        if (sentry_value_is_null(attribute)) {
            continue;
        }

        const QByteArray key = it.key().toUtf8();
        sentry_value_set_by_key_n(
            nativeAttributes, key.constData(), static_cast<size_t>(key.size()), attribute);
    }
    return nativeAttributes;
}

sentry_value_t nativeFingerprintFromStringList(const QStringList &fingerprint)
{
    sentry_value_t nativeFingerprint = sentry_value_new_list();
    for (const QString &part : fingerprint) {
        const QByteArray utf8 = part.toUtf8();
        sentry_value_append(
            nativeFingerprint, sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size())));
    }
    return nativeFingerprint;
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

void setUtf8Option(const QString &value, void (*setter)(sentry_options_t *, const char *, size_t), sentry_options_t *options)
{
    if (value.isEmpty()) {
        return;
    }

    const QByteArray utf8 = value.toUtf8();
    setter(options, utf8.constData(), static_cast<size_t>(utf8.size()));
}

bool createEventHookState(Sentry *sentry,
                          SentryOptions *options,
                          const QJSValue &callback,
                          const QString &propertyName,
                          bool requireSameThread,
                          std::unique_ptr<SentrySdkEventHookState> *state)
{
    if (callback.isUndefined() || callback.isNull()) {
        return true;
    }

    if (!callback.isCallable()) {
        emit sentry->errorOccurred(QStringLiteral("SentryOptions.%1 must be a function.").arg(propertyName));
        return false;
    }

    QJSEngine *engine = qjsEngine(options);
    if (!engine) {
        emit sentry->errorOccurred(
            QStringLiteral("SentryOptions.%1 requires options created by a QML engine.").arg(propertyName));
        return false;
    }

    auto hookState = std::make_unique<SentrySdkEventHookState>();
    hookState->sentry = sentry;
    hookState->engine = engine;
    hookState->callback = callback;
    hookState->thread = requireSameThread ? QThread::currentThread() : nullptr;
    hookState->propertyName = propertyName;
    *state = std::move(hookState);
    return true;
}

sentry_value_t invokeValueHook(sentry_value_t value, SentrySdkEventHookState *state)
{
    if (!state || !state->engine || !state->callback.isCallable()) {
        return value;
    }

    if (state->thread && QThread::currentThread() != state->thread) {
        return value;
    }

    const QScopedValueRollback<int> rollback(hookDepth, hookDepth + 1);
    QJSValue scriptValue = SentryEvent::toScriptValue(state->engine, nativeValueToVariant(value));
    if (scriptValue.isUndefined()) {
        return value;
    }

    QJSValue result = state->callback.call({scriptValue});
    if (result.isError()) {
        if (state->sentry) {
            emit state->sentry->errorOccurred(
                QStringLiteral("SentryOptions.%1 failed: %2").arg(state->propertyName, result.toString()));
        }
        return value;
    }

    if (result.isUndefined() || (result.isBool() && result.toBool())) {
        return value;
    }

    if (result.isNull() || (result.isBool() && !result.toBool())) {
        sentry_value_decref(value);
        return sentry_value_new_null();
    }

    sentry_value_t replacement = nativeValueFromVariant(result.toVariant());
    if (sentry_value_is_null(replacement)) {
        sentry_value_decref(value);
        return replacement;
    }

    sentry_value_decref(value);
    return replacement;
}

sentry_value_t beforeSendCallback(sentry_value_t event, void *, void *userData)
{
    return invokeValueHook(event, static_cast<SentrySdkEventHookState *>(userData));
}

sentry_value_t onCrashCallback(const sentry_ucontext_t *, sentry_value_t event, void *userData)
{
    auto *state = static_cast<SentrySdkCrashHookState *>(userData);
    if (!state) {
        return event;
    }
    if (state->sdk) {
        QVariantMap eventMap = nativeValueToVariant(event).toMap();
        state->sdk->applyFingerprintToEvent(&eventMap);
        if (!eventMap.isEmpty()) {
            sentry_value_decref(event);
            event = nativeValueFromVariant(eventMap);
        }
    }
    return invokeValueHook(event, state->qmlHook);
}

sentry_value_t beforeBreadcrumbCallback(sentry_value_t breadcrumb, void *userData)
{
    return invokeValueHook(breadcrumb, static_cast<SentrySdkEventHookState *>(userData));
}

sentry_value_t beforeSendLogCallback(sentry_value_t log, void *userData)
{
    return invokeValueHook(log, static_cast<SentrySdkEventHookState *>(userData));
}

sentry_value_t beforeSendMetricCallback(sentry_value_t metric, void *userData)
{
    return invokeValueHook(metric, static_cast<SentrySdkEventHookState *>(userData));
}

QString levelNameFromString(const QString &level)
{
    return SentryEvent::levelNameFromString(level);
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

sentry_value_t breadcrumbFromVariantMap(const QVariantMap &breadcrumb)
{
    const QString type = breadcrumb.value(QStringLiteral("type"), QStringLiteral("default")).toString();
    const QString message = breadcrumb.value(QStringLiteral("message")).toString();
    const QByteArray typeUtf8 = type.toUtf8();
    const QByteArray messageUtf8 = message.toUtf8();

    sentry_value_t nativeBreadcrumb = sentry_value_new_breadcrumb_n(
        type.isEmpty() ? nullptr : typeUtf8.constData(),
        type.isEmpty() ? 0 : static_cast<size_t>(typeUtf8.size()),
        message.isEmpty() ? nullptr : messageUtf8.constData(),
        message.isEmpty() ? 0 : static_cast<size_t>(messageUtf8.size()));

    for (auto it = breadcrumb.cbegin(); it != breadcrumb.cend(); ++it) {
        if (it.key().isEmpty() || it.key() == QLatin1String("type") || it.key() == QLatin1String("message")) {
            continue;
        }

        if (it.key() == QLatin1String("level")) {
            setStringValue(nativeBreadcrumb, "level", levelNameFromString(it.value().toString()));
            continue;
        }

        const QByteArray key = it.key().toUtf8();
        sentry_value_set_by_key_n(
            nativeBreadcrumb, key.constData(), static_cast<size_t>(key.size()), nativeValueFromVariant(it.value()));
    }

    return nativeBreadcrumb;
}

QVariantMap userFromVariantMap(const QVariantMap &user)
{
    QVariantMap nativeUser = user;
    const QString ipAddressKey = QStringLiteral("ipAddress");
    const QString protocolIpAddressKey = QStringLiteral("ip_address");
    if (nativeUser.contains(ipAddressKey) && !nativeUser.contains(protocolIpAddressKey)) {
        nativeUser.insert(protocolIpAddressKey, nativeUser.value(ipAddressKey));
        nativeUser.remove(ipAddressKey);
    }
    return nativeUser;
}

QString feedbackValue(const QVariantMap &feedback,
                      const QString &key,
                      const QString &fallbackKey = {},
                      const QString &secondFallbackKey = {})
{
    if (feedback.contains(key)) {
        return feedback.value(key).toString();
    }
    if (!fallbackKey.isEmpty() && feedback.contains(fallbackKey)) {
        return feedback.value(fallbackKey).toString();
    }
    if (!secondFallbackKey.isEmpty() && feedback.contains(secondFallbackKey)) {
        return feedback.value(secondFallbackKey).toString();
    }
    return {};
}

struct NativeHintDeleter
{
    void operator()(sentry_hint_t *hint) const { sentry__hint_free(hint); }
};

void setNativeAttachmentFilename(sentry_attachment_t *attachment, const QString &filename)
{
    if (!attachment || filename.isEmpty()) {
        return;
    }

#if defined(Q_OS_WIN)
    const std::wstring wideFilename = filename.toStdWString();
    sentry_attachment_set_filenamew_n(attachment, wideFilename.c_str(), wideFilename.size());
#else
    const QByteArray encodedFilename = QFile::encodeName(filename);
    sentry_attachment_set_filename_n(
        attachment, encodedFilename.constData(), static_cast<size_t>(encodedFilename.size()));
#endif
}

void setNativeAttachmentContentType(sentry_attachment_t *attachment, const QString &contentType)
{
    if (!attachment || contentType.isEmpty()) {
        return;
    }

    const QByteArray utf8ContentType = contentType.toUtf8();
    sentry_attachment_set_content_type_n(
        attachment, utf8ContentType.constData(), static_cast<size_t>(utf8ContentType.size()));
}

sentry_attachment_t *attachHintFile(sentry_hint_t *hint, const QString &path)
{
    const QString nativePath = QDir::toNativeSeparators(path);
#if defined(Q_OS_WIN)
    const std::wstring widePath = nativePath.toStdWString();
    return sentry_hint_attach_filew_n(hint, widePath.c_str(), widePath.size());
#else
    const QByteArray encodedPath = QFile::encodeName(nativePath);
    return sentry_hint_attach_file_n(hint, encodedPath.constData(), static_cast<size_t>(encodedPath.size()));
#endif
}

sentry_attachment_t *attachHintBytes(sentry_hint_t *hint, const QByteArray &bytes, const QString &filename)
{
#if defined(Q_OS_WIN)
    const std::wstring wideFilename = filename.toStdWString();
    return sentry_hint_attach_bytesw_n(
        hint, bytes.constData(), static_cast<size_t>(bytes.size()), wideFilename.c_str(), wideFilename.size());
#else
    const QByteArray encodedFilename = QFile::encodeName(filename);
    return sentry_hint_attach_bytes_n(hint,
                                      bytes.constData(),
                                      static_cast<size_t>(bytes.size()),
                                      encodedFilename.constData(),
                                      static_cast<size_t>(encodedFilename.size()));
#endif
}

sentry_level_t logLevelFromInt(int level)
{
    switch (level) {
    case SENTRY_LEVEL_TRACE:
        return SENTRY_LEVEL_TRACE;
    case SENTRY_LEVEL_DEBUG:
        return SENTRY_LEVEL_DEBUG;
    case SENTRY_LEVEL_INFO:
        return SENTRY_LEVEL_INFO;
    case SENTRY_LEVEL_WARNING:
        return SENTRY_LEVEL_WARNING;
    case SENTRY_LEVEL_ERROR:
        return SENTRY_LEVEL_ERROR;
    case SENTRY_LEVEL_FATAL:
        return SENTRY_LEVEL_FATAL;
    default:
        return SENTRY_LEVEL_INFO;
    }
}

bool checkMetricResult(Sentry *sentry, sentry_metrics_result_t result)
{
    if (result == SENTRY_METRICS_RESULT_FAILED && sentry) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric could not be queued."));
    }
    return result == SENTRY_METRICS_RESULT_SUCCESS;
}

} // namespace

SentrySdk *SentrySdk::instance()
{
    static SentrySdk sdk;
    return &sdk;
}

SentrySdk::SentrySdk(QObject *parent)
    : QObject(parent)
{
}

SentrySdk::~SentrySdk()
{
    if (m_initialized && QCoreApplication::instance() && !QCoreApplication::closingDown()) {
        close();
    }
}

bool SentrySdk::isInitialized() const
{
    return m_initialized;
}

bool SentrySdk::init(Sentry *sentry, SentryOptions *options)
{
    if (m_initialized) {
        return true;
    }

    if (!sentry) {
        return false;
    }

    if (!options) {
        emit sentry->errorOccurred(QStringLiteral("Sentry.init requires a SentryOptions object."));
        return false;
    }

    if (!std::isfinite(options->sampleRate()) || options->sampleRate() < 0.0 || options->sampleRate() > 1.0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry sampleRate must be between 0.0 and 1.0."));
        return false;
    }

    if (options->maxBreadcrumbs() < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry maxBreadcrumbs must not be negative."));
        return false;
    }

    if (options->shutdownTimeout() < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry shutdownTimeout must not be negative."));
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeBreadcrumbState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeBreadcrumb(),
                              QStringLiteral("beforeBreadcrumb"),
                              true,
                              &beforeBreadcrumbState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendLogState;
    if (!createEventHookState(
            sentry, options, options->beforeSendLog(), QStringLiteral("beforeSendLog"), true, &beforeSendLogState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendMetricState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeSendMetric(),
                              QStringLiteral("beforeSendMetric"),
                              true,
                              &beforeSendMetricState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendState;
    if (!createEventHookState(
            sentry, options, options->beforeSend(), QStringLiteral("beforeSend"), true, &beforeSendState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> onCrashState;
    if (!createEventHookState(sentry, options, options->onCrash(), QStringLiteral("onCrash"), false, &onCrashState)) {
        return false;
    }
    auto crashHookState = std::make_unique<SentrySdkCrashHookState>();
    crashHookState->sdk = this;
    crashHookState->qmlHook = onCrashState.get();

    sentry_options_t *nativeOptions = sentry_options_new();
    if (!nativeOptions) {
        return false;
    }

    setUtf8Option(options->dsn(), sentry_options_set_dsn_n, nativeOptions);
    setUtf8Option(options->release(), sentry_options_set_release_n, nativeOptions);
    setUtf8Option(options->environment(), sentry_options_set_environment_n, nativeOptions);
    setUtf8Option(options->dist(), sentry_options_set_dist_n, nativeOptions);
    sentry_options_set_debug(nativeOptions, options->debug() ? 1 : 0);
    sentry_options_set_enable_logs(nativeOptions, options->enableLogs() ? 1 : 0);
    sentry_options_set_enable_metrics(nativeOptions, options->enableMetrics() ? 1 : 0);
    sentry_options_set_auto_session_tracking(nativeOptions, options->autoSessionTracking() ? 1 : 0);
    sentry_options_set_sample_rate(nativeOptions, options->sampleRate());
    sentry_options_set_max_breadcrumbs(nativeOptions, static_cast<size_t>(options->maxBreadcrumbs()));
    sentry_options_set_shutdown_timeout(nativeOptions, static_cast<uint64_t>(options->shutdownTimeout()));
    sentry_options_set_sdk_name(nativeOptions, SENTRY_QML_SDK_NAME);

    if (beforeBreadcrumbState) {
        sentry_options_set_before_breadcrumb(nativeOptions, beforeBreadcrumbCallback, beforeBreadcrumbState.get());
    }

    if (beforeSendLogState) {
        sentry_options_set_before_send_log(nativeOptions, beforeSendLogCallback, beforeSendLogState.get());
    }

    if (beforeSendMetricState) {
        sentry_options_set_before_send_metric(nativeOptions, beforeSendMetricCallback, beforeSendMetricState.get());
    }

    if (beforeSendState) {
        sentry_options_set_before_send(nativeOptions, beforeSendCallback, beforeSendState.get());
    }

    sentry_options_set_on_crash(nativeOptions, onCrashCallback, crashHookState.get());

    if (!options->databasePath().isEmpty()) {
        const QString nativePath = QDir::toNativeSeparators(options->databasePath());
#if defined(Q_OS_WIN)
        const std::wstring widePath = nativePath.toStdWString();
        sentry_options_set_database_pathw(nativeOptions, widePath.c_str());
#else
        const QByteArray encodedPath = QFile::encodeName(nativePath);
        sentry_options_set_database_path_n(nativeOptions, encodedPath.constData(), static_cast<size_t>(encodedPath.size()));
#endif
    }

    const int result = sentry_init(nativeOptions);
    if (result != 0) {
        beforeBreadcrumbState.reset();
        beforeSendLogState.reset();
        beforeSendMetricState.reset();
        beforeSendState.reset();
        onCrashState.reset();
        crashHookState.reset();
        emit sentry->errorOccurred(QStringLiteral("sentry_init failed with code %1.").arg(result));
        return false;
    }

    m_beforeBreadcrumbState = std::move(beforeBreadcrumbState);
    m_beforeSendLogState = std::move(beforeSendLogState);
    m_beforeSendMetricState = std::move(beforeSendMetricState);
    m_beforeSendState = std::move(beforeSendState);
    m_onCrashState = std::move(onCrashState);
    m_crashHookState = std::move(crashHookState);
    const SentryUser *user = options->user();
    if (user && !user->isEmpty()) {
        sentry_set_user(nativeValueFromVariant(user->toVariantMap()));
    }
    setInitialized(true);
    connectToApplicationShutdown();
    return true;
}

bool SentrySdk::flush(int timeoutMs)
{
    if (!m_initialized) {
        return true;
    }

    return sentry_flush(timeoutMs < 0 ? 0 : static_cast<uint64_t>(timeoutMs)) == 0;
}

bool SentrySdk::close()
{
    if (!m_initialized) {
        return true;
    }

    sentry_close();
    QObject::disconnect(m_applicationShutdownConnection);
    m_applicationShutdownConnection = {};
    m_beforeBreadcrumbState.reset();
    m_beforeSendLogState.reset();
    m_beforeSendMetricState.reset();
    m_beforeSendState.reset();
    m_crashHookState.reset();
    m_onCrashState.reset();
    m_fingerprint.clear();
    invalidateAttachments();
    setInitialized(false);
    return true;
}

void SentrySdk::closeBeforeApplicationShutdown()
{
    close();
}

void SentrySdk::connectToApplicationShutdown()
{
    if (m_applicationShutdownConnection || !QCoreApplication::instance()) {
        return;
    }

    m_applicationShutdownConnection = QObject::connect(QCoreApplication::instance(),
                                                       &QCoreApplication::aboutToQuit,
                                                       this,
                                                       &SentrySdk::closeBeforeApplicationShutdown,
                                                       Qt::DirectConnection);
}

void SentrySdk::detachSentry(Sentry *sentry)
{
    auto detach = [sentry](std::unique_ptr<SentrySdkEventHookState> &state)
    {
        if (state && state->sentry == sentry) {
            state->sentry = nullptr;
            state->engine = nullptr;
            state->callback = QJSValue();
        }
    };

    detach(m_beforeSendState);
    detach(m_beforeBreadcrumbState);
    detach(m_beforeSendLogState);
    detach(m_beforeSendMetricState);
    detach(m_onCrashState);
}

bool SentrySdk::ensureCanCall(Sentry *sentry,
                              const char *method,
                              const char *action,
                              const char *hookType) const
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.%1 cannot be called from Sentry %2.")
                                           .arg(QString::fromLatin1(method), QString::fromLatin1(hookType)));
        }
        return false;
    }

    return ensureInitialized(sentry, action);
}

bool SentrySdk::ensureInitialized(Sentry *sentry, const char *action) const
{
    if (m_initialized) {
        return true;
    }

    if (sentry) {
        emit sentry->errorOccurred(
            QStringLiteral("Sentry must be initialized before %1.").arg(QString::fromLatin1(action)));
    }
    return false;
}

bool SentrySdk::setRelease(Sentry *sentry, const QString &release)
{
    if (!ensureCanCall(sentry, "setRelease", "setting releases")) {
        return false;
    }

    if (release.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry release must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Release = release.toUtf8();
    sentry_set_release_n(utf8Release.constData(), static_cast<size_t>(utf8Release.size()));
    return true;
}

bool SentrySdk::setEnvironment(Sentry *sentry, const QString &environment)
{
    if (!ensureCanCall(sentry, "setEnvironment", "setting environments")) {
        return false;
    }

    if (environment.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry environment must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Environment = environment.toUtf8();
    sentry_set_environment_n(utf8Environment.constData(), static_cast<size_t>(utf8Environment.size()));
    return true;
}

bool SentrySdk::setUser(Sentry *sentry, const QVariantMap &user)
{
    if (!ensureCanCall(sentry, "setUser", "setting users")) {
        return false;
    }

    if (user.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry user must not be empty."));
        }
        return false;
    }

    sentry_set_user(nativeValueFromVariant(userFromVariantMap(user)));
    return true;
}

bool SentrySdk::removeUser(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeUser", "removing users")) {
        return false;
    }

    sentry_remove_user();
    return true;
}

bool SentrySdk::setTag(Sentry *sentry, const QString &key, const QString &value)
{
    if (!ensureCanCall(sentry, "setTag", "setting tags")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    const QByteArray utf8Value = value.toUtf8();
    sentry_set_tag_n(utf8Key.constData(),
                     static_cast<size_t>(utf8Key.size()),
                     utf8Value.constData(),
                     static_cast<size_t>(utf8Value.size()));
    return true;
}

bool SentrySdk::removeTag(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeTag", "removing tags")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    sentry_remove_tag_n(utf8Key.constData(), static_cast<size_t>(utf8Key.size()));
    return true;
}

bool SentrySdk::setContext(Sentry *sentry, const QString &key, const QVariantMap &context)
{
    if (!ensureCanCall(sentry, "setContext", "setting contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    sentry_set_context_n(utf8Key.constData(),
                         static_cast<size_t>(utf8Key.size()),
                         nativeValueFromVariant(context));
    return true;
}

bool SentrySdk::removeContext(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeContext", "removing contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    sentry_remove_context_n(utf8Key.constData(), static_cast<size_t>(utf8Key.size()));
    return true;
}

bool SentrySdk::setAttribute(Sentry *sentry, const QString &key, const QVariant &value)
{
    if (!ensureCanCall(sentry, "setAttribute", "setting attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        }
        return false;
    }

    sentry_value_t attribute = nativeAttributeFromVariant(value);
    if (sentry_value_is_null(attribute)) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry attribute value must be a boolean, number, string, "
                                                      "or list of booleans, numbers, or strings."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    sentry_set_attribute_n(utf8Key.constData(), static_cast<size_t>(utf8Key.size()), attribute);
    return true;
}

bool SentrySdk::removeAttribute(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeAttribute", "removing attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        }
        return false;
    }

    const QByteArray utf8Key = key.toUtf8();
    sentry_remove_attribute_n(utf8Key.constData(), static_cast<size_t>(utf8Key.size()));
    return true;
}

bool SentrySdk::setFingerprint(Sentry *sentry, const QStringList &fingerprint)
{
    if (!ensureCanCall(sentry, "setFingerprint", "setting fingerprints")) {
        return false;
    }

    if (fingerprint.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry fingerprint must not be empty."));
        }
        return false;
    }

    m_fingerprint = fingerprint;
    return true;
}

bool SentrySdk::removeFingerprint(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeFingerprint", "removing fingerprints")) {
        return false;
    }

    m_fingerprint.clear();
    sentry_remove_fingerprint();
    return true;
}

SentryAttachment *SentrySdk::attachFile(Sentry *sentry, const QString &path, const QString &contentType)
{
    if (!ensureCanCall(sentry, "attachFile", "attaching files")) {
        return nullptr;
    }

    if (path.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry attachment path must not be empty."));
        }
        return nullptr;
    }

    sentry_attachment_t *attachment = nullptr;
    const QString nativePath = QDir::toNativeSeparators(path);
#if defined(Q_OS_WIN)
    const std::wstring widePath = nativePath.toStdWString();
    attachment = sentry_attach_filew_n(widePath.c_str(), widePath.size());
#else
    const QByteArray encodedPath = QFile::encodeName(nativePath);
    attachment = sentry_attach_file_n(encodedPath.constData(), static_cast<size_t>(encodedPath.size()));
#endif

    if (!attachment) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry file attachment could not be added."));
        }
        return nullptr;
    }

    auto *wrapper = new SentryAttachment(attachment, sentry);
    if (!contentType.isEmpty()) {
        wrapper->setContentType(contentType);
    }
    trackAttachment(wrapper);
    return wrapper;
}

SentryAttachment *SentrySdk::attachBytes(Sentry *sentry,
                                         const QByteArray &bytes,
                                         const QString &filename,
                                         const QString &contentType)
{
    if (!ensureCanCall(sentry, "attachBytes", "attaching bytes")) {
        return nullptr;
    }

    if (bytes.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment must not be empty."));
        }
        return nullptr;
    }

    if (filename.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment filename must not be empty."));
        }
        return nullptr;
    }

    sentry_attachment_t *attachment = nullptr;
#if defined(Q_OS_WIN)
    const std::wstring wideFilename = filename.toStdWString();
    attachment = sentry_attach_bytesw_n(
        bytes.constData(), static_cast<size_t>(bytes.size()), wideFilename.c_str(), wideFilename.size());
#else
    const QByteArray encodedFilename = QFile::encodeName(filename);
    attachment = sentry_attach_bytes_n(bytes.constData(),
                                       static_cast<size_t>(bytes.size()),
                                       encodedFilename.constData(),
                                       static_cast<size_t>(encodedFilename.size()));
#endif

    if (!attachment) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment could not be added."));
        }
        return nullptr;
    }

    auto *wrapper = new SentryAttachment(attachment, sentry);
    wrapper->setFilename(filename);
    if (!contentType.isEmpty()) {
        wrapper->setContentType(contentType);
    }
    trackAttachment(wrapper);
    return wrapper;
}

bool SentrySdk::clearAttachments(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "clearAttachments", "clearing attachments")) {
        return false;
    }

    sentry_clear_attachments();
    invalidateAttachments();
    return true;
}

void SentrySdk::trackAttachment(SentryAttachment *attachment)
{
    if (!attachment) {
        return;
    }

    m_attachments.append(attachment);
}

void SentrySdk::detachAttachment(SentryAttachment *attachment)
{
    m_attachments.removeAll(attachment);
}

void SentrySdk::invalidateAttachments()
{
    const QList<SentryAttachment *> attachments = m_attachments;
    m_attachments.clear();
    for (SentryAttachment *attachment : attachments) {
        if (attachment) {
            attachment->invalidate();
        }
    }
}

void SentrySdk::setAttachmentFilename(SentryAttachment *attachment, const QString &filename)
{
    if (!attachment || !attachment->handle()) {
        return;
    }

    auto *handle = static_cast<sentry_attachment_t *>(attachment->handle());
#if defined(Q_OS_WIN)
    const std::wstring wideFilename = filename.toStdWString();
    sentry_attachment_set_filenamew_n(handle, wideFilename.c_str(), wideFilename.size());
#else
    const QByteArray encodedFilename = QFile::encodeName(filename);
    sentry_attachment_set_filename_n(
        handle, encodedFilename.constData(), static_cast<size_t>(encodedFilename.size()));
#endif
}

void SentrySdk::setAttachmentContentType(SentryAttachment *attachment, const QString &contentType)
{
    if (!attachment || !attachment->handle()) {
        return;
    }

    const QByteArray utf8ContentType = contentType.toUtf8();
    sentry_attachment_set_content_type_n(static_cast<sentry_attachment_t *>(attachment->handle()),
                                         utf8ContentType.constData(),
                                         static_cast<size_t>(utf8ContentType.size()));
}

bool SentrySdk::removeAttachment(Sentry *sentry, SentryAttachment *attachment)
{
    if (!ensureCanCall(sentry, "removeAttachment", "removing attachments")) {
        return false;
    }

    if (!attachment || !attachment->handle()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry attachment is not valid."));
        }
        return false;
    }

    sentry_remove_attachment(static_cast<sentry_attachment_t *>(attachment->handle()));
    detachAttachment(attachment);
    attachment->invalidate();
    return true;
}

bool SentrySdk::startSession(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "startSession", "starting sessions")) {
        return false;
    }

    sentry_start_session();
    return true;
}

bool SentrySdk::endSession(Sentry *sentry, int status)
{
    if (!ensureCanCall(sentry, "endSession", "ending sessions")) {
        return false;
    }

    switch (status) {
    case -1:
        sentry_end_session();
        break;
    case SENTRY_SESSION_STATUS_OK:
    case SENTRY_SESSION_STATUS_CRASHED:
    case SENTRY_SESSION_STATUS_ABNORMAL:
    case SENTRY_SESSION_STATUS_EXITED:
        sentry_end_session_with_status(static_cast<sentry_session_status_t>(status));
        break;
    default:
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Unsupported Sentry session status."));
        }
        return false;
    }

    return true;
}

bool SentrySdk::addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb)
{
    if (!ensureCanCall(sentry, "addBreadcrumb", "adding breadcrumbs")) {
        return false;
    }

    if (breadcrumb.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry breadcrumb must not be empty."));
        }
        return false;
    }

    sentry_add_breadcrumb(breadcrumbFromVariantMap(breadcrumb));
    return true;
}

bool SentrySdk::log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "log", "logging", "hooks")) {
        return false;
    }

    if (message.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry log message must not be empty."));
        }
        return false;
    }

    const QByteArray body = message.toUtf8();
    const log_return_value_t result
        = sentry_log(logLevelFromInt(level), body.constData(), nativeAttributesFromVariantMap(attributes));
    if (result == SENTRY_LOG_RETURN_FAILED && sentry) {
        emit sentry->errorOccurred(QStringLiteral("Sentry log could not be queued."));
    }
    return result == SENTRY_LOG_RETURN_SUCCESS;
}

bool SentrySdk::count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "count", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        }
        return false;
    }

    const QByteArray metricName = name.toUtf8();
    const sentry_metrics_result_t result = sentry_metrics_count(
        metricName.constData(), static_cast<int64_t>(value), nativeAttributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

bool SentrySdk::gauge(Sentry *sentry,
                      const QString &name,
                      double value,
                      const QString &unit,
                      const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "gauge", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        }
        return false;
    }

    if (!std::isfinite(value)) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        }
        return false;
    }

    const QByteArray metricName = name.toUtf8();
    const QByteArray metricUnit = unit.toUtf8();
    const sentry_metrics_result_t result = sentry_metrics_gauge(metricName.constData(),
                                                               value,
                                                               unit.isEmpty() ? nullptr : metricUnit.constData(),
                                                               nativeAttributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

bool SentrySdk::distribution(Sentry *sentry,
                             const QString &name,
                             double value,
                             const QString &unit,
                             const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "distribution", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        }
        return false;
    }

    if (!std::isfinite(value)) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        }
        return false;
    }

    const QByteArray metricName = name.toUtf8();
    const QByteArray metricUnit = unit.toUtf8();
    const sentry_metrics_result_t result
        = sentry_metrics_distribution(metricName.constData(),
                                      value,
                                      unit.isEmpty() ? nullptr : metricUnit.constData(),
                                      nativeAttributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

QString SentrySdk::captureMessage(Sentry *sentry, const QString &message, const QString &level)
{
    if (!ensureInitialized(sentry, "capturing messages")) {
        return {};
    }

    if (message.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry message must not be empty."));
        }
        return {};
    }

    const QVariantMap event = {
        {QStringLiteral("level"), SentryEvent::levelNameFromString(level)},
        {QStringLiteral("logger"), QStringLiteral("qml")},
        {QStringLiteral("message"),
         QVariantMap{
             {QStringLiteral("formatted"), message},
         }},
    };

    return captureEvent(sentry, event, SentrySdkCaptureMode::Manual);
}

bool SentrySdk::captureFeedback(Sentry *sentry, const QVariantMap &feedback, SentryHint *hint)
{
    if (!ensureCanCall(sentry, "captureFeedback", "capturing feedback", "hooks")) {
        return false;
    }

    const QString message = feedbackValue(feedback, QStringLiteral("message"));
    if (message.trimmed().isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry feedback message must not be empty."));
        }
        return false;
    }

    const QString contactEmail = feedbackValue(feedback,
                                               QStringLiteral("email"),
                                               QStringLiteral("contactEmail"),
                                               QStringLiteral("contact_email"));
    const QString name = feedbackValue(feedback, QStringLiteral("name"));
    const QString associatedEventId = feedbackValue(feedback,
                                                    QStringLiteral("associatedEventId"),
                                                    QStringLiteral("associated_event_id"),
                                                    QStringLiteral("eventId"))
                                          .trimmed();

    sentry_uuid_t nativeAssociatedEventId = sentry_uuid_nil();
    const sentry_uuid_t *nativeAssociatedEventIdPtr = nullptr;
    if (!associatedEventId.isEmpty()) {
        const QByteArray eventId = associatedEventId.toUtf8();
        nativeAssociatedEventId = sentry_uuid_from_string_n(eventId.constData(), static_cast<size_t>(eventId.size()));
        if (sentry_uuid_is_nil(&nativeAssociatedEventId)) {
            if (sentry) {
                emit sentry->errorOccurred(QStringLiteral("Sentry feedback associatedEventId must be a valid event ID."));
            }
            return false;
        }
        nativeAssociatedEventIdPtr = &nativeAssociatedEventId;
    }

    const QByteArray messageUtf8 = message.toUtf8();
    const QByteArray contactEmailUtf8 = contactEmail.toUtf8();
    const QByteArray nameUtf8 = name.toUtf8();
    sentry_value_t nativeFeedback =
        sentry_value_new_feedback_n(messageUtf8.constData(),
                                    static_cast<size_t>(messageUtf8.size()),
                                    contactEmail.isEmpty() ? nullptr : contactEmailUtf8.constData(),
                                    static_cast<size_t>(contactEmailUtf8.size()),
                                    name.isEmpty() ? nullptr : nameUtf8.constData(),
                                    static_cast<size_t>(nameUtf8.size()),
                                    nativeAssociatedEventIdPtr);

    std::unique_ptr<sentry_hint_t, NativeHintDeleter> nativeHint;
    if (hint && !hint->d->attachments.isEmpty()) {
        nativeHint.reset(sentry_hint_new());
        if (!nativeHint) {
            sentry_value_decref(nativeFeedback);
            if (sentry) {
                emit sentry->errorOccurred(QStringLiteral("Sentry hint could not be created."));
            }
            return false;
        }

        for (const SentryHintAttachment &attachment : hint->d->attachments) {
            sentry_attachment_t *nativeAttachment = nullptr;
            if (attachment.type == SentryHintAttachmentType::File) {
                nativeAttachment = attachHintFile(nativeHint.get(), attachment.path);
                setNativeAttachmentFilename(nativeAttachment, attachment.filename);
            } else {
                nativeAttachment = attachHintBytes(nativeHint.get(), attachment.bytes, attachment.filename);
            }

            if (!nativeAttachment) {
                sentry_value_decref(nativeFeedback);
                if (sentry) {
                    emit sentry->errorOccurred(QStringLiteral("Sentry hint attachment could not be added."));
                }
                return false;
            }

            setNativeAttachmentContentType(nativeAttachment, attachment.contentType);
        }
    }

    if (nativeHint) {
        sentry_capture_feedback_with_hint(nativeFeedback, nativeHint.release());
    } else {
        sentry_capture_feedback(nativeFeedback);
    }
    return true;
}

QString SentrySdk::captureEvent(Sentry *sentry, const QVariantMap &event, SentrySdkCaptureMode mode)
{
    if (hookDepth > 0) {
        if (mode == SentrySdkCaptureMode::Manual && sentry) {
            emit sentry->errorOccurred(
                QStringLiteral("Sentry.capture* cannot be called from Sentry event hooks."));
        }
        return {};
    }

    if (!m_initialized) {
        return {};
    }

    if (m_fingerprint.isEmpty()) {
        return eventIdFromUuid(sentry_capture_event(nativeValueFromVariant(event)));
    }

    sentry_scope_t *scope = sentry_local_scope_new();
    if (!scope) {
        QVariantMap eventWithFingerprint = event;
        applyFingerprintToEvent(&eventWithFingerprint);
        return eventIdFromUuid(sentry_capture_event(nativeValueFromVariant(eventWithFingerprint)));
    }

    sentry_scope_set_fingerprints(scope, nativeFingerprintFromStringList(m_fingerprint));
    return eventIdFromUuid(sentry_capture_event_with_scope(nativeValueFromVariant(event), scope));
}

void SentrySdk::applyFingerprintToEvent(QVariantMap *event) const
{
    if (!event || m_fingerprint.isEmpty()) {
        return;
    }

    event->insert(QStringLiteral("fingerprint"), m_fingerprint);
}

void SentrySdk::setInitialized(bool initialized)
{
    if (m_initialized == initialized) {
        return;
    }

    m_initialized = initialized;
    emit initializedChanged();
}
