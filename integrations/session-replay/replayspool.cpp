#include "replayspool.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qsavefile.h>
#include <QtCore/quuid.h>
#include <QtGui/qimagewriter.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

QString manifestPath(const QString &directory)
{
    return QDir(directory).filePath(QStringLiteral("manifest.json"));
}

QString crashPath(const QString &directory)
{
    return QDir(directory).filePath(QStringLiteral("crash.envelope"));
}

QString lockPath(const QString &directory, const QString &name)
{
    return QDir(directory).filePath(name);
}

qint64 jsonInteger(const QJsonObject &object, const QString &key)
{
    constexpr double maximumExactInteger = 9007199254740991.0;
    const double number = object.value(key).toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number || number < -maximumExactInteger
        || number > maximumExactInteger) {
        return 0;
    }
    return static_cast<qint64>(number);
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("could not atomically write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace

ReplaySpool::ReplaySpool(QString root, ReplayConfiguration configuration)
    : m_root(std::move(root))
    , m_configuration(configuration)
{
}

bool ReplaySpool::isReplayId(const QString &id)
{
    if (id.size() != 32) {
        return false;
    }
    for (QChar character : id) {
        if ((character < u'0' || character > u'9') && (character < u'a' || character > u'f')) {
            return false;
        }
    }
    return true;
}

QString ReplaySpool::defaultRoot(const QString &databasePath)
{
    const QString database = databasePath.isEmpty()
        ? QDir::current().absoluteFilePath(QStringLiteral(".sentry-native"))
        : databasePath;
    return QDir(database).filePath(QStringLiteral("replay-frames"));
}

bool ReplaySpool::begin(const QString &replayId, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_directory.isEmpty()) {
        *error = QStringLiteral("replay spool is already recording");
        return false;
    }
    if (!isReplayId(replayId)) {
        *error = QStringLiteral("generated replay ID is invalid");
        return false;
    }
    if (!QDir().mkpath(m_root)) {
        *error = QStringLiteral("could not create replay spool root %1").arg(m_root);
        return false;
    }

    const QString directory = QDir(m_root).filePath(replayId);
    if (QFileInfo::exists(directory) || !QDir().mkpath(QDir(directory).filePath(QStringLiteral("frames")))) {
        *error = QStringLiteral("could not create replay spool %1").arg(directory);
        return false;
    }

    auto recordingLock =
        std::make_unique<QLockFile>(lockPath(directory, QStringLiteral("recording.lock")));
    recordingLock->setStaleLockTime(10000);
    if (!recordingLock->tryLock()) {
        QDir(directory).removeRecursively();
        *error = QStringLiteral("could not claim replay spool %1").arg(directory);
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonObject manifest {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("replayId"), replayId},
        {QStringLiteral("sampled"), true},
        {QStringLiteral("runId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("processId"), static_cast<double>(QCoreApplication::applicationPid())},
        {QStringLiteral("replayType"), QStringLiteral("buffer")},
        {QStringLiteral("segmentId"), 0},
        {QStringLiteral("startTimestampMs"), static_cast<double>(now)},
        {QStringLiteral("latestTimestampMs"), static_cast<double>(now)},
        {QStringLiteral("canvasWidth"), m_configuration.maxWidth},
        {QStringLiteral("canvasHeight"), m_configuration.maxHeight},
        {QStringLiteral("frameRate"), m_configuration.frameRate},
        {QStringLiteral("durationMs"), m_configuration.durationMs},
        {QStringLiteral("redactionPolicyVersion"), 1},
        {QStringLiteral("state"), QStringLiteral("recording")},
        {QStringLiteral("encodeAttempts"), 0},
        {QStringLiteral("frames"), QJsonArray()},
    };
    if (!writeManifest(directory, manifest, error)) {
        QDir(directory).removeRecursively();
        return false;
    }

    m_replayId = replayId;
    m_directory = directory;
    m_manifest = std::move(manifest);
    m_recordingLock = std::move(recordingLock);
    m_frameNumber = 0;
    return true;
}

bool ReplaySpool::append(const QImage &redactedFrame, qint64 timestampMs, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (m_directory.isEmpty() || redactedFrame.isNull()) {
        *error = QStringLiteral("replay spool is not recording");
        return false;
    }

    const QString name = QStringLiteral("%1.jpg").arg(++m_frameNumber, 8, 10, QLatin1Char('0'));
    const QString path = QDir(m_directory).filePath(QStringLiteral("frames/%1").arg(name));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("could not create replay frame: %1").arg(file.errorString());
        return false;
    }
    QImageWriter writer(&file, "jpg");
    writer.setQuality(m_configuration.imageQuality);
    if (!writer.write(redactedFrame) || !file.commit()) {
        *error = QStringLiteral("could not atomically write replay frame: %1").arg(writer.errorString());
        file.cancelWriting();
        return false;
    }

    QJsonObject candidate = m_manifest;
    QJsonArray frames = candidate.value(QStringLiteral("frames")).toArray();
    frames.append(QJsonObject {
        {QStringLiteral("name"), name},
        {QStringLiteral("timestampMs"), static_cast<double>(timestampMs)},
        {QStringLiteral("size"), static_cast<double>(QFileInfo(path).size())},
    });

    const qint64 cutoff = timestampMs - m_configuration.durationMs;
    const int maximumFrames = m_configuration.durationMs * m_configuration.frameRate / 1000 + 2;
    qint64 totalBytes = directoryBytes(m_root);
    QStringList removed;
    while (!frames.isEmpty()) {
        const QJsonObject first = frames.first().toObject();
        const bool expired = jsonInteger(first, QStringLiteral("timestampMs")) < cutoff;
        const bool overCount = frames.size() > maximumFrames;
        const bool overBytes = totalBytes > m_configuration.maxSpoolBytes;
        if (!expired && !overCount && !overBytes) {
            break;
        }
        removed.append(first.value(QStringLiteral("name")).toString());
        totalBytes -= jsonInteger(first, QStringLiteral("size"));
        frames.removeAt(0);
    }

    candidate.insert(QStringLiteral("frames"), frames);
    candidate.insert(QStringLiteral("latestTimestampMs"), static_cast<double>(timestampMs));
    if (!frames.isEmpty()) {
        candidate.insert(QStringLiteral("startTimestampMs"),
                         frames.first().toObject().value(QStringLiteral("timestampMs")));
    }
    if (!writeManifest(m_directory, candidate, error)) {
        QFile::remove(path);
        return false;
    }

    m_manifest = std::move(candidate);
    for (const QString &removedName : removed) {
        QFile::remove(QDir(m_directory).filePath(QStringLiteral("frames/%1").arg(removedName)));
    }
    return true;
}

void ReplaySpool::discardCurrent()
{
    QMutexLocker locker(&m_mutex);
    if (!m_directory.isEmpty() && isReplayId(m_replayId)) {
        m_recordingLock.reset();
        QDir(m_directory).removeRecursively();
    }
    m_replayId.clear();
    m_directory.clear();
    m_manifest = {};
    m_recordingLock.reset();
}

QString ReplaySpool::currentReplayId() const
{
    QMutexLocker locker(&m_mutex);
    return m_replayId;
}

bool ReplaySpool::readManifest(const QString &directory, QJsonObject *manifest)
{
    QFile file(manifestPath(directory));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 1024 * 1024) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    *manifest = document.object();
    return manifest->value(QStringLiteral("schemaVersion")).toInt() == 1
        && isReplayId(manifest->value(QStringLiteral("replayId")).toString());
}

bool ReplaySpool::writeManifest(const QString &directory, const QJsonObject &manifest, QString *error)
{
    return writeBytes(manifestPath(directory), QJsonDocument(manifest).toJson(QJsonDocument::Compact), error);
}

qint64 ReplaySpool::directoryBytes(const QString &directory)
{
    qint64 result = 0;
    QDir root(directory);
    const QFileInfoList entries = root.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        result += entry.isDir() ? directoryBytes(entry.absoluteFilePath()) : entry.size();
    }
    return result;
}

bool ReplaySpool::correlate(const QString &root, const SentryPreviousCrashRecord &crash, QString *error)
{
    if (!isReplayId(crash.replayId) || crash.envelope.isEmpty()) {
        *error = QStringLiteral("the previous crash has no valid replay correlation");
        return false;
    }
    const QString directory = QDir(root).filePath(crash.replayId);
    QJsonObject manifest;
    if (!readManifest(directory, &manifest)
        || manifest.value(QStringLiteral("replayId")).toString() != crash.replayId) {
        *error = QStringLiteral("no replay spool matches previous crash %1").arg(crash.eventId);
        return false;
    }
    if (!writeBytes(crashPath(directory), crash.envelope, error)) {
        return false;
    }

    const qint64 crashTimestamp = crash.timestamp.isValid()
        ? crash.timestamp.toMSecsSinceEpoch()
        : jsonInteger(manifest, QStringLiteral("latestTimestampMs"));
    manifest.insert(QStringLiteral("state"), QStringLiteral("crashed"));
    manifest.insert(QStringLiteral("crashEventId"), crash.eventId);
    manifest.insert(QStringLiteral("crashRecordId"), crash.id);
    manifest.insert(QStringLiteral("crashTimestampMs"), static_cast<double>(crashTimestamp));
    return writeManifest(directory, manifest, error);
}

QList<ReplayJob> ReplaySpool::pending(const QString &root, const ReplayConfiguration &configuration)
{
    QList<ReplayJob> jobs;
    QDir rootDirectory(root);
    for (const QFileInfo &entry : rootDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!isReplayId(entry.fileName())) {
            continue;
        }
        QJsonObject manifest;
        if (!readManifest(entry.absoluteFilePath(), &manifest)) {
            continue;
        }
        if (manifest.value(QStringLiteral("replayId")).toString() != entry.fileName()) {
            continue;
        }
        const QString state = manifest.value(QStringLiteral("state")).toString();
        if (state != QStringLiteral("crashed") && state != QStringLiteral("encoding")
            && state != QStringLiteral("failed")) {
            continue;
        }

        QFile crashFile(crashPath(entry.absoluteFilePath()));
        if (!crashFile.open(QIODevice::ReadOnly) || crashFile.size() <= 0
            || crashFile.size() > 32LL * 1024 * 1024) {
            continue;
        }
        ReplayJob job;
        job.replayId = entry.fileName();
        job.directory = entry.absoluteFilePath();
        job.outputPath = QDir(job.directory).filePath(QStringLiteral("replay-%1.mp4").arg(job.replayId));
        job.canvas = QSize(manifest.value(QStringLiteral("canvasWidth")).toInt(),
                           manifest.value(QStringLiteral("canvasHeight")).toInt());
        job.frameRate = manifest.value(QStringLiteral("frameRate")).toInt();
        if (job.canvas.width() <= 0 || job.canvas.height() <= 0 || job.canvas.width() > 4096
            || job.canvas.height() > 4096 || (job.canvas.width() % 2) != 0
            || (job.canvas.height() % 2) != 0 || job.frameRate < 1 || job.frameRate > 2) {
            continue;
        }
        job.attempts = manifest.value(QStringLiteral("encodeAttempts")).toInt();
        if (job.attempts >= 3) {
            QDir(job.directory).removeRecursively();
            continue;
        }
        job.crashTimestampMs = jsonInteger(manifest, QStringLiteral("crashTimestampMs"));
        job.crash.id = manifest.value(QStringLiteral("crashRecordId")).toString();
        job.crash.eventId = manifest.value(QStringLiteral("crashEventId")).toString();
        job.crash.replayId = job.replayId;
        job.crash.timestamp = QDateTime::fromMSecsSinceEpoch(job.crashTimestampMs).toUTC();
        job.crash.envelope = crashFile.readAll();

        const int durationMs = qBound(1000, manifest.value(QStringLiteral("durationMs")).toInt(), 20000);
        const qint64 cutoff = job.crashTimestampMs - durationMs;
        for (const QJsonValue &value : manifest.value(QStringLiteral("frames")).toArray()) {
            const QJsonObject frameObject = value.toObject();
            const QString name = frameObject.value(QStringLiteral("name")).toString();
            const qint64 timestamp = jsonInteger(frameObject, QStringLiteral("timestampMs"));
            const QString path = QDir(job.directory).filePath(QStringLiteral("frames/%1").arg(name));
            const qint64 declaredSize = jsonInteger(frameObject, QStringLiteral("size"));
            const QFileInfo frameInfo(path);
            if (QFileInfo(name).fileName() != name || timestamp < cutoff || timestamp > job.crashTimestampMs
                || !frameInfo.isFile() || declaredSize <= 0 || frameInfo.size() != declaredSize
                || frameInfo.size() > configuration.maxSpoolBytes) {
                continue;
            }
            job.frames.append({path, timestamp, declaredSize});
        }
        if (!job.frames.isEmpty() && job.canvas.width() > 0 && job.canvas.height() > 0) {
            jobs.append(std::move(job));
        }
    }

    std::sort(jobs.begin(), jobs.end(), [](const ReplayJob &left, const ReplayJob &right) {
        return left.crashTimestampMs > right.crashTimestampMs;
    });
    while (jobs.size() > configuration.maxPendingReplays) {
        const ReplayJob expired = jobs.takeLast();
        QLockFile lock(lockPath(expired.directory, QStringLiteral("encode.lock")));
        lock.setStaleLockTime(10000);
        if (lock.tryLock()) {
            QDir(expired.directory).removeRecursively();
        }
    }

    qint64 totalBytes = directoryBytes(root);
    for (qsizetype i = jobs.size() - 1; i >= 0 && totalBytes > configuration.maxSpoolBytes; --i) {
        const ReplayJob job = jobs.at(i);
        QLockFile lock(lockPath(job.directory, QStringLiteral("encode.lock")));
        lock.setStaleLockTime(10000);
        if (!lock.tryLock()) {
            continue;
        }
        const qint64 bytes = directoryBytes(job.directory);
        if (QDir(job.directory).removeRecursively()) {
            totalBytes -= bytes;
            jobs.removeAt(i);
        }
    }
    return jobs;
}

void ReplaySpool::cleanupRecordingOrphans(const QString &root, const QSet<QString> &matchedReplayIds)
{
    QDir rootDirectory(root);
    for (const QFileInfo &entry : rootDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!isReplayId(entry.fileName()) || matchedReplayIds.contains(entry.fileName())) {
            continue;
        }
        QLockFile lock(lockPath(entry.absoluteFilePath(), QStringLiteral("recording.lock")));
        lock.setStaleLockTime(10000);
        if (!lock.tryLock()) {
            continue;
        }
        QJsonObject manifest;
        if (!readManifest(entry.absoluteFilePath(), &manifest)
            || manifest.value(QStringLiteral("state")).toString() == QStringLiteral("recording")) {
            QDir(entry.absoluteFilePath()).removeRecursively();
        }
    }
}

bool ReplaySpool::markEncoding(const ReplayJob &job, QString *error)
{
    QJsonObject manifest;
    if (!isReplayId(job.replayId) || !readManifest(job.directory, &manifest)) {
        *error = QStringLiteral("replay manifest is unavailable");
        return false;
    }
    manifest.insert(QStringLiteral("state"), QStringLiteral("encoding"));
    manifest.insert(QStringLiteral("encodeAttempts"), job.attempts + 1);
    manifest.remove(QStringLiteral("lastErrorCategory"));
    return writeManifest(job.directory, manifest, error);
}

void ReplaySpool::markFailed(const ReplayJob &job, const QString &category)
{
    QFile::remove(job.outputPath);
    QJsonObject manifest;
    if (!readManifest(job.directory, &manifest)) {
        return;
    }
    manifest.insert(QStringLiteral("state"), QStringLiteral("failed"));
    manifest.insert(QStringLiteral("lastErrorCategory"), category);
    writeManifest(job.directory, manifest);
}

bool ReplaySpool::remove(const ReplayJob &job)
{
    return isReplayId(job.replayId) && QFileInfo(job.directory).fileName() == job.replayId
        && QDir(job.directory).removeRecursively();
}
