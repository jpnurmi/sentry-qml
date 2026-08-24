#pragma once

#include "replayconfiguration.h"
#include "replayspool.h"

#include <SentryQml/sentryintegrationplugin.h>
#include <SentryQml/sentrypreviouscrashservice.h>
#include <SentryQml/sentryreplayvideoservice.h>

#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qthread.h>
#include <QtCore/qthreadpool.h>

#include <atomic>
#include <memory>

class QLockFile;
class ReplayCapture;
class ReplayEncoder;
class SentryIntegrationContext;

class SentryQmlSessionReplayIntegration final : public QObject, public SentryIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SENTRY_QML_INTEGRATION_IID FILE "sessionreplayintegration.json")
    Q_INTERFACES(SentryIntegrationPlugin)

public:
    SentryQmlSessionReplayIntegration();
    ~SentryQmlSessionReplayIntegration() override;

    bool prepare(SentryIntegrationContext *context, const QVariantMap &configuration, QString *error) override;
    bool start(QString *error) override;
    bool flush(int timeoutMs, QString *error) override;
    void stop() noexcept override;

signals:
    void jobsDrained();

private:
    void store(QImage image, qint64 timestampMs);
    void processNext();
    void encoded(bool success,
                 const QString &path,
                 const QString &category,
                 const QString &message);
    void finishJob(const ReplayJob &job);

    ReplayConfiguration m_configuration;
    QPointer<SentryIntegrationContext> m_context;
    QPointer<SentryPreviousCrashService> m_previousCrashes;
    QPointer<SentryReplayVideoService> m_submission;
    QString m_root;
    std::unique_ptr<ReplaySpool> m_spool;
    std::unique_ptr<ReplayCapture> m_capture;
    QThreadPool m_storage;
    std::atomic_bool m_storageBusy = false;
    QThread m_encoderThread;
    ReplayEncoder *m_encoder = nullptr;
    QList<ReplayJob> m_jobs;
    std::unique_ptr<ReplayJob> m_currentJob;
    std::unique_ptr<QLockFile> m_encodeLock;
    bool m_prepared = false;
    bool m_stopping = false;
};
