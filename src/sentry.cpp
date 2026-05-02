#include <SentryQml/sentry.h>

#include <SentryQml/private/sentryqmlengine_p.h>

#include <SentryQml/sentryoptions.h>

#include "sentrysdk_p.h"

#include <cmath>

Sentry::Sentry(QObject *parent)
    : QObject(parent)
{
    QObject::connect(SentrySdk::instance(), &SentrySdk::initializedChanged, this, &Sentry::initializedChanged);
}

Sentry::~Sentry()
{
    SentrySdk::instance()->detachSentry(this);
}

Sentry *Sentry::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(scriptEngine)

    auto *sentry = new Sentry(engine);
    sentry->m_qmlEngine = std::make_unique<SentryQmlEngine>(engine, sentry);
    return sentry;
}

bool Sentry::isInitialized() const
{
    return SentrySdk::instance()->isInitialized();
}

bool Sentry::init(SentryOptions *options)
{
    ensureQmlEngine(qmlEngine(options));

    const bool ok = SentrySdk::instance()->init(this, options);
    if (ok && m_qmlEngine) {
        m_qmlEngine->installWarningHandler();
    }
    return ok;
}

bool Sentry::flush(int timeoutMs)
{
    return SentrySdk::instance()->flush(timeoutMs);
}

bool Sentry::close()
{
    if (m_qmlEngine) {
        m_qmlEngine->uninstallWarningHandler();
    }
    return SentrySdk::instance()->close();
}

bool Sentry::setRelease(const QString &release)
{
    return SentrySdk::instance()->setRelease(this, release);
}

bool Sentry::setEnvironment(const QString &environment)
{
    return SentrySdk::instance()->setEnvironment(this, environment);
}

bool Sentry::setUser(const QVariantMap &user)
{
    return SentrySdk::instance()->setUser(this, user);
}

bool Sentry::removeUser()
{
    return SentrySdk::instance()->removeUser(this);
}

bool Sentry::setTag(const QString &key, const QString &value)
{
    return SentrySdk::instance()->setTag(this, key, value);
}

bool Sentry::removeTag(const QString &key)
{
    return SentrySdk::instance()->removeTag(this, key);
}

bool Sentry::setContext(const QString &key, const QVariantMap &context)
{
    return SentrySdk::instance()->setContext(this, key, context);
}

bool Sentry::removeContext(const QString &key)
{
    return SentrySdk::instance()->removeContext(this, key);
}

bool Sentry::setAttribute(const QString &key, const QVariant &value)
{
    return SentrySdk::instance()->setAttribute(this, key, value);
}

bool Sentry::removeAttribute(const QString &key)
{
    return SentrySdk::instance()->removeAttribute(this, key);
}

bool Sentry::setFingerprint(const QStringList &fingerprint)
{
    return SentrySdk::instance()->setFingerprint(this, fingerprint);
}

bool Sentry::removeFingerprint()
{
    return SentrySdk::instance()->removeFingerprint(this);
}

SentryAttachment *Sentry::attachFile(const QString &path, const QString &contentType)
{
    return SentrySdk::instance()->attachFile(this, path, contentType);
}

SentryAttachment *Sentry::attachBytes(const QByteArray &bytes, const QString &filename, const QString &contentType)
{
    return SentrySdk::instance()->attachBytes(this, bytes, filename, contentType);
}

bool Sentry::removeAttachment(SentryAttachment *attachment)
{
    return SentrySdk::instance()->removeAttachment(this, attachment);
}

bool Sentry::clearAttachments()
{
    return SentrySdk::instance()->clearAttachments(this);
}

bool Sentry::startSession()
{
    return SentrySdk::instance()->startSession(this);
}

bool Sentry::endSession()
{
    return SentrySdk::instance()->endSession(this, -1);
}

bool Sentry::endSession(SessionStatus status)
{
    return SentrySdk::instance()->endSession(this, static_cast<int>(status));
}

bool Sentry::addBreadcrumb(const QVariantMap &breadcrumb)
{
    return SentrySdk::instance()->addBreadcrumb(this, breadcrumb);
}

bool Sentry::addBreadcrumb(const QString &message,
                           const QString &category,
                           const QString &type,
                           const QString &level,
                           const QVariantMap &data)
{
    QVariantMap breadcrumb;
    breadcrumb.insert(QStringLiteral("message"), message);
    breadcrumb.insert(QStringLiteral("category"), category);
    breadcrumb.insert(QStringLiteral("type"), type);
    breadcrumb.insert(QStringLiteral("level"), level);
    if (!data.isEmpty()) {
        breadcrumb.insert(QStringLiteral("data"), data);
    }
    return addBreadcrumb(breadcrumb);
}

bool Sentry::log(Level level, const QString &message, const QVariantMap &attributes)
{
    return SentrySdk::instance()->log(this, static_cast<int>(level), message, attributes);
}

bool Sentry::trace(const QString &message, const QVariantMap &attributes)
{
    return log(Trace, message, attributes);
}

bool Sentry::debug(const QString &message, const QVariantMap &attributes)
{
    return log(Debug, message, attributes);
}

bool Sentry::info(const QString &message, const QVariantMap &attributes)
{
    return log(Info, message, attributes);
}

bool Sentry::warn(const QString &message, const QVariantMap &attributes)
{
    return log(Warning, message, attributes);
}

bool Sentry::error(const QString &message, const QVariantMap &attributes)
{
    return log(Error, message, attributes);
}

bool Sentry::fatal(const QString &message, const QVariantMap &attributes)
{
    return log(Fatal, message, attributes);
}

bool Sentry::metric(MetricType type,
                    const QString &name,
                    double value,
                    const QString &unit,
                    const QVariantMap &attributes)
{
    if (!std::isfinite(value)) {
        emit errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        return false;
    }

    switch (type) {
    case Count:
        return count(name, static_cast<qint64>(value), attributes);
    case Gauge:
        return gauge(name, value, unit, attributes);
    case Distribution:
        return distribution(name, value, unit, attributes);
    }

    emit errorOccurred(QStringLiteral("Unsupported Sentry metric type."));
    return false;
}

bool Sentry::count(const QString &name, qint64 value, const QVariantMap &attributes)
{
    return SentrySdk::instance()->count(this, name, value, attributes);
}

bool Sentry::gauge(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    return SentrySdk::instance()->gauge(this, name, value, unit, attributes);
}

bool Sentry::distribution(const QString &name, double value, const QString &unit, const QVariantMap &attributes)
{
    return SentrySdk::instance()->distribution(this, name, value, unit, attributes);
}

QString Sentry::captureMessage(const QString &message, const QString &level)
{
    return SentrySdk::instance()->captureMessage(this, message, level);
}

QString Sentry::captureException(const QJSValue &exception)
{
    if (!isInitialized()) {
        emit errorOccurred(QStringLiteral("Sentry must be initialized before capturing exceptions."));
        return {};
    }

    if (!m_qmlEngine) {
        return {};
    }

    return m_qmlEngine->captureException(exception);
}

bool Sentry::captureFeedback(const QVariantMap &feedback, SentryHint *hint)
{
    return SentrySdk::instance()->captureFeedback(this, feedback, hint);
}

void Sentry::ensureQmlEngine(QQmlEngine *engine)
{
    if (!engine || m_qmlEngine) {
        return;
    }

    m_qmlEngine = std::make_unique<SentryQmlEngine>(engine, this);
}
