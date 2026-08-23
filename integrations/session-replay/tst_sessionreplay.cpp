#include "replayconfiguration.h"
#include "replayencoder.h"
#include "replayprivacy.h"
#include "replayspool.h"

#include <QtCore/qdatetime.h>
#include <QtCore/qdir.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qtemporarydir.h>
#include <QtGui/qimage.h>
#include <QtQuick/qquickitem.h>
#include <QtTest/qtest.h>
#include <QtTest/qsignalspy.h>

#include <limits>

class TestTextField final : public QQuickItem
{
    Q_OBJECT
};

class SessionReplayTest final : public QObject
{
    Q_OBJECT

private slots:
    void configurationDefaults();
    void configurationRejectsInvalidValues();
    void explicitSubtreeMask();
    void passwordCannotBeUnmasked();
    void spoolCorrelation();
    void activeSpoolIsNotCleanedAsAnOrphan();
    void replayIdValidation();
    void encoderProducesVideo();
};

void SessionReplayTest::configurationDefaults()
{
    ReplayConfiguration configuration;
    QString error;
    QVERIFY(ReplayConfiguration::parse({}, nullptr, &configuration, &error));
    QCOMPARE(configuration.crashSampleRate, 0.0);
    QCOMPARE(configuration.durationMs, 5000);
    QCOMPARE(configuration.frameRate, 1);
    QVERIFY(configuration.maskAllText);
    QVERIFY(configuration.maskAllImages);
}

void SessionReplayTest::configurationRejectsInvalidValues()
{
    ReplayConfiguration configuration;
    QString error;
    QVERIFY(!ReplayConfiguration::parse({{QStringLiteral("frameRate"), 3}}, nullptr, &configuration, &error));
    QVERIFY(error.contains(QStringLiteral("frameRate")));

    error.clear();
    QVERIFY(!ReplayConfiguration::parse(
        {{QStringLiteral("crashSampleRate"), std::numeric_limits<double>::quiet_NaN()}},
        nullptr,
        &configuration,
        &error));
    QVERIFY(error.contains(QStringLiteral("finite")));

    error.clear();
    QVERIFY(!ReplayConfiguration::parse(
        {{QStringLiteral("maxWidth"), 1.0e300}}, nullptr, &configuration, &error));
    QVERIFY(error.contains(QStringLiteral("integer")));
}

void SessionReplayTest::activeSpoolIsNotCleanedAsAnOrphan()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ReplayConfiguration configuration;
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("replay-frames"));
    ReplaySpool spool(root, configuration);
    const QString replayId = QStringLiteral("0123456789abcdef0123456789abcdef");
    QString error;
    QVERIFY2(spool.begin(replayId, &error), qPrintable(error));

    ReplaySpool::cleanupRecordingOrphans(root, {});
    QVERIFY(QFileInfo::exists(QDir(root).filePath(replayId)));
    spool.discardCurrent();
}

void SessionReplayTest::explicitSubtreeMask()
{
    QQuickItem root;
    root.setWidth(400);
    root.setHeight(300);
    QQuickItem child(&root);
    child.setPosition(QPointF(20, 30));
    child.setSize(QSizeF(100, 50));
    child.setProperty("sentryReplayMask", true);

    ReplayPrivacy privacy;
    const QVector<QRectF> masks = privacy.masks(&root, false, false, false, nullptr);
    QCOMPARE(masks.size(), 1);
    QVERIFY(masks.first().contains(QPointF(20, 30)));
    QVERIFY(masks.first().contains(QPointF(120, 80)));
}

void SessionReplayTest::passwordCannotBeUnmasked()
{
    QQuickItem root;
    root.setSize(QSizeF(400, 300));
    TestTextField field;
    field.setParentItem(&root);
    field.setSize(QSizeF(100, 50));

    ReplayPrivacy privacy;
    QCOMPARE(privacy.masks(&root, true, false, false, nullptr).size(), 1);

    field.setProperty("sentryReplayUnmask", true);
    QVERIFY(privacy.masks(&root, true, false, false, nullptr).isEmpty());

    field.setProperty("echoMode", 1);
    QCOMPARE(privacy.masks(&root, true, false, false, nullptr).size(), 1);
}

void SessionReplayTest::spoolCorrelation()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ReplayConfiguration configuration;
    configuration.maxWidth = 320;
    configuration.maxHeight = 180;
    configuration.maxSpoolBytes = 4 * 1024 * 1024;
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("replay-frames"));
    ReplaySpool spool(root, configuration);
    const QString replayId = QStringLiteral("0123456789abcdef0123456789abcdef");
    QString error;
    QVERIFY2(spool.begin(replayId, &error), qPrintable(error));

    QImage redacted(configuration.maxWidth, configuration.maxHeight, QImage::Format_RGB32);
    redacted.fill(Qt::black);
    const qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QVERIFY2(spool.append(redacted, timestamp, &error), qPrintable(error));

    SentryPreviousCrashRecord crash;
    crash.id = QStringLiteral("record");
    crash.eventId = QStringLiteral("event");
    crash.replayId = replayId;
    crash.timestamp = QDateTime::fromMSecsSinceEpoch(timestamp + 100);
    crash.envelope = QByteArrayLiteral("serialized crash envelope");
    QVERIFY2(ReplaySpool::correlate(root, crash, &error), qPrintable(error));

    const QList<ReplayJob> jobs = ReplaySpool::pending(root, configuration);
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(jobs.first().replayId, replayId);
    QCOMPARE(jobs.first().frames.size(), 1);
    QCOMPARE(jobs.first().crash.envelope, crash.envelope);
    QVERIFY(ReplaySpool::remove(jobs.first()));
}

void SessionReplayTest::replayIdValidation()
{
    QVERIFY(ReplaySpool::isReplayId(QStringLiteral("0123456789abcdef0123456789abcdef")));
    QVERIFY(!ReplaySpool::isReplayId(QStringLiteral("../../outside")));
    QVERIFY(!ReplaySpool::isReplayId(QStringLiteral("0123456789ABCDEF0123456789ABCDEF")));
    QVERIFY(!ReplaySpool::isReplayId(QStringLiteral("٠١٢٣٤٥٦٧٨٩abcdef0123456789abcdef")));
}

void SessionReplayTest::encoderProducesVideo()
{
    QString supportError;
    if (!ReplayEncoder::probe(&supportError)) {
        QVERIFY(!supportError.isEmpty());
        return;
    }

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ReplayJob job;
    job.replayId = QStringLiteral("0123456789abcdef0123456789abcdef");
    job.directory = temporary.path();
    job.outputPath = QDir(temporary.path()).filePath(QStringLiteral("replay.mp4"));
    job.canvas = QSize(320, 180);
    job.frameRate = 1;
    const qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    job.crashTimestampMs = timestamp + 1500;
    for (int index = 0; index < 2; ++index) {
        QImage image(job.canvas, QImage::Format_RGB32);
        image.fill(index == 0 ? Qt::black : Qt::darkGray);
        const QString path = QDir(temporary.path()).filePath(QStringLiteral("%1.jpg").arg(index));
        QVERIFY(image.save(path, "jpg", 70));
        job.frames.append({path, timestamp + index * 1000, QFileInfo(path).size()});
    }

    ReplayEncoder encoder;
    QSignalSpy finished(&encoder, &ReplayEncoder::finished);
    encoder.encode(job);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
    const QList<QVariant> result = finished.takeFirst();
    QVERIFY2(result.at(0).toBool(), qPrintable(result.at(3).toString()));
    const QFileInfo output(result.at(1).toString());
    QVERIFY(output.isFile());
    QVERIFY(output.size() > 0);
}

QTEST_MAIN(SessionReplayTest)

#include "tst_sessionreplay.moc"
