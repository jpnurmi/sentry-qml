#include <SentryQml/private/sentrynativesdk_p.h>

#include <SentryQml/private/sentryevent_p.h>

#include <SentryQml/sentry.h>
#include <SentryQml/sentryoptions.h>

#include <include/sentry.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qpointer.h>
#include <QtCore/qthread.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>

#include <cmath>

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

namespace {

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

sentry_value_t invokeEventHook(sentry_value_t event, SentryNativeEventHookState *state)
{
    if (!state || !state->engine || !state->callback.isCallable()) {
        return event;
    }

    if (state->thread && QThread::currentThread() != state->thread) {
        return event;
    }

    QJSValue scriptEvent = SentryEvent::toScriptValue(state->engine, event);
    if (scriptEvent.isUndefined()) {
        return event;
    }

    QJSValue result = state->callback.call({scriptEvent});
    if (result.isError()) {
        if (state->sentry) {
            emit state->sentry->errorOccurred(
                QStringLiteral("SentryOptions.%1 failed: %2").arg(state->propertyName, result.toString()));
        }
        return event;
    }

    if (result.isUndefined() || (result.isBool() && result.toBool())) {
        return event;
    }

    if (result.isNull() || (result.isBool() && !result.toBool())) {
        sentry_value_decref(event);
        return sentry_value_new_null();
    }

    sentry_value_t replacement = SentryEvent::fromVariant(result.toVariant(QJSValue::RetainJSObjects));
    if (sentry_value_is_null(replacement)) {
        sentry_value_decref(event);
        return replacement;
    }

    sentry_value_decref(event);
    return replacement;
}

sentry_value_t passThroughOnCrash(const sentry_ucontext_t *, sentry_value_t event, void *)
{
    qDebug("### passThroughOnCrash");
    return event;
}

sentry_value_t beforeSendCallback(sentry_value_t event, void *, void *userData)
{
    qDebug("### beforeSendCallback");
    return invokeEventHook(event, static_cast<SentryNativeEventHookState *>(userData));
}

sentry_value_t onCrashCallback(const sentry_ucontext_t *, sentry_value_t event, void *userData)
{
    qDebug("### onCrashCallback");
    return invokeEventHook(event, static_cast<SentryNativeEventHookState *>(userData));
}

} // namespace

SentryNativeSdk *SentryNativeSdk::instance()
{
    static SentryNativeSdk sdk;
    return &sdk;
}

SentryNativeSdk::SentryNativeSdk(QObject *parent)
    : QObject(parent)
{
}

SentryNativeSdk::~SentryNativeSdk()
{
    if (m_initialized) {
        sentry_close();
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

    if (options->shutdownTimeout() < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry shutdownTimeout must not be negative."));
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

    sentry_options_t *nativeOptions = sentry_options_new();
    if (!nativeOptions) {
        emit sentry->errorOccurred(QStringLiteral("Failed to allocate sentry-native options."));
        return false;
    }

    setUtf8Option(options->dsn(), sentry_options_set_dsn_n, nativeOptions);
    setUtf8Option(options->release(), sentry_options_set_release_n, nativeOptions);
    setUtf8Option(options->environment(), sentry_options_set_environment_n, nativeOptions);
    setUtf8Option(options->dist(), sentry_options_set_dist_n, nativeOptions);
    sentry_options_set_debug(nativeOptions, options->debug() ? 1 : 0);
    sentry_options_set_sample_rate(nativeOptions, options->sampleRate());
    sentry_options_set_shutdown_timeout(nativeOptions, static_cast<uint64_t>(options->shutdownTimeout()));
    sentry_options_set_sdk_name(nativeOptions, SENTRY_QML_SDK_NAME);

    if (beforeSendState) {
        sentry_options_set_before_send(nativeOptions, beforeSendCallback, beforeSendState.get());
    }

    if (onCrashState) {
        sentry_options_set_on_crash(nativeOptions, onCrashCallback, onCrashState.get());
    } else if (beforeSendState) {
        sentry_options_set_on_crash(nativeOptions, passThroughOnCrash, nullptr);
    }

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
        beforeSendState.reset();
        onCrashState.reset();
        emit sentry->errorOccurred(QStringLiteral("sentry_init failed with code %1.").arg(result));
        return false;
    }

    m_beforeSendState = std::move(beforeSendState);
    m_onCrashState = std::move(onCrashState);
    setInitialized(true);
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
    m_beforeSendState.reset();
    m_onCrashState.reset();
    setInitialized(false);
    return true;
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
    detach(m_onCrashState);
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

    return captureEvent(event);
}

QString SentryNativeSdk::captureEvent(sentry_value_t event)
{
    if (!m_initialized) {
        sentry_value_decref(event);
        return {};
    }

    return SentryEvent::eventIdFromUuid(sentry_capture_event(event));
}

void SentryNativeSdk::setInitialized(bool initialized)
{
    if (m_initialized == initialized) {
        return;
    }

    m_initialized = initialized;
    emit initializedChanged();
}
