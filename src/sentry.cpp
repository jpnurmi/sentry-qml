#include <SentryQml/sentry.h>

#include <SentryQml/private/sentrynativesdk_p.h>
#include <SentryQml/private/sentryqmlengine_p.h>

#include <SentryQml/sentryoptions.h>

Sentry::Sentry(QObject *parent)
    : QObject(parent)
{
    QObject::connect(SentryNativeSdk::instance(), &SentryNativeSdk::initializedChanged, this, &Sentry::initializedChanged);
}

Sentry::~Sentry()
{
    SentryNativeSdk::instance()->detachSentry(this);
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
    return SentryNativeSdk::instance()->isInitialized();
}

bool Sentry::init(SentryOptions *options)
{
    ensureQmlEngine(qmlEngine(options));

    const bool ok = SentryNativeSdk::instance()->init(this, options);
    if (ok && m_qmlEngine) {
        m_qmlEngine->installWarningHandler();
    }
    return ok;
}

bool Sentry::flush(int timeoutMs)
{
    return SentryNativeSdk::instance()->flush(timeoutMs);
}

bool Sentry::close()
{
    if (m_qmlEngine) {
        m_qmlEngine->uninstallWarningHandler();
    }
    return SentryNativeSdk::instance()->close();
}

bool Sentry::addBreadcrumb(const QVariantMap &breadcrumb)
{
    return SentryNativeSdk::instance()->addBreadcrumb(this, breadcrumb);
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
    return SentryNativeSdk::instance()->log(this, static_cast<int>(level), message, attributes);
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

QString Sentry::captureMessage(const QString &message, const QString &level)
{
    return SentryNativeSdk::instance()->captureMessage(this, message, level);
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

void Sentry::ensureQmlEngine(QQmlEngine *engine)
{
    if (!engine || m_qmlEngine) {
        return;
    }

    m_qmlEngine = std::make_unique<SentryQmlEngine>(engine, this);
}
