#include "replayencoder.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qdir.h>
#include <QtCore/qeventloop.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qtemporarydir.h>
#include <QtCore/qtimer.h>
#include <QtCore/qurl.h>
#include <QtGui/qimagereader.h>
#include <QtMultimedia/qmediacapturesession.h>
#include <QtMultimedia/qmediaformat.h>
#include <QtMultimedia/qmediarecorder.h>
#include <QtMultimedia/qvideoframe.h>
#include <QtMultimedia/qvideoframeformat.h>
#include <QtMultimedia/qvideoframeinput.h>

#include <algorithm>
#include <utility>

namespace {

constexpr qint64 maximumVideoBytes = 20LL * 1024 * 1024;

QMediaFormat replayFormat()
{
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    return format;
}

int videoByte(int value)
{
    return std::clamp(value, 0, 255);
}

QVideoFrame nv12Frame(const QImage &image)
{
    QVideoFrameFormat format(image.size(), QVideoFrameFormat::Format_NV12);
    format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
    format.setColorRange(QVideoFrameFormat::ColorRange_Video);
    QVideoFrame frame(format);
    if (!frame.isValid() || !frame.map(QVideoFrame::WriteOnly)) {
        return {};
    }
    if (frame.planeCount() < 2) {
        frame.unmap();
        return {};
    }

    uchar *yPlane = frame.bits(0);
    uchar *uvPlane = frame.bits(1);
    const int yStride = frame.bytesPerLine(0);
    const int uvStride = frame.bytesPerLine(1);
    if (!yPlane || !uvPlane || yStride < image.width() || uvStride < image.width()) {
        frame.unmap();
        return {};
    }

    for (int y = 0; y < image.height(); ++y) {
        const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *target = yPlane + y * yStride;
        for (int x = 0; x < image.width(); ++x) {
            const int red = qRed(source[x]);
            const int green = qGreen(source[x]);
            const int blue = qBlue(source[x]);
            target[x] = static_cast<uchar>(videoByte(((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16));
        }
    }

    for (int y = 0; y < image.height(); y += 2) {
        uchar *target = uvPlane + (y / 2) * uvStride;
        for (int x = 0; x < image.width(); x += 2) {
            int red = 0;
            int green = 0;
            int blue = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y + dy));
                for (int dx = 0; dx < 2; ++dx) {
                    red += qRed(source[x + dx]);
                    green += qGreen(source[x + dx]);
                    blue += qBlue(source[x + dx]);
                }
            }
            red /= 4;
            green /= 4;
            blue /= 4;
            target[x] = static_cast<uchar>(videoByte(((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128));
            target[x + 1] =
                static_cast<uchar>(videoByte(((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128));
        }
    }

    frame.unmap();
    return frame;
}

} // namespace

ReplayEncoder::ReplayEncoder(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        fail(QStringLiteral("encode-timeout"), QStringLiteral("video encoding did not finish within 60 seconds"));
    });
}

bool ReplayEncoder::isSupported(QString *error)
{
    const QString backend = qEnvironmentVariable("QT_MEDIA_BACKEND");
    if (!backend.isEmpty() && backend.compare(QStringLiteral("ffmpeg"), Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("QVideoFrameInput requires Qt Multimedia's FFmpeg backend, but QT_MEDIA_BACKEND is '%1'")
                     .arg(backend);
        return false;
    }
    if (!replayFormat().isSupported(QMediaFormat::Encode)) {
        *error = QStringLiteral("Qt Multimedia cannot encode H.264 video in an MPEG-4 container; deploy the FFmpeg "
                               "backend with an H.264 encoder");
        return false;
    }
    return true;
}

bool ReplayEncoder::probe(QString *error)
{
    if (!isSupported(error)) {
        return false;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        *error = QStringLiteral("could not create a temporary directory for the H.264 encoder check");
        return false;
    }

    ReplayJob job;
    job.outputPath = QDir(temporary.path()).filePath(QStringLiteral("probe.mp4"));
    job.canvas = QSize(128, 128);
    job.frameRate = 1;
    const qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    job.crashTimestampMs = timestamp + 2000;

    QImage image(job.canvas, QImage::Format_RGB32);
    image.fill(Qt::black);
    const QString framePath = QDir(temporary.path()).filePath(QStringLiteral("probe.jpg"));
    if (!image.save(framePath, "jpg", 70)) {
        *error = QStringLiteral("could not create a temporary frame for the H.264 encoder check");
        return false;
    }
    const qint64 frameSize = QFileInfo(framePath).size();
    job.frames.append({framePath, timestamp, frameSize});
    job.frames.append({framePath, timestamp + 1000, frameSize});

    ReplayEncoder encoder;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool finished = false;
    bool success = false;
    QString failure;
    connect(&encoder,
            &ReplayEncoder::finished,
            &loop,
            [&](bool result, const QString &, const QString &category, const QString &message) {
                finished = true;
                success = result;
                failure = message.isEmpty() ? category : message;
                loop.quit();
            });
    connect(&timeout, &QTimer::timeout, &encoder, [&encoder]() { encoder.cancel(); });

    encoder.encode(std::move(job));
    if (!finished) {
        timeout.start(10000);
        loop.exec();
    }

    if (!success) {
        *error = QStringLiteral("Qt Multimedia's H.264/MPEG-4 encoder failed its runtime check: %1")
                     .arg(failure.isEmpty() ? QStringLiteral("timed out") : failure);
    }
    return success;
}

void ReplayEncoder::encode(ReplayJob job)
{
    if (m_busy) {
        emit finished(false, QString(), QStringLiteral("encoder-busy"), QStringLiteral("the replay encoder is busy"));
        return;
    }
    QString supportError;
    if (!isSupported(&supportError)) {
        emit finished(false, QString(), QStringLiteral("codec-unavailable"), supportError);
        return;
    }
    if (job.frames.isEmpty() || job.canvas.width() <= 0 || job.canvas.height() <= 0
        || job.canvas.width() > 4096 || job.canvas.height() > 4096 || (job.canvas.width() % 2) != 0
        || (job.canvas.height() % 2) != 0 || job.frameRate < 1 || job.frameRate > 2) {
        emit finished(false, QString(), QStringLiteral("invalid-input"), QStringLiteral("the replay has no usable frames"));
        return;
    }

    m_job = std::move(job);
    m_nextFrame = 0;
    m_actualPath.clear();
    m_busy = true;
    m_finalizing = false;
    QFile::remove(m_job.outputPath);

    m_session = new QMediaCaptureSession(this);
    m_recorder = new QMediaRecorder(this);
    QVideoFrameFormat inputFormat(m_job.canvas, QVideoFrameFormat::Format_NV12);
    inputFormat.setStreamFrameRate(m_job.frameRate);
    m_input = new QVideoFrameInput(inputFormat, this);
    m_session->setRecorder(m_recorder);
    m_session->setVideoFrameInput(m_input);

    m_recorder->setMediaFormat(replayFormat());
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_job.outputPath));
    m_recorder->setAutoStop(true);
    m_recorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    m_recorder->setVideoFrameRate(m_job.frameRate);
    m_recorder->setVideoResolution(m_job.canvas);
    const int bitRate = std::clamp(m_job.canvas.width() * m_job.canvas.height() * m_job.frameRate, 250000, 2500000);
    m_recorder->setVideoBitRate(bitRate);

    connect(m_input, &QVideoFrameInput::readyToSendVideoFrame, this, &ReplayEncoder::pump);
    connect(m_recorder, &QMediaRecorder::actualLocationChanged, this, [this](const QUrl &location) {
        if (location.isLocalFile()) {
            m_actualPath = location.toLocalFile();
        }
    });
    connect(m_recorder,
            &QMediaRecorder::errorOccurred,
            this,
            [this](QMediaRecorder::Error, const QString &message) {
                fail(QStringLiteral("encode-error"), message);
            });
    connect(m_recorder,
            &QMediaRecorder::recorderStateChanged,
            this,
            [this](QMediaRecorder::RecorderState state) {
                if (!m_busy) {
                    return;
                }
                if (state == QMediaRecorder::RecordingState) {
                    pump();
                } else if (state == QMediaRecorder::StoppedState && m_finalizing) {
                    complete();
                }
            });

    m_timeout->start(60000);
    m_recorder->record();
    if (m_recorder->error() != QMediaRecorder::NoError) {
        fail(QStringLiteral("encode-error"), m_recorder->errorString());
    }
}

void ReplayEncoder::pump()
{
    if (!m_busy || m_finalizing || !m_input) {
        return;
    }
    while (m_nextFrame < m_job.frames.size()) {
        QImageReader reader(m_job.frames.at(m_nextFrame).path, "jpg");
        if (reader.size() != m_job.canvas) {
            fail(QStringLiteral("invalid-frame"), QStringLiteral("a replay frame has inconsistent dimensions"));
            return;
        }
        QImage image = reader.read();
        if (image.isNull()) {
            fail(QStringLiteral("invalid-frame"),
                 QStringLiteral("could not decode a redacted replay frame: %1").arg(reader.errorString()));
            return;
        }
        image = image.convertToFormat(QImage::Format_RGB32);
        QVideoFrame frame = nv12Frame(image);
        if (!frame.isValid()) {
            fail(QStringLiteral("invalid-frame"), QStringLiteral("could not convert a replay frame to NV12"));
            return;
        }
        const qint64 firstTimestamp = m_job.frames.first().timestampMs;
        const qint64 startUs = qMax<qint64>(0, m_job.frames.at(m_nextFrame).timestampMs - firstTimestamp) * 1000;
        qint64 endTimestamp = m_job.crashTimestampMs;
        if (m_nextFrame + 1 < m_job.frames.size()) {
            endTimestamp = m_job.frames.at(m_nextFrame + 1).timestampMs;
        } else {
            const qint64 maximumHold = qMax<qint64>(2000, 2000 / m_job.frameRate);
            endTimestamp = qMin(endTimestamp, m_job.frames.at(m_nextFrame).timestampMs + maximumHold);
        }
        frame.setStartTime(startUs);
        frame.setEndTime(qMax(startUs + 1000, (endTimestamp - firstTimestamp) * 1000));
        frame.setStreamFrameRate(m_job.frameRate);
        if (!m_input->sendVideoFrame(frame)) {
            return;
        }
        ++m_nextFrame;
    }

    m_finalizing = true;
    if (!m_input->sendVideoFrame(QVideoFrame())) {
        m_finalizing = false;
        return;
    }
}

void ReplayEncoder::cancel()
{
    fail(QStringLiteral("cancelled"), QStringLiteral("replay encoding was cancelled"));
}

void ReplayEncoder::fail(const QString &category, const QString &message)
{
    if (!m_busy) {
        return;
    }
    m_busy = false;
    m_timeout->stop();
    const QString output = m_actualPath.isEmpty() ? m_job.outputPath : m_actualPath;

    QTimer::singleShot(0, this, [this, category, message, output] {
        if (m_recorder && m_recorder->recorderState() != QMediaRecorder::StoppedState) {
            m_recorder->stop();
        }
        reset();
        QFile::remove(output);
        emit finished(false, QString(), category, message);
    });
}

void ReplayEncoder::complete()
{
    if (!m_busy || !m_recorder) {
        return;
    }
    m_timeout->stop();
    const QString output = m_actualPath.isEmpty() ? m_job.outputPath : m_actualPath;
    const QMediaFormat actualFormat = m_recorder->mediaFormat();
    const QSize actualResolution = m_recorder->videoResolution();
    const qint64 actualDuration = m_recorder->duration();
    const QFileInfo outputInfo(output);
    QString error;
    if (actualFormat.fileFormat() != QMediaFormat::MPEG4
        || actualFormat.videoCodec() != QMediaFormat::VideoCodec::H264) {
        error = QStringLiteral("Qt Multimedia changed the requested H.264/MPEG-4 format");
    } else if (actualResolution != m_job.canvas) {
        error = QStringLiteral("the finalized replay video has unexpected dimensions");
    } else if (actualDuration <= 0 || actualDuration > 22000) {
        error = QStringLiteral("the finalized replay video has an implausible duration");
    } else if (!outputInfo.isFile() || outputInfo.size() <= 0 || outputInfo.size() > maximumVideoBytes) {
        error = QStringLiteral("the finalized replay video is empty or exceeds 20 MiB");
    }

    m_busy = false;
    QTimer::singleShot(0, this, [this, error, output] {
        reset();
        if (!error.isEmpty()) {
            QFile::remove(output);
            emit finished(false, QString(), QStringLiteral("invalid-output"), error);
            return;
        }
        emit finished(true, output, QString(), QString());
    });
}

void ReplayEncoder::reset()
{
    if (m_session) {
        m_session->setVideoFrameInput(nullptr);
        m_session->setRecorder(nullptr);
    }
    delete m_input;
    delete m_recorder;
    delete m_session;
    m_input = nullptr;
    m_recorder = nullptr;
    m_session = nullptr;
    m_job = {};
    m_nextFrame = 0;
    m_actualPath.clear();
    m_finalizing = false;
}
