#include "sessionreplayintegration.h"

#include "replaycapture.h"
#include "replayencoder.h"

#include <SentryQml/sentryintegrationcontext.h>

#include <QtCore/qdatetime.h>
#include <QtCore/qdir.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qfile.h>
#include <QtCore/qlockfile.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qrandom.h>
#include <QtCore/qset.h>
#include <QtCore/qtimer.h>
#include <QtCore/quuid.h>
#include <QtGui/qimagewriter.h>

#include <utility>

namespace {

QString newReplayId()
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    id.remove(QLatin1Char('-'));
    return id;
}

bool hasJpegWriter()
{
    const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
    return formats.contains(QByteArrayLiteral("jpg")) || formats.contains(QByteArrayLiteral("jpeg"));
}

bool permanentEncodeFailure(const QString &category)
{
    return category == QStringLiteral("codec-unavailable") || category == QStringLiteral("invalid-input")
        || category == QStringLiteral("invalid-frame") || category == QStringLiteral("invalid-output");
}

} // namespace

SentryQmlSessionReplayIntegration::SentryQmlSessionReplayIntegration()
{
    m_storage.setMaxThreadCount(1);
    m_storage.setExpiryTimeout(-1);
    m_encoderThread.setObjectName(QStringLiteral("SentryQmlReplayEncoder"));
}

SentryQmlSessionReplayIntegration::~SentryQmlSessionReplayIntegration()
{
    stop();
}

bool SentryQmlSessionReplayIntegration::prepare(SentryIntegrationContext *context,
                                               const QVariantMap &configuration,
                                               QString *error)
{
    if (!context) {
        *error = QStringLiteral("integration context is unavailable");
        return false;
    }
    if (!ReplayConfiguration::parse(configuration, context, &m_configuration, error)) {
        return false;
    }
    if (!hasJpegWriter()) {
        *error = QStringLiteral("Qt has no JPEG image writer; Session Replay cannot create a bounded frame spool");
        return false;
    }
    if (!ReplayEncoder::probe(error)) {
        return false;
    }

    m_previousCrashes = qobject_cast<SentryPreviousCrashService *>(
        context->service(QString::fromLatin1(SentryPreviousCrashService::ServiceId)));
    m_submission = qobject_cast<SentryReplayVideoService *>(
        context->service(QString::fromLatin1(SentryReplayVideoService::ServiceId)));
    if (!m_previousCrashes || !m_submission) {
        *error = QStringLiteral("native previous-crash and replay-video services are required");
        return false;
    }

    m_context = context;
    m_root = ReplaySpool::defaultRoot(context->databasePath());
    m_spool = std::make_unique<ReplaySpool>(m_root, m_configuration);
    m_prepared = true;
    return true;
}

bool SentryQmlSessionReplayIntegration::start(QString *)
{
    if (!m_prepared || !m_context || !m_previousCrashes || !m_submission) {
        return false;
    }
    m_stopping = false;
    if (!m_encoderThread.isRunning()) {
        m_encoder = new ReplayEncoder;
        m_encoder->moveToThread(&m_encoderThread);
        connect(m_encoder,
                &ReplayEncoder::finished,
                this,
                &SentryQmlSessionReplayIntegration::encoded,
                Qt::QueuedConnection);
        m_encoderThread.start(QThread::LowPriority);
    }

    QSet<QString> matched;
    const QList<SentryPreviousCrashRecord> crashRecords = m_previousCrashes->records();
    for (const SentryPreviousCrashRecord &crash : crashRecords) {
        QString correlationError;
        if (ReplaySpool::correlate(m_root, crash, &correlationError)) {
            matched.insert(crash.replayId);
        } else {
            m_previousCrashes->acknowledge(crash.id);
            if (m_configuration.debugArtifacts) {
                m_context->reportWarning(QStringLiteral("Sentry Session Replay: %1.").arg(correlationError));
            }
        }
    }
    ReplaySpool::cleanupRecordingOrphans(m_root, matched);
    m_jobs = ReplaySpool::pending(m_root, m_configuration);

    QSet<QString> pendingIds;
    for (const ReplayJob &job : std::as_const(m_jobs)) {
        pendingIds.insert(job.replayId);
    }
    for (const SentryPreviousCrashRecord &crash : crashRecords) {
        if (matched.contains(crash.replayId) && !pendingIds.contains(crash.replayId)) {
            m_previousCrashes->acknowledge(crash.id);
            ReplayJob unusable;
            unusable.replayId = crash.replayId;
            unusable.directory = QDir(m_root).filePath(crash.replayId);
            ReplaySpool::remove(unusable);
        }
    }

    if (QRandomGenerator::global()->generateDouble() < m_configuration.crashSampleRate) {
        const QString replayId = newReplayId();
        QString spoolError;
        if (!m_spool->begin(replayId, &spoolError)) {
            m_context->reportWarning(QStringLiteral("Sentry Session Replay capture is disabled: %1.").arg(spoolError));
        } else if (!m_context->setContext(QStringLiteral("replay"), {{QStringLiteral("replay_id"), replayId}})
                   || !m_context->setAttribute(QStringLiteral("sentry.replay_id"), replayId)
                   || !m_context->setAttribute(QStringLiteral("sentry._internal.replay_is_buffering"), true)) {
            m_spool->discardCurrent();
            m_context->reportWarning(QStringLiteral("Sentry Session Replay could not advertise replay correlation."));
        } else {
            m_capture = std::make_unique<ReplayCapture>(
                m_configuration,
                m_context,
                [this](QImage image, qint64 timestampMs) { store(std::move(image), timestampMs); },
                this);
            m_capture->start();
        }
    }

    QTimer::singleShot(0, this, &SentryQmlSessionReplayIntegration::processNext);
    return true;
}

bool SentryQmlSessionReplayIntegration::flush(int timeoutMs, QString *error)
{
    if (!m_storage.waitForDone(qMax(timeoutMs, 0))) {
        *error = QStringLiteral("timed out while writing replay frames");
        return false;
    }
    if (!m_currentJob && m_jobs.isEmpty()) {
        return true;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &SentryQmlSessionReplayIntegration::jobsDrained, &loop, &QEventLoop::quit);
    timer.start(qMax(timeoutMs, 0));
    loop.exec();
    if (m_currentJob || !m_jobs.isEmpty()) {
        *error = QStringLiteral("timed out while encoding or submitting pending replays");
        return false;
    }
    return true;
}

void SentryQmlSessionReplayIntegration::stop() noexcept
{
    if (!m_prepared && !m_encoderThread.isRunning()) {
        return;
    }
    m_stopping = true;
    if (m_capture) {
        m_capture->stop();
        m_capture.reset();
    }
    m_storage.waitForDone();
    m_storageBusy.store(false, std::memory_order_release);

    if (m_spool) {
        m_spool->discardCurrent();
    }
    if (m_context) {
        m_context->removeAttribute(QStringLiteral("sentry._internal.replay_is_buffering"));
        m_context->removeAttribute(QStringLiteral("sentry.replay_id"));
        m_context->removeContext(QStringLiteral("replay"));
    }

    if (m_encoder && m_encoderThread.isRunning()) {
        ReplayEncoder *encoder = m_encoder;
        QMetaObject::invokeMethod(encoder, [encoder]() { encoder->cancel(); }, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(encoder, [encoder]() { delete encoder; }, Qt::BlockingQueuedConnection);
        m_encoder = nullptr;
    }
    m_encoderThread.quit();
    m_encoderThread.wait();

    m_encodeLock.reset();
    m_currentJob.reset();
    m_jobs.clear();
    m_spool.reset();
    m_previousCrashes = nullptr;
    m_submission = nullptr;
    m_context = nullptr;
    m_root.clear();
    m_prepared = false;
}

void SentryQmlSessionReplayIntegration::store(QImage image, qint64 timestampMs)
{
    if (m_stopping || !m_spool || image.isNull()
        || m_storageBusy.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    m_storage.start([this, image = std::move(image), timestampMs]() {
        QString error;
        const bool stored = m_spool->append(image, timestampMs, &error);
        m_storageBusy.store(false, std::memory_order_release);
        if (!stored) {
            QMetaObject::invokeMethod(this, [this, error]() {
                if (m_context && !m_stopping) {
                    m_context->reportWarning(QStringLiteral("Sentry Session Replay dropped a frame: %1.").arg(error));
                }
            });
        }
    });
}

void SentryQmlSessionReplayIntegration::processNext()
{
    if (m_stopping || m_currentJob || !m_encoder) {
        return;
    }
    if (m_jobs.isEmpty()) {
        emit jobsDrained();
        return;
    }

    ReplayJob job = m_jobs.takeFirst();
    auto lock = std::make_unique<QLockFile>(QDir(job.directory).filePath(QStringLiteral("encode.lock")));
    lock->setStaleLockTime(10000);
    if (!lock->tryLock()) {
        QTimer::singleShot(0, this, &SentryQmlSessionReplayIntegration::processNext);
        return;
    }
    QString error;
    if (!ReplaySpool::markEncoding(job, &error)) {
        if (m_context) {
            m_context->reportWarning(QStringLiteral("Sentry Session Replay could not claim a replay: %1.").arg(error));
        }
        QTimer::singleShot(0, this, &SentryQmlSessionReplayIntegration::processNext);
        return;
    }

    m_encodeLock = std::move(lock);
    m_currentJob = std::make_unique<ReplayJob>(job);
    ReplayEncoder *encoder = m_encoder;
    QMetaObject::invokeMethod(encoder, [encoder, job = std::move(job)]() mutable { encoder->encode(std::move(job)); });
}

void SentryQmlSessionReplayIntegration::encoded(bool success,
                                                const QString &path,
                                                const QString &category,
                                                const QString &message)
{
    if (!m_currentJob) {
        return;
    }
    const ReplayJob job = *m_currentJob;
    if (m_stopping) {
        ReplaySpool::markFailed(job, QStringLiteral("cancelled"));
        finishJob(job);
        return;
    }

    if (!success) {
        ReplaySpool::markFailed(job, category);
        if (m_context) {
            m_context->reportWarning(QStringLiteral("Sentry Session Replay encoding failed: %1.").arg(message));
        }
        if (permanentEncodeFailure(category)) {
            if (m_previousCrashes) {
                m_previousCrashes->acknowledge(job.crash.id);
            }
            m_encodeLock.reset();
            ReplaySpool::remove(job);
        }
        finishJob(job);
        return;
    }

    const qint64 startMs = job.frames.first().timestampMs;
    const double durationMs = qMax<qint64>(1, job.crashTimestampMs - startMs);
    QVariantMap metadata {
        {QStringLiteral("replayId"), job.replayId},
        {QStringLiteral("replayType"), QStringLiteral("buffer")},
        {QStringLiteral("segmentId"), 0},
        {QStringLiteral("durationMs"), durationMs},
        {QStringLiteral("endTimestampSec"), job.crashTimestampMs / 1000.0},
        {QStringLiteral("width"), job.canvas.width()},
        {QStringLiteral("height"), job.canvas.height()},
        {QStringLiteral("frameCount"), job.frames.size()},
        {QStringLiteral("frameRate"), job.frameRate},
    };
    QString submissionError;
    const SentryReplayVideoService::Result result = m_submission
        ? m_submission->submit(path, metadata, job.crash, &submissionError)
        : SentryReplayVideoService::Unavailable;
    if (result == SentryReplayVideoService::Accepted) {
        if (m_previousCrashes) {
            m_previousCrashes->acknowledge(job.crash.id);
        }
        m_encodeLock.reset();
        ReplaySpool::remove(job);
    } else {
        QFile::remove(path);
        ReplaySpool::markFailed(job, QStringLiteral("submission"));
        if (m_context) {
            m_context->reportWarning(
                QStringLiteral("Sentry Session Replay submission failed: %1.").arg(submissionError));
        }
        if (result == SentryReplayVideoService::InvalidInput) {
            if (m_previousCrashes) {
                m_previousCrashes->acknowledge(job.crash.id);
            }
            m_encodeLock.reset();
            ReplaySpool::remove(job);
        }
    }
    finishJob(job);
}

void SentryQmlSessionReplayIntegration::finishJob(const ReplayJob &)
{
    m_encodeLock.reset();
    m_currentJob.reset();
    QTimer::singleShot(0, this, &SentryQmlSessionReplayIntegration::processNext);
}
