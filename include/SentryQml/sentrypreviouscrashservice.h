#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qdatetime.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

class SENTRYQML_EXPORT SentryPreviousCrashRecord
{
public:
    QString id;
    QString eventId;
    QString replayId;
    QDateTime timestamp;
    QByteArray envelope;
};

// Retains copies of crash envelopes delivered synchronously by the native SDK
// during initialization. Records remain available until a consumer explicitly
// acknowledges them.
class SENTRYQML_EXPORT SentryPreviousCrashService : public QObject
{
    Q_OBJECT

public:
    static constexpr auto ServiceId = "io.sentry.qml.previous-crash/1";

    explicit SentryPreviousCrashService(QObject *parent = nullptr);
    ~SentryPreviousCrashService() override;

    virtual QList<SentryPreviousCrashRecord> records() const = 0;
    virtual void acknowledge(const QString &id) = 0;
};
