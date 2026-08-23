#pragma once

#include "replayspool.h"

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

class QMediaCaptureSession;
class QMediaRecorder;
class QTimer;
class QVideoFrameInput;

class ReplayEncoder final : public QObject
{
    Q_OBJECT

public:
    explicit ReplayEncoder(QObject *parent = nullptr);

    static bool isSupported(QString *error);
    static bool probe(QString *error);
    void encode(ReplayJob job);
    void cancel();

signals:
    void finished(bool success, const QString &path, const QString &category, const QString &message);

private:
    void pump();
    void fail(const QString &category, const QString &message);
    void complete();
    void reset();

    ReplayJob m_job;
    QMediaCaptureSession *m_session = nullptr;
    QMediaRecorder *m_recorder = nullptr;
    QVideoFrameInput *m_input = nullptr;
    QTimer *m_timeout = nullptr;
    qsizetype m_nextFrame = 0;
    QString m_actualPath;
    bool m_busy = false;
    bool m_finalizing = false;
};
