#pragma once

#include "replayconfiguration.h"

#include <SentryQml/sentrypreviouscrashservice.h>

#include <QtCore/qjsonobject.h>
#include <QtCore/qlockfile.h>
#include <QtCore/qmutex.h>
#include <QtCore/qset.h>
#include <QtCore/qsize.h>
#include <QtCore/qstring.h>
#include <QtCore/qvector.h>
#include <QtGui/qimage.h>

#include <memory>

struct ReplayFrame
{
    QString path;
    qint64 timestampMs = 0;
    qint64 size = 0;
};

struct ReplayJob
{
    QString replayId;
    QString directory;
    QString outputPath;
    QSize canvas;
    int frameRate = 1;
    int attempts = 0;
    qint64 crashTimestampMs = 0;
    QVector<ReplayFrame> frames;
    SentryPreviousCrashRecord crash;
};

class ReplaySpool
{
public:
    ReplaySpool(QString root, ReplayConfiguration configuration);

    static bool isReplayId(const QString &id);
    static QString defaultRoot(const QString &databasePath);

    bool begin(const QString &replayId, QString *error);
    bool append(const QImage &redactedFrame, qint64 timestampMs, QString *error);
    void discardCurrent();
    QString currentReplayId() const;

    static bool correlate(const QString &root, const SentryPreviousCrashRecord &crash, QString *error);
    static QList<ReplayJob> pending(const QString &root, const ReplayConfiguration &configuration);
    static void cleanupRecordingOrphans(const QString &root, const QSet<QString> &matchedReplayIds);
    static bool markEncoding(const ReplayJob &job, QString *error);
    static void markFailed(const ReplayJob &job, const QString &category);
    static bool remove(const ReplayJob &job);

private:
    static bool readManifest(const QString &directory, QJsonObject *manifest);
    static bool writeManifest(const QString &directory, const QJsonObject &manifest, QString *error = nullptr);
    static qint64 directoryBytes(const QString &directory);

    QString m_root;
    ReplayConfiguration m_configuration;
    mutable QMutex m_mutex;
    QString m_replayId;
    QString m_directory;
    QJsonObject m_manifest;
    std::unique_ptr<QLockFile> m_recordingLock;
    quint64 m_frameNumber = 0;
};
