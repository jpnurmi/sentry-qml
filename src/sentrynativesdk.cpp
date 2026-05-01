#include <SentryQml/private/sentrynativesdk_p.h>

#include <SentryQml/private/sentryevent_p.h>

#include <SentryQml/sentry.h>
#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryuser.h>

#include <include/sentry.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qstringlist.h>
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

struct SentryNativeEventHookState
{
    QPointer<Sentry> sentry;
    QPointer<QJSEngine> engine;
    QJSValue callback;
    QThread *thread = nullptr;
    QString propertyName;
};

struct SentryNativeCrashHookState
{
    SentryNativeSdk *sdk = nullptr;
    SentryNativeEventHookState *qmlHook = nullptr;
};

struct SentryNativeValue
{
    SentryNativeValue()
        : value(sentry_value_new_null())
    {
    }

    ~SentryNativeValue()
    {
        sentry_value_decref(value);
    }

    void reset(sentry_value_t newValue = sentry_value_new_null())
    {
        sentry_value_decref(value);
        value = newValue;
    }

    sentry_value_t ref() const
    {
        sentry_value_incref(value);
        return value;
    }

    sentry_value_t value;
};

namespace {

thread_local int hookDepth = 0;

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
                          std::unique_ptr<SentryNativeEventHookState> *state)
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

    auto hookState = std::make_unique<SentryNativeEventHookState>();
    hookState->sentry = sentry;
    hookState->engine = engine;
    hookState->callback = callback;
    hookState->thread = requireSameThread ? QThread::currentThread() : nullptr;
    hookState->propertyName = propertyName;
    *state = std::move(hookState);
    return true;
}

sentry_value_t invokeValueHook(sentry_value_t value, SentryNativeEventHookState *state)
{
    if (!state || !state->engine || !state->callback.isCallable()) {
        return value;
    }

    if (state->thread && QThread::currentThread() != state->thread) {
        return value;
    }

    const QScopedValueRollback<int> rollback(hookDepth, hookDepth + 1);
    QJSValue scriptValue = SentryEvent::toScriptValue(state->engine, value);
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

    sentry_value_t replacement = SentryEvent::fromVariant(result.toVariant());
    if (sentry_value_is_null(replacement)) {
        sentry_value_decref(value);
        return replacement;
    }

    sentry_value_decref(value);
    return replacement;
}

sentry_value_t beforeSendCallback(sentry_value_t event, void *, void *userData)
{
    return invokeValueHook(event, static_cast<SentryNativeEventHookState *>(userData));
}

sentry_value_t onCrashCallback(const sentry_ucontext_t *, sentry_value_t event, void *userData)
{
    auto *state = static_cast<SentryNativeCrashHookState *>(userData);
    if (!state) {
        return event;
    }
    if (state->sdk) {
        state->sdk->applyFingerprintToEvent(event);
    }
    return invokeValueHook(event, state->qmlHook);
}

sentry_value_t beforeBreadcrumbCallback(sentry_value_t breadcrumb, void *userData)
{
    return invokeValueHook(breadcrumb, static_cast<SentryNativeEventHookState *>(userData));
}

sentry_value_t beforeSendLogCallback(sentry_value_t log, void *userData)
{
    return invokeValueHook(log, static_cast<SentryNativeEventHookState *>(userData));
}

sentry_value_t beforeSendMetricCallback(sentry_value_t metric, void *userData)
{
    return invokeValueHook(metric, static_cast<SentryNativeEventHookState *>(userData));
}

QString levelNameFromString(const QString &level)
{
    switch (SentryEvent::levelFromString(level)) {
    case SENTRY_LEVEL_TRACE:
        return QStringLiteral("trace");
    case SENTRY_LEVEL_DEBUG:
        return QStringLiteral("debug");
    case SENTRY_LEVEL_WARNING:
        return QStringLiteral("warning");
    case SENTRY_LEVEL_ERROR:
        return QStringLiteral("error");
    case SENTRY_LEVEL_FATAL:
        return QStringLiteral("fatal");
    case SENTRY_LEVEL_INFO:
    default:
        return QStringLiteral("info");
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
            nativeBreadcrumb, key.constData(), static_cast<size_t>(key.size()), SentryEvent::fromVariant(it.value()));
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

sentry_value_t fingerprintFromStringList(const QStringList &fingerprint)
{
    sentry_value_t nativeFingerprint = sentry_value_new_list();
    for (const QString &part : fingerprint) {
        const QByteArray utf8 = part.toUtf8();
        sentry_value_append(
            nativeFingerprint, sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size())));
    }
    return nativeFingerprint;
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

SentryNativeSdk *SentryNativeSdk::instance()
{
    static SentryNativeSdk sdk;
    return &sdk;
}

SentryNativeSdk::SentryNativeSdk(QObject *parent)
    : QObject(parent)
    , m_fingerprint(std::make_unique<SentryNativeValue>())
{
}

SentryNativeSdk::~SentryNativeSdk()
{
    if (m_initialized && QCoreApplication::instance() && !QCoreApplication::closingDown()) {
        close();
    }
}

bool SentryNativeSdk::isInitialized() const
{
    return m_initialized;
}

bool SentryNativeSdk::init(Sentry *sentry, SentryOptions *options)
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

    std::unique_ptr<SentryNativeEventHookState> beforeBreadcrumbState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeBreadcrumb(),
                              QStringLiteral("beforeBreadcrumb"),
                              true,
                              &beforeBreadcrumbState)) {
        return false;
    }

    std::unique_ptr<SentryNativeEventHookState> beforeSendLogState;
    if (!createEventHookState(
            sentry, options, options->beforeSendLog(), QStringLiteral("beforeSendLog"), true, &beforeSendLogState)) {
        return false;
    }

    std::unique_ptr<SentryNativeEventHookState> beforeSendMetricState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeSendMetric(),
                              QStringLiteral("beforeSendMetric"),
                              true,
                              &beforeSendMetricState)) {
        return false;
    }

    std::unique_ptr<SentryNativeEventHookState> beforeSendState;
    if (!createEventHookState(
            sentry, options, options->beforeSend(), QStringLiteral("beforeSend"), true, &beforeSendState)) {
        return false;
    }

    std::unique_ptr<SentryNativeEventHookState> onCrashState;
    if (!createEventHookState(sentry, options, options->onCrash(), QStringLiteral("onCrash"), false, &onCrashState)) {
        return false;
    }
    auto crashHookState = std::make_unique<SentryNativeCrashHookState>();
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
        sentry_set_user(SentryEvent::fromVariant(user->toVariantMap()));
    }
    setInitialized(true);
    connectToApplicationShutdown();
    return true;
}

bool SentryNativeSdk::flush(int timeoutMs)
{
    if (!m_initialized) {
        return true;
    }

    return sentry_flush(timeoutMs < 0 ? 0 : static_cast<uint64_t>(timeoutMs)) == 0;
}

bool SentryNativeSdk::close()
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
    m_fingerprint->reset();
    setInitialized(false);
    return true;
}

void SentryNativeSdk::closeBeforeApplicationShutdown()
{
    close();
}

void SentryNativeSdk::connectToApplicationShutdown()
{
    if (m_applicationShutdownConnection || !QCoreApplication::instance()) {
        return;
    }

    m_applicationShutdownConnection = QObject::connect(QCoreApplication::instance(),
                                                       &QCoreApplication::aboutToQuit,
                                                       this,
                                                       &SentryNativeSdk::closeBeforeApplicationShutdown,
                                                       Qt::DirectConnection);
}

void SentryNativeSdk::detachSentry(Sentry *sentry)
{
    auto detach = [sentry](std::unique_ptr<SentryNativeEventHookState> &state)
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

bool SentryNativeSdk::setUser(Sentry *sentry, const QVariantMap &user)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.setUser cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before setting users."));
        }
        return false;
    }

    if (user.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry user must not be empty."));
        }
        return false;
    }

    sentry_set_user(SentryEvent::fromVariant(userFromVariantMap(user)));
    return true;
}

bool SentryNativeSdk::removeUser(Sentry *sentry)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.removeUser cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before removing users."));
        }
        return false;
    }

    sentry_remove_user();
    return true;
}

bool SentryNativeSdk::setTag(Sentry *sentry, const QString &key, const QString &value)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.setTag cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before setting tags."));
        }
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

bool SentryNativeSdk::removeTag(Sentry *sentry, const QString &key)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.removeTag cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before removing tags."));
        }
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

bool SentryNativeSdk::setContext(Sentry *sentry, const QString &key, const QVariantMap &context)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.setContext cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before setting contexts."));
        }
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
                         SentryEvent::fromVariant(context));
    return true;
}

bool SentryNativeSdk::removeContext(Sentry *sentry, const QString &key)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.removeContext cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before removing contexts."));
        }
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

bool SentryNativeSdk::setFingerprint(Sentry *sentry, const QStringList &fingerprint)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.setFingerprint cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before setting fingerprints."));
        }
        return false;
    }

    if (fingerprint.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry fingerprint must not be empty."));
        }
        return false;
    }

    m_fingerprint->reset(fingerprintFromStringList(fingerprint));
    return true;
}

bool SentryNativeSdk::removeFingerprint(Sentry *sentry)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.removeFingerprint cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before removing fingerprints."));
        }
        return false;
    }

    m_fingerprint->reset();
    sentry_remove_fingerprint();
    return true;
}

bool SentryNativeSdk::addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.addBreadcrumb cannot be called from Sentry event hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before adding breadcrumbs."));
        }
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

bool SentryNativeSdk::log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.log cannot be called from Sentry hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before logging."));
        }
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
        = sentry_log(logLevelFromInt(level), body.constData(), SentryEvent::attributesFromVariantMap(attributes));
    if (result == SENTRY_LOG_RETURN_FAILED && sentry) {
        emit sentry->errorOccurred(QStringLiteral("Sentry log could not be queued."));
    }
    return result == SENTRY_LOG_RETURN_SUCCESS;
}

bool SentryNativeSdk::count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.count cannot be called from Sentry hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before recording metrics."));
        }
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
        metricName.constData(), static_cast<int64_t>(value), SentryEvent::attributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

bool SentryNativeSdk::gauge(Sentry *sentry,
                            const QString &name,
                            double value,
                            const QString &unit,
                            const QVariantMap &attributes)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.gauge cannot be called from Sentry hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before recording metrics."));
        }
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
                                                               SentryEvent::attributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

bool SentryNativeSdk::distribution(Sentry *sentry,
                                   const QString &name,
                                   double value,
                                   const QString &unit,
                                   const QVariantMap &attributes)
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.distribution cannot be called from Sentry hooks."));
        }
        return false;
    }

    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before recording metrics."));
        }
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
                                      SentryEvent::attributesFromVariantMap(attributes));
    return checkMetricResult(sentry, result);
}

QString SentryNativeSdk::captureMessage(Sentry *sentry, const QString &message, const QString &level)
{
    if (!m_initialized) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry must be initialized before capturing messages."));
        }
        return {};
    }

    if (message.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry message must not be empty."));
        }
        return {};
    }

    const QByteArray logger = QByteArrayLiteral("qml");
    const QByteArray text = message.toUtf8();
    sentry_value_t event = sentry_value_new_message_event_n(
        static_cast<sentry_level_t>(SentryEvent::levelFromString(level)),
        logger.constData(),
        static_cast<size_t>(logger.size()),
        text.constData(),
        static_cast<size_t>(text.size()));

    return captureEvent(sentry, event, SentryNativeCaptureMode::Manual);
}

QString SentryNativeSdk::captureEvent(Sentry *sentry, sentry_value_t event, SentryNativeCaptureMode mode)
{
    if (hookDepth > 0) {
        sentry_value_decref(event);
        if (mode == SentryNativeCaptureMode::Manual && sentry) {
            emit sentry->errorOccurred(
                QStringLiteral("Sentry.capture* cannot be called from Sentry event hooks."));
        }
        return {};
    }

    if (!m_initialized) {
        sentry_value_decref(event);
        return {};
    }

    if (!m_fingerprint || sentry_value_is_null(m_fingerprint->value)) {
        return SentryEvent::eventIdFromUuid(sentry_capture_event(event));
    }

    sentry_scope_t *scope = sentry_local_scope_new();
    if (!scope) {
        applyFingerprintToEvent(event);
        return SentryEvent::eventIdFromUuid(sentry_capture_event(event));
    }

    sentry_scope_set_fingerprints(scope, m_fingerprint->ref());
    return SentryEvent::eventIdFromUuid(sentry_capture_event_with_scope(event, scope));
}

void SentryNativeSdk::applyFingerprintToEvent(sentry_value_t event) const
{
    if (!m_fingerprint || sentry_value_is_null(m_fingerprint->value)) {
        return;
    }

    sentry_value_set_by_key(event, "fingerprint", m_fingerprint->ref());
}

void SentryNativeSdk::setInitialized(bool initialized)
{
    if (m_initialized == initialized) {
        return;
    }

    m_initialized = initialized;
    emit initializedChanged();
}
