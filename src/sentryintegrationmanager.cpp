#include <SentryQml/private/sentryintegrationmanager_p.h>

#include <SentryQml/private/sentrysdk_p.h>
#include <SentryQml/sentry.h>
#include <SentryQml/sentryintegration.h>
#include <SentryQml/sentryintegrationcontext.h>
#include <SentryQml/sentryintegrationplugin.h>
#include <SentryQml/sentryoptions.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdeadlinetimer.h>
#include <QtCore/qdebug.h>
#include <QtCore/qdir.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qhash.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qlibrary.h>
#include <QtCore/qpluginloader.h>
#include <QtCore/qpointer.h>
#include <QtCore/qregularexpression.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qset.h>
#include <QtCore/qthread.h>
#include <QtCore/qurl.h>
#include <QtCore/qversionnumber.h>

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

#ifndef SENTRY_QML_VERSION
#define SENTRY_QML_VERSION "0.0.0"
#endif

namespace {

QString currentPlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_IOS)
    return QStringLiteral("ios");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WASM)
    return QStringLiteral("wasm");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("unknown");
#endif
}

QString canonicalOrCleanPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

QString deploymentDirectory()
{
    QString path = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MACOS)
    path = QDir(path).filePath(QStringLiteral("../PlugIns/sentry-integrations"));
#else
    path = QDir(path).filePath(QStringLiteral("sentry-integrations"));
#endif
    return canonicalOrCleanPath(path);
}

QString pluginId(const QJsonObject &root)
{
    return root.value(QStringLiteral("MetaData")).toObject().value(QStringLiteral("Id")).toString();
}

QStringList stringArray(const QJsonValue &value, bool *valid)
{
    QStringList result;
    if (!value.isArray()) {
        *valid = false;
        return result;
    }

    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()) {
            *valid = false;
            return {};
        }
        result.append(entry.toString());
    }
    *valid = true;
    return result;
}

QString exceptionError(const std::exception &exception)
{
    return QStringLiteral("threw an exception: %1").arg(QString::fromLocal8Bit(exception.what()));
}

bool isVersionedServiceId(const QString &iid)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*/[1-9][0-9]*$"));
    return pattern.match(iid).hasMatch();
}

} // namespace

struct SentryIntegrationContextPrivate
{
    SentrySdk *sdk = nullptr;
    SentryIntegrationManager *manager = nullptr;
    QPointer<Sentry> sentry;
    QString sdkVersion = QStringLiteral(SENTRY_QML_VERSION);
    QString platform;
    QString backend;
    QString databasePath;
    SentryIntegrationContext::State state = SentryIntegrationContext::Inactive;
};

struct SentryIntegrationDescriptorSnapshot
{
    QString id;
    QString path;
    QVariantMap configuration;
    bool required = false;
};

struct SentryIntegrationCandidate
{
    QJsonObject metadata;
    QString origin;
    QString path;
    QtPluginInstanceFunction staticInstance = nullptr;
    QString metadataError;

    bool isStatic() const
    {
        return staticInstance != nullptr;
    }
};

struct SentryLoadedIntegration
{
    QString id;
    QString origin;
    QObject *instance = nullptr;
    SentryIntegrationPlugin *plugin = nullptr;
};

struct SentryIntegrationCycleEntry
{
    enum State
    {
        Prepared,
        Running
    };

    SentryLoadedIntegration *loaded = nullptr;
    bool required = false;
    State state = Prepared;
};

class SentryIntegrationManagerPrivate
{
public:
    explicit SentryIntegrationManagerPrivate(SentryIntegrationManager *q, SentrySdk *sdk)
        : q(q)
        , sdk(sdk)
    {
        auto contextPrivate = std::make_unique<SentryIntegrationContextPrivate>();
        contextPrivate->sdk = sdk;
        contextPrivate->manager = q;
        context =
            std::unique_ptr<SentryIntegrationContext>(new SentryIntegrationContext(std::move(contextPrivate), sdk));
    }

    void setContextState(SentryIntegrationContext::State state)
    {
        if (context->d->state == state) {
            return;
        }
        context->d->state = state;
        emit context->stateChanged();
    }

    void diagnostic(const QString &id, bool required, const QString &message) const
    {
        const QString text = QStringLiteral("Sentry integration '%1': %2").arg(id, message);
        if (required) {
            context->reportError(text);
        } else {
            context->reportWarning(text);
        }
    }

    bool snapshot(SentryOptions *options, QList<SentryIntegrationDescriptorSnapshot> *descriptors,
                  QStringList *searchRoots)
    {
        QSet<QString> ids;
        for (SentryIntegration *integration : options->integrationList()) {
            if (!integration || !integration->enabled()) {
                continue;
            }

            SentryIntegrationDescriptorSnapshot descriptor;
            descriptor.id = integration->id();
            descriptor.path = integration->path();
            descriptor.configuration = integration->configuration();
            descriptor.required = integration->required();
            if (ids.contains(descriptor.id)) {
                diagnostic(descriptor.id, true, QStringLiteral("duplicate enabled descriptors are not allowed."));
                return false;
            }
            ids.insert(descriptor.id);
            descriptors->append(std::move(descriptor));
        }

        QSet<QString> uniqueRoots;
        for (const QString &configuredPath : options->integrationPaths()) {
            QString path = configuredPath;
            const QFileInfo directInfo(path);
            if (!directInfo.isAbsolute()) {
                const QUrl url(path);
                if (!url.isLocalFile()) {
                    context->reportError(QStringLiteral("SentryOptions.integrationPaths entries must be "
                                                        "absolute local paths."));
                    return false;
                }
                path = url.toLocalFile();
            }
            if (!QFileInfo(path).isAbsolute()) {
                context->reportError(QStringLiteral("SentryOptions.integrationPaths entries must be "
                                                    "absolute local paths."));
                return false;
            }

            path = canonicalOrCleanPath(path);
            if (!uniqueRoots.contains(path)) {
                uniqueRoots.insert(path);
                searchRoots->append(path);
            }
        }

        const QString deployed = deploymentDirectory();
        if (!uniqueRoots.contains(deployed)) {
            searchRoots->append(deployed);
        }
        return true;
    }

    QList<SentryIntegrationCandidate> candidates(const SentryIntegrationDescriptorSnapshot &descriptor,
                                                 const QStringList &searchRoots) const
    {
        QList<SentryIntegrationCandidate> result;
        if (!descriptor.path.isEmpty()) {
            if (!QFileInfo(descriptor.path).isAbsolute()) {
                return result;
            }
            const QString path = canonicalOrCleanPath(descriptor.path);
            QPluginLoader loader(path);
            result.append({loader.metaData(), path, path, nullptr, loader.errorString()});
            return result;
        }

        for (const QStaticPlugin &plugin : QPluginLoader::staticPlugins()) {
            const QJsonObject metadata = plugin.metaData();
            if (pluginId(metadata) != descriptor.id) {
                continue;
            }
            const QString className = metadata.value(QStringLiteral("className")).toString();
            result.append({metadata, QStringLiteral("static:%1").arg(className), QString(), plugin.instance});
        }

        QSet<QString> inspected;
        for (const QString &root : searchRoots) {
            const QDir directory(root);
            const QFileInfoList entries =
                directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &entry : entries) {
                if (!entry.isFile() || !QLibrary::isLibrary(entry.fileName())) {
                    continue;
                }
                const QString path = canonicalOrCleanPath(entry.absoluteFilePath());
                if (inspected.contains(path)) {
                    continue;
                }
                inspected.insert(path);

                QPluginLoader loader(path);
                const QJsonObject metadata = loader.metaData();
                if (pluginId(metadata) == descriptor.id) {
                    result.append({metadata, path, path, nullptr});
                }
            }
        }
        return result;
    }

    bool validate(const SentryIntegrationDescriptorSnapshot &descriptor, const SentryIntegrationCandidate &candidate,
                  QString *error) const
    {
        if (candidate.metadata.isEmpty()) {
            if (candidate.path.isEmpty()) {
                *error = QStringLiteral("plugin metadata is missing.");
            } else {
                *error = QStringLiteral("plugin metadata could not be read from %1: %2")
                             .arg(candidate.path, candidate.metadataError);
            }
            return false;
        }
        if (candidate.metadata.value(QStringLiteral("IID")).toString() != QStringLiteral(SENTRY_QML_INTEGRATION_IID)) {
            *error = QStringLiteral("plugin IID is not %1.").arg(QStringLiteral(SENTRY_QML_INTEGRATION_IID));
            return false;
        }

        const QJsonValue metadataValue = candidate.metadata.value(QStringLiteral("MetaData"));
        if (!metadataValue.isObject()) {
            *error = QStringLiteral("plugin metadata object is missing.");
            return false;
        }
        const QJsonObject metadata = metadataValue.toObject();
        const QStringList requiredKeys = {
            QStringLiteral("Id"),
            QStringLiteral("Name"),
            QStringLiteral("Version"),
            QStringLiteral("IntegrationApi"),
            QStringLiteral("MinimumSentryQmlVersion"),
            QStringLiteral("Platforms"),
            QStringLiteral("Backends"),
            QStringLiteral("RequiredServices"),
        };
        for (const QString &key : requiredKeys) {
            if (!metadata.contains(key)) {
                *error = QStringLiteral("required metadata key '%1' is missing.").arg(key);
                return false;
            }
        }

        static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9.-]*$"));
        const QString id = metadata.value(QStringLiteral("Id")).toString();
        if (!idPattern.match(id).hasMatch()) {
            *error = QStringLiteral("metadata ID '%1' is invalid.").arg(id);
            return false;
        }
        if (id != descriptor.id) {
            *error = QStringLiteral("metadata ID '%1' does not match the requested ID.").arg(id);
            return false;
        }
        if (metadata.value(QStringLiteral("Name")).toString().isEmpty() ||
            metadata.value(QStringLiteral("Version")).toString().isEmpty()) {
            *error = QStringLiteral("plugin name and version must not be empty.");
            return false;
        }
        if (!metadata.value(QStringLiteral("IntegrationApi")).isDouble() ||
            metadata.value(QStringLiteral("IntegrationApi")).toInt() != SENTRY_QML_INTEGRATION_API) {
            *error = QStringLiteral("integration API version is not supported.");
            return false;
        }

        const QString minimumText = metadata.value(QStringLiteral("MinimumSentryQmlVersion")).toString();
        const QVersionNumber minimum = QVersionNumber::fromString(minimumText);
        const QVersionNumber current = QVersionNumber::fromString(QStringLiteral(SENTRY_QML_VERSION));
        if (minimum.isNull() || current.isNull()) {
            *error = QStringLiteral("Sentry QML version metadata is invalid.");
            return false;
        }
        if (QVersionNumber::compare(current, minimum) < 0) {
            *error = QStringLiteral("requires Sentry QML %1 or newer.").arg(minimumText);
            return false;
        }

        bool validArray = false;
        const QStringList platforms = stringArray(metadata.value(QStringLiteral("Platforms")), &validArray);
        if (!validArray || !platforms.contains(context->platform())) {
            *error = QStringLiteral("does not support platform '%1'.").arg(context->platform());
            return false;
        }
        const QStringList backends = stringArray(metadata.value(QStringLiteral("Backends")), &validArray);
        if (!validArray || !backends.contains(context->backend())) {
            *error = QStringLiteral("does not support backend '%1'.").arg(context->backend());
            return false;
        }
        const QStringList requiredServices =
            stringArray(metadata.value(QStringLiteral("RequiredServices")), &validArray);
        if (!validArray) {
            *error = QStringLiteral("required services metadata is invalid.");
            return false;
        }
        for (const QString &iid : requiredServices) {
            if (!isVersionedServiceId(iid)) {
                *error = QStringLiteral("required service ID '%1' is invalid.").arg(iid);
                return false;
            }
            if (!services.value(iid)) {
                *error = QStringLiteral("required service '%1' is unavailable.").arg(iid);
                return false;
            }
        }
        return true;
    }

    SentryLoadedIntegration *load(const SentryIntegrationDescriptorSnapshot &descriptor,
                                  const SentryIntegrationCandidate &candidate, QString *error)
    {
        for (const std::unique_ptr<SentryLoadedIntegration> &loaded : loadedIntegrations) {
            if (loaded->id != descriptor.id) {
                continue;
            }
            if (loaded->origin != candidate.origin) {
                *error =
                    QStringLiteral("a different binary for this ID is already loaded from %1.").arg(loaded->origin);
                return nullptr;
            }
            return loaded.get();
        }

        QObject *instance = nullptr;
        if (candidate.isStatic()) {
            instance = candidate.staticInstance();
        } else {
            auto loader = std::make_unique<QPluginLoader>(candidate.path);
            loader->setLoadHints(QLibrary::PreventUnloadHint);
            instance = loader->instance();
            if (!instance) {
                *error = QStringLiteral("could not load %1: %2").arg(candidate.path, loader->errorString());
                retainedLoaders.push_back(std::move(loader));
                return nullptr;
            }
            retainedLoaders.push_back(std::move(loader));
        }

        if (!instance) {
            *error = QStringLiteral("plugin root object could not be created.");
            return nullptr;
        }
        auto *plugin = qobject_cast<SentryIntegrationPlugin *>(instance);
        if (!plugin) {
            *error = QStringLiteral("plugin root object does not implement the v1 "
                                    "integration interface.");
            return nullptr;
        }

        auto loaded = std::make_unique<SentryLoadedIntegration>();
        loaded->id = descriptor.id;
        loaded->origin = candidate.origin;
        loaded->instance = instance;
        loaded->plugin = plugin;
        SentryLoadedIntegration *result = loaded.get();
        loadedIntegrations.push_back(std::move(loaded));
        return result;
    }

    void stopEntries() noexcept
    {
        if (cycle.empty()) {
            activeIds.clear();
            preparedIds.clear();
            backendReady = false;
            services.clear();
            setContextState(SentryIntegrationContext::Inactive);
            return;
        }

        setContextState(SentryIntegrationContext::Stopping);
        for (auto it = cycle.rbegin(); it != cycle.rend(); ++it) {
            it->loaded->plugin->stop();
        }
        cycle.clear();
        activeIds.clear();
        preparedIds.clear();
        backendReady = false;
        services.clear();
        setContextState(SentryIntegrationContext::Inactive);
    }

    SentryIntegrationManager *q = nullptr;
    SentrySdk *sdk = nullptr;
    std::unique_ptr<SentryIntegrationContext> context;
    std::vector<std::unique_ptr<QPluginLoader>> retainedLoaders;
    std::vector<std::unique_ptr<SentryLoadedIntegration>> loadedIntegrations;
    std::vector<SentryIntegrationCycleEntry> cycle;
    QHash<QString, QPointer<QObject>> services;
    QStringList preparedIds;
    QStringList activeIds;
    bool backendReady = false;
};

SentryIntegrationContext::SentryIntegrationContext(std::unique_ptr<SentryIntegrationContextPrivate> contextPrivate,
                                                   QObject *parent)
    : QObject(parent)
    , d(std::move(contextPrivate))
{
}

SentryIntegrationContext::~SentryIntegrationContext() = default;

QString SentryIntegrationContext::sdkVersion() const
{
    return d->sdkVersion;
}

QString SentryIntegrationContext::platform() const
{
    return d->platform;
}

QString SentryIntegrationContext::backend() const
{
    return d->backend;
}

QString SentryIntegrationContext::databasePath() const
{
    return d->databasePath;
}

QCoreApplication *SentryIntegrationContext::application() const
{
    return QCoreApplication::instance();
}

QThread *SentryIntegrationContext::guiThread() const
{
    return QCoreApplication::instance() ? QCoreApplication::instance()->thread() : nullptr;
}

SentryIntegrationContext::State SentryIntegrationContext::state() const
{
    return d->state;
}

void SentryIntegrationContext::reportWarning(const QString &message) const
{
    qWarning().noquote() << message;
    if (d->sentry) {
        emit d->sentry->errorOccurred(message);
    }
}

void SentryIntegrationContext::reportError(const QString &message) const
{
    qCritical().noquote() << message;
    if (d->sentry) {
        emit d->sentry->errorOccurred(message);
    }
}

bool SentryIntegrationContext::setContext(const QString &key, const QVariantMap &context)
{
    if (!d->manager->runtimeAvailable() || !d->sentry) {
        return false;
    }
    QScopedValueRollback<bool> operation(d->sdk->m_integrationOperation, true);
    return d->sdk->setContext(d->sentry, key, context);
}

bool SentryIntegrationContext::removeContext(const QString &key)
{
    if (!d->manager->runtimeAvailable() || !d->sentry) {
        return false;
    }
    QScopedValueRollback<bool> operation(d->sdk->m_integrationOperation, true);
    return d->sdk->removeContext(d->sentry, key);
}

bool SentryIntegrationContext::setAttribute(const QString &key, const QVariant &value)
{
    if (!d->manager->runtimeAvailable() || !d->sentry) {
        return false;
    }
    QScopedValueRollback<bool> operation(d->sdk->m_integrationOperation, true);
    return d->sdk->setAttribute(d->sentry, key, value);
}

bool SentryIntegrationContext::removeAttribute(const QString &key)
{
    if (!d->manager->runtimeAvailable() || !d->sentry) {
        return false;
    }
    QScopedValueRollback<bool> operation(d->sdk->m_integrationOperation, true);
    return d->sdk->removeAttribute(d->sentry, key);
}

bool SentryIntegrationContext::addBreadcrumb(const QVariantMap &breadcrumb)
{
    if (!d->manager->runtimeAvailable() || !d->sentry) {
        return false;
    }
    QScopedValueRollback<bool> operation(d->sdk->m_integrationOperation, true);
    return d->sdk->addBreadcrumb(d->sentry, breadcrumb);
}

QObject *SentryIntegrationContext::service(const QString &iid) const
{
    if (d->state == Inactive) {
        return nullptr;
    }
    return d->manager->service(iid);
}

SentryIntegrationManager::SentryIntegrationManager(SentrySdk *sdk)
    : d(std::make_unique<SentryIntegrationManagerPrivate>(this, sdk))
{
}

SentryIntegrationManager::~SentryIntegrationManager() = default;

void SentryIntegrationManager::beginInitialization(Sentry *sentry, SentryOptions *options, const QString &backend)
{
    Q_ASSERT(d->cycle.empty());
    d->services.clear();
    d->preparedIds.clear();
    d->activeIds.clear();
    d->context->d->sentry = sentry;
    d->context->d->platform = currentPlatform();
    d->context->d->backend = backend;
    d->context->d->databasePath =
        options->databasePath().isEmpty() ? QString() : canonicalOrCleanPath(options->databasePath());
}

bool SentryIntegrationManager::prepare(SentryOptions *options)
{
    Q_ASSERT(d->cycle.empty());
    QList<SentryIntegrationDescriptorSnapshot> descriptors;
    QStringList searchRoots;
    if (!d->snapshot(options, &descriptors, &searchRoots)) {
        d->stopEntries();
        return false;
    }
    if (descriptors.isEmpty()) {
        return true;
    }

    d->setContextState(SentryIntegrationContext::Preparing);
    for (const SentryIntegrationDescriptorSnapshot &descriptor : descriptors) {
        static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9.-]*$"));
        if (!idPattern.match(descriptor.id).hasMatch()) {
            d->diagnostic(descriptor.id, descriptor.required, QStringLiteral("ID must match [a-z0-9][a-z0-9.-]*."));
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }
        if (!descriptor.path.isEmpty() && !QFileInfo(descriptor.path).isAbsolute()) {
            d->diagnostic(descriptor.id, descriptor.required,
                          QStringLiteral("path must be an absolute plugin-library path."));
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }

        const QList<SentryIntegrationCandidate> candidates = d->candidates(descriptor, searchRoots);
        if (candidates.isEmpty()) {
            d->diagnostic(descriptor.id, descriptor.required, QStringLiteral("plugin was not found."));
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }
        if (candidates.size() != 1) {
            QStringList origins;
            for (const SentryIntegrationCandidate &candidate : candidates) {
                origins.append(candidate.origin);
            }
            d->diagnostic(
                descriptor.id, descriptor.required,
                QStringLiteral("multiple matching plugins were found: %1.").arg(origins.join(QStringLiteral(", "))));
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }

        QString error;
        const SentryIntegrationCandidate &candidate = candidates.constFirst();
        if (!d->validate(descriptor, candidate, &error)) {
            d->diagnostic(descriptor.id, descriptor.required, error);
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }
        SentryLoadedIntegration *loaded = d->load(descriptor, candidate, &error);
        if (!loaded) {
            d->diagnostic(descriptor.id, descriptor.required, error);
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }

        bool prepared = false;
        try {
            prepared = loaded->plugin->prepare(d->context.get(), descriptor.configuration, &error);
        } catch (const std::exception &exception) {
            error = exceptionError(exception);
        } catch (...) {
            error = QStringLiteral("threw an unknown exception during prepare.");
        }
        if (!prepared) {
            loaded->plugin->stop();
            d->diagnostic(descriptor.id, descriptor.required,
                          error.isEmpty() ? QStringLiteral("prepare failed.") : error);
            if (descriptor.required) {
                d->stopEntries();
                return false;
            }
            continue;
        }

        d->cycle.push_back({loaded, descriptor.required, SentryIntegrationCycleEntry::Prepared});
        d->preparedIds.append(descriptor.id);
    }

    if (d->cycle.empty()) {
        d->setContextState(SentryIntegrationContext::Inactive);
    }
    return true;
}

bool SentryIntegrationManager::start()
{
    d->backendReady = true;
    if (d->cycle.empty()) {
        return true;
    }

    d->setContextState(SentryIntegrationContext::Running);
    for (auto it = d->cycle.begin(); it != d->cycle.end();) {
        QString error;
        bool started = false;
        try {
            started = it->loaded->plugin->start(&error);
        } catch (const std::exception &exception) {
            error = exceptionError(exception);
        } catch (...) {
            error = QStringLiteral("threw an unknown exception during start.");
        }
        if (started) {
            it->state = SentryIntegrationCycleEntry::Running;
            d->activeIds.append(it->loaded->id);
            ++it;
            continue;
        }

        d->diagnostic(it->loaded->id, it->required, error.isEmpty() ? QStringLiteral("start failed.") : error);
        if (it->required) {
            d->stopEntries();
            return false;
        }
        it->loaded->plugin->stop();
        it = d->cycle.erase(it);
    }

    if (d->cycle.empty()) {
        d->setContextState(SentryIntegrationContext::Inactive);
    }
    return true;
}

bool SentryIntegrationManager::flush(int timeoutMs, int *remainingTimeoutMs)
{
    const int boundedTimeout = std::max(timeoutMs, 0);
    QDeadlineTimer deadline(boundedTimeout, Qt::PreciseTimer);
    bool success = true;
    if (!d->cycle.empty()) {
        d->setContextState(SentryIntegrationContext::Flushing);
    }

    for (const SentryIntegrationCycleEntry &entry : d->cycle) {
        if (entry.state != SentryIntegrationCycleEntry::Running) {
            continue;
        }
        const int remaining = std::max<qint64>(deadline.remainingTime(), 0);
        QString error;
        bool flushed = false;
        try {
            flushed = entry.loaded->plugin->flush(remaining, &error);
        } catch (const std::exception &exception) {
            error = exceptionError(exception);
        } catch (...) {
            error = QStringLiteral("threw an unknown exception during flush.");
        }
        if (!flushed) {
            success = false;
            d->diagnostic(entry.loaded->id, false, error.isEmpty() ? QStringLiteral("flush failed.") : error);
        }
    }

    if (!d->cycle.empty()) {
        d->setContextState(SentryIntegrationContext::Running);
    }
    if (remainingTimeoutMs) {
        *remainingTimeoutMs = std::max<qint64>(deadline.remainingTime(), 0);
    }
    return success;
}

void SentryIntegrationManager::stop() noexcept
{
    d->stopEntries();
}

QStringList SentryIntegrationManager::preparedIntegrationIds() const
{
    return d->preparedIds;
}

QStringList SentryIntegrationManager::activeIntegrationIds() const
{
    return d->activeIds;
}

bool SentryIntegrationManager::registerService(const QString &iid, QObject *service)
{
    if (!isVersionedServiceId(iid) || !service || d->services.contains(iid)) {
        return false;
    }
    d->services.insert(iid, service);
    return true;
}

QObject *SentryIntegrationManager::service(const QString &iid) const
{
    return d->services.value(iid);
}

bool SentryIntegrationManager::runtimeAvailable() const
{
    return d->backendReady && (d->context->state() == SentryIntegrationContext::Running ||
                               d->context->state() == SentryIntegrationContext::Flushing ||
                               d->context->state() == SentryIntegrationContext::Stopping);
}
