#include <SentryQml/private/sentrysdk_p.h>

#include "sentryobjcbridge_p.h"

#include <SentryQml/private/sentryevent_p.h>
#include <SentryQml/private/sentryhint_p.h>

#include <SentryQml/sentry.h>
#include <SentryQml/sentryattachment.h>
#include <SentryQml/sentryhint.h>
#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryuser.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qpointer.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qthread.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>

#include <cmath>

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
};

struct SentrySdkAttachmentState
{
    SentryObjCBridge::Attachment attachment;
};

namespace {

thread_local int hookDepth = 0;

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

SentryObjCBridge::HookResult invokeValueHook(const QVariant &value, SentrySdkEventHookState *state)
{
    if (!state || !state->engine || !state->callback.isCallable()) {
        return {};
    }

    if (state->thread && QThread::currentThread() != state->thread) {
        return {};
    }

    const QScopedValueRollback<int> rollback(hookDepth, hookDepth + 1);
    QJSValue scriptValue = SentryEvent::toScriptValue(state->engine, value);
    if (scriptValue.isUndefined()) {
        return {};
    }

    QJSValue result = state->callback.call({scriptValue});
    if (result.isError()) {
        if (state->sentry) {
            emit state->sentry->errorOccurred(
                QStringLiteral("SentryOptions.%1 failed: %2").arg(state->propertyName, result.toString()));
        }
        return {};
    }

    if (result.isUndefined() || (result.isBool() && result.toBool())) {
        return {};
    }

    if (result.isNull() || (result.isBool() && !result.toBool())) {
        return {SentryObjCBridge::HookResult::Drop, {}};
    }

    return {SentryObjCBridge::HookResult::Replace, result.toVariant()};
}

SentryObjCBridge::Hook hookFromState(SentrySdkEventHookState *state)
{
    if (!state) {
        return {};
    }
    return [state](const QVariant &value) { return invokeValueHook(value, state); };
}

QString levelNameFromString(const QString &level)
{
    return SentryEvent::levelNameFromString(level);
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

SentryObjCBridge::Attachment hintAttachment(const SentryHintAttachment &attachment)
{
    SentryObjCBridge::Attachment native;
    native.type = attachment.type == SentryHintAttachmentType::File ? SentryObjCBridge::Attachment::File
                                                                    : SentryObjCBridge::Attachment::Bytes;
    native.path = attachment.path;
    native.bytes = attachment.bytes;
    native.filename = attachment.filename;
    native.contentType = attachment.contentType;
    return native;
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

bool isSupportedMetricAttribute(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        if (map.contains(QStringLiteral("value"))) {
            return isSupportedMetricAttribute(map.value(QStringLiteral("value")));
        }
        return true;
    }

    switch (value.metaType().id()) {
    case QMetaType::Bool:
    case QMetaType::Float:
    case QMetaType::Double:
    case QMetaType::QString:
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        return true;
    default:
        return isSupportedInteger(value);
    }
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

    m_beforeBreadcrumbState = std::move(beforeBreadcrumbState);
    m_beforeSendLogState = std::move(beforeSendLogState);
    m_beforeSendMetricState = std::move(beforeSendMetricState);
    m_beforeSendState = std::move(beforeSendState);
    m_onCrashState = std::move(onCrashState);
    m_crashHookState = std::make_unique<SentrySdkCrashHookState>();
    m_applyHooksLocally = options->dsn().isEmpty();

    SentryObjCBridge::Options nativeOptions;
    nativeOptions.dsn = options->dsn();
    nativeOptions.databasePath = options->databasePath();
    nativeOptions.release = options->release();
    nativeOptions.environment = options->environment();
    nativeOptions.dist = options->dist();
    if (options->user()) {
        nativeOptions.user = options->user()->toVariantMap();
    }
    nativeOptions.debug = options->debug();
    nativeOptions.enableLogs = options->enableLogs();
    nativeOptions.enableMetrics = options->enableMetrics();
    nativeOptions.autoSessionTracking = options->autoSessionTracking();
    nativeOptions.sampleRate = options->sampleRate();
    nativeOptions.maxBreadcrumbs = options->maxBreadcrumbs();
    nativeOptions.shutdownTimeout = options->shutdownTimeout();
    nativeOptions.beforeBreadcrumb = m_applyHooksLocally ? SentryObjCBridge::Hook {}
                                                        : hookFromState(m_beforeBreadcrumbState.get());
    nativeOptions.beforeSendLog = hookFromState(m_beforeSendLogState.get());
    nativeOptions.beforeSendMetric = hookFromState(m_beforeSendMetricState.get());
    nativeOptions.beforeSend = m_applyHooksLocally ? SentryObjCBridge::Hook {} : hookFromState(m_beforeSendState.get());
    nativeOptions.onCrash = hookFromState(m_onCrashState.get());
    m_release = nativeOptions.release;
    m_environment = nativeOptions.environment;
    m_dist = nativeOptions.dist;
    m_user = nativeOptions.user;
    m_tags.clear();
    m_contexts.clear();
    m_breadcrumbs.clear();
    m_maxBreadcrumbs = nativeOptions.maxBreadcrumbs;

    if (!SentryObjCBridge::start(nativeOptions)) {
        m_applyHooksLocally = false;
        clearLocalScope();
        m_beforeBreadcrumbState.reset();
        m_beforeSendLogState.reset();
        m_beforeSendMetricState.reset();
        m_beforeSendState.reset();
        m_onCrashState.reset();
        m_crashHookState.reset();
        emit sentry->errorOccurred(QStringLiteral("SentryObjC could not be initialized."));
        return false;
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

    SentryObjCBridge::flush(timeoutMs);
    return true;
}

bool SentrySdk::close()
{
    if (!m_initialized) {
        return true;
    }

    SentryObjCBridge::close();

    if (m_applicationShutdownConnection) {
        QObject::disconnect(m_applicationShutdownConnection);
        m_applicationShutdownConnection = {};
    }

    m_beforeBreadcrumbState.reset();
    m_beforeSendLogState.reset();
    m_beforeSendMetricState.reset();
    m_beforeSendState.reset();
    m_onCrashState.reset();
    m_crashHookState.reset();
    m_applyHooksLocally = false;
    clearLocalScope();
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

    SentryObjCBridge::setRelease(release);
    m_release = release;
    return true;
}

bool SentrySdk::setEnvironment(Sentry *sentry, const QString &environment)
{
    if (!ensureCanCall(sentry, "setEnvironment", "setting environments")) {
        return false;
    }

    SentryObjCBridge::setEnvironment(environment);
    m_environment = environment;
    return true;
}

bool SentrySdk::setUser(Sentry *sentry, const QVariantMap &user)
{
    if (!ensureCanCall(sentry, "setUser", "setting users")) {
        return false;
    }

    m_user = userFromVariantMap(user);
    SentryObjCBridge::setUser(m_user);
    return true;
}

bool SentrySdk::removeUser(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeUser", "removing users")) {
        return false;
    }

    SentryObjCBridge::removeUser();
    m_user.clear();
    return true;
}

bool SentrySdk::setTag(Sentry *sentry, const QString &key, const QString &value)
{
    if (!ensureCanCall(sentry, "setTag", "setting tags")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        return false;
    }

    SentryObjCBridge::setTag(key, value);
    m_tags.insert(key, value);
    return true;
}

bool SentrySdk::removeTag(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeTag", "removing tags")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        return false;
    }

    SentryObjCBridge::removeTag(key);
    m_tags.remove(key);
    return true;
}

bool SentrySdk::setContext(Sentry *sentry, const QString &key, const QVariantMap &context)
{
    if (!ensureCanCall(sentry, "setContext", "setting contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        return false;
    }

    SentryObjCBridge::setContext(key, context);
    m_contexts.insert(key, context);
    return true;
}

bool SentrySdk::removeContext(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeContext", "removing contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        return false;
    }

    SentryObjCBridge::removeContext(key);
    m_contexts.remove(key);
    return true;
}

bool SentrySdk::setAttribute(Sentry *sentry, const QString &key, const QVariant &value)
{
    if (!ensureCanCall(sentry, "setAttribute", "setting attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        return false;
    }

    if (!isSupportedMetricAttribute(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute value must be supported."));
        return false;
    }

    SentryObjCBridge::setAttribute(key, value);
    return true;
}

bool SentrySdk::removeAttribute(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeAttribute", "removing attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        return false;
    }

    SentryObjCBridge::removeAttribute(key);
    return true;
}

bool SentrySdk::setFingerprint(Sentry *sentry, const QStringList &fingerprint)
{
    if (!ensureCanCall(sentry, "setFingerprint", "setting fingerprints")) {
        return false;
    }

    m_fingerprint = fingerprint;
    SentryObjCBridge::setFingerprint(fingerprint);
    return true;
}

bool SentrySdk::removeFingerprint(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeFingerprint", "removing fingerprints")) {
        return false;
    }

    m_fingerprint.clear();
    SentryObjCBridge::clearFingerprint();
    return true;
}

SentryAttachment *SentrySdk::attachFile(Sentry *sentry, const QString &path, const QString &contentType)
{
    if (!ensureCanCall(sentry, "attachFile", "attaching files")) {
        return nullptr;
    }

    if (path.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attachment path must not be empty."));
        return nullptr;
    }

    auto *state = new SentrySdkAttachmentState;
    state->attachment.type = SentryObjCBridge::Attachment::File;
    state->attachment.path = path;
    auto *wrapper = new SentryAttachment(state, sentry);
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
        emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment must not be empty."));
        return nullptr;
    }

    if (filename.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment filename must not be empty."));
        return nullptr;
    }

    auto *state = new SentrySdkAttachmentState;
    state->attachment.type = SentryObjCBridge::Attachment::Bytes;
    state->attachment.bytes = bytes;
    state->attachment.filename = filename;
    auto *wrapper = new SentryAttachment(state, sentry);
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

    invalidateAttachments();
    SentryObjCBridge::clearAttachments();
    return true;
}

void SentrySdk::trackAttachment(SentryAttachment *attachment)
{
    if (!attachment) {
        return;
    }

    m_attachments.append(attachment);
    updateAttachments();
}

void SentrySdk::detachAttachment(SentryAttachment *attachment)
{
    if (!attachment) {
        return;
    }

    const bool wasTracked = m_attachments.removeAll(attachment) > 0;
    delete static_cast<SentrySdkAttachmentState *>(attachment->handle());
    attachment->invalidate();
    if (wasTracked && m_initialized) {
        updateAttachments();
    }
}

void SentrySdk::invalidateAttachments()
{
    const QList<SentryAttachment *> attachments = m_attachments;
    m_attachments.clear();
    for (SentryAttachment *attachment : attachments) {
        if (!attachment) {
            continue;
        }
        delete static_cast<SentrySdkAttachmentState *>(attachment->handle());
        attachment->invalidate();
    }
}

void SentrySdk::updateAttachments()
{
    QList<SentryObjCBridge::Attachment> attachments;
    for (SentryAttachment *attachment : m_attachments) {
        auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr;
        if (state) {
            attachments.append(state->attachment);
        }
    }
    SentryObjCBridge::setAttachments(attachments);
}

void SentrySdk::setAttachmentFilename(SentryAttachment *attachment, const QString &filename)
{
    if (auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr) {
        state->attachment.filename = filename;
        if (m_attachments.contains(attachment)) {
            updateAttachments();
        }
    }
}

void SentrySdk::setAttachmentContentType(SentryAttachment *attachment, const QString &contentType)
{
    if (auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr) {
        state->attachment.contentType = contentType;
        if (m_attachments.contains(attachment)) {
            updateAttachments();
        }
    }
}

bool SentrySdk::removeAttachment(Sentry *sentry, SentryAttachment *attachment)
{
    if (!ensureCanCall(sentry, "removeAttachment", "removing attachments")) {
        return false;
    }

    if (!attachment || !attachment->handle()) {
        return true;
    }

    detachAttachment(attachment);
    return true;
}

bool SentrySdk::startSession(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "startSession", "starting sessions")) {
        return false;
    }

    SentryObjCBridge::startSession();
    return true;
}

bool SentrySdk::endSession(Sentry *sentry, int)
{
    if (!ensureCanCall(sentry, "endSession", "ending sessions")) {
        return false;
    }

    SentryObjCBridge::endSession();
    return true;
}

bool SentrySdk::addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb)
{
    if (!ensureCanCall(sentry, "addBreadcrumb", "adding breadcrumbs")) {
        return false;
    }

    QVariantMap nativeBreadcrumb = breadcrumb;
    if (m_applyHooksLocally) {
        const SentryObjCBridge::HookResult result = invokeValueHook(nativeBreadcrumb, m_beforeBreadcrumbState.get());
        if (result.action == SentryObjCBridge::HookResult::Drop) {
            return true;
        }
        if (result.action == SentryObjCBridge::HookResult::Replace) {
            nativeBreadcrumb = result.value.toMap();
        }

        if (m_maxBreadcrumbs > 0) {
            m_breadcrumbs.append(nativeBreadcrumb);
            while (m_breadcrumbs.size() > m_maxBreadcrumbs) {
                m_breadcrumbs.removeFirst();
            }
        }
    }

    SentryObjCBridge::addBreadcrumb(nativeBreadcrumb);
    return true;
}

bool SentrySdk::log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "log", "logging", "hooks")) {
        return false;
    }

    if (message.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry log message must not be empty."));
        return false;
    }

    SentryObjCBridge::log(level, message, attributes);
    return true;
}

bool SentrySdk::count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "count", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (value < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric count must not be negative."));
        return false;
    }

    SentryObjCBridge::count(name, static_cast<quint64>(value), attributes);
    return true;
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
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (!std::isfinite(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        return false;
    }

    SentryObjCBridge::gauge(name, value, unit, attributes);
    return true;
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
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (!std::isfinite(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        return false;
    }

    SentryObjCBridge::distribution(name, value, unit, attributes);
    return true;
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
        {QStringLiteral("level"), levelNameFromString(level)},
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
        emit sentry->errorOccurred(QStringLiteral("Sentry feedback message must not be empty."));
        return false;
    }

    QList<SentryObjCBridge::Attachment> attachments;
    if (hint) {
        for (const SentryHintAttachment &attachment : hint->d->attachments) {
            attachments.append(hintAttachment(attachment));
        }
    }

    SentryObjCBridge::captureFeedback(feedback, attachments);
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

    QVariantMap nativeEvent = event;
    if (m_applyHooksLocally) {
        applyLocalScopeToEvent(&nativeEvent);
        applyFingerprintToEvent(&nativeEvent);
        const SentryObjCBridge::HookResult result = invokeValueHook(nativeEvent, m_beforeSendState.get());
        if (result.action == SentryObjCBridge::HookResult::Drop) {
            return {};
        }
        if (result.action == SentryObjCBridge::HookResult::Replace) {
            nativeEvent = result.value.toMap();
        }
        return SentryObjCBridge::captureEvent(nativeEvent, {});
    }

    return SentryObjCBridge::captureEvent(nativeEvent, m_fingerprint);
}

void SentrySdk::clearLocalScope()
{
    m_release.clear();
    m_environment.clear();
    m_dist.clear();
    m_user.clear();
    m_tags.clear();
    m_contexts.clear();
    m_breadcrumbs.clear();
    m_maxBreadcrumbs = 100;
}

void SentrySdk::applyLocalScopeToEvent(QVariantMap *event) const
{
    if (!event) {
        return;
    }

    if (!m_release.isEmpty() && !event->contains(QStringLiteral("release"))) {
        event->insert(QStringLiteral("release"), m_release);
    }
    if (!m_environment.isEmpty() && !event->contains(QStringLiteral("environment"))) {
        event->insert(QStringLiteral("environment"), m_environment);
    }
    if (!m_dist.isEmpty() && !event->contains(QStringLiteral("dist"))) {
        event->insert(QStringLiteral("dist"), m_dist);
    }
    if (!m_user.isEmpty() && !event->contains(QStringLiteral("user"))) {
        event->insert(QStringLiteral("user"), m_user);
    }
    if (!m_tags.isEmpty()) {
        QVariantMap tags = m_tags;
        const QVariantMap eventTags = event->value(QStringLiteral("tags")).toMap();
        for (auto it = eventTags.cbegin(); it != eventTags.cend(); ++it) {
            tags.insert(it.key(), it.value());
        }
        event->insert(QStringLiteral("tags"), tags);
    }
    if (!m_contexts.isEmpty()) {
        QVariantMap contexts = m_contexts;
        const QVariantMap eventContexts = event->value(QStringLiteral("contexts")).toMap();
        for (auto it = eventContexts.cbegin(); it != eventContexts.cend(); ++it) {
            contexts.insert(it.key(), it.value());
        }
        event->insert(QStringLiteral("contexts"), contexts);
    }
    if (!m_breadcrumbs.isEmpty() && !event->contains(QStringLiteral("breadcrumbs"))) {
        event->insert(QStringLiteral("breadcrumbs"), m_breadcrumbs);
    }
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
