#pragma once

#include <SentryQml/sentrypreviouscrashservice.h>
#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

class SENTRYQML_EXPORT SentryReplayVideoService : public QObject
{
    Q_OBJECT

public:
    static constexpr auto ServiceId = "io.sentry.qml.replay-video/1";

    enum Result
    {
        Accepted,
        InvalidInput,
        BuildFailed,
        RateLimited,
        Unavailable
    };
    Q_ENUM(Result)

    explicit SentryReplayVideoService(QObject *parent = nullptr);
    ~SentryReplayVideoService() override;

    // On Accepted the SDK owns the constructed envelope. The video and crash
    // record remain caller-owned for every result.
    virtual Result submit(const QString &videoPath,
                          const QVariantMap &metadata,
                          const SentryPreviousCrashRecord &crash,
                          QString *error) = 0;
};
