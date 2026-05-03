#pragma once

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>

#include <functional>

namespace SentryObjCBridge {

struct HookResult
{
    enum Action
    {
        Keep,
        Drop,
        Replace
    };

    Action action = Keep;
    QVariant value;
};

using Hook = std::function<HookResult(const QVariant &)>;

struct Attachment
{
    enum Type
    {
        File,
        Bytes
    };

    Type type = File;
    QString path;
    QByteArray bytes;
    QString filename;
    QString contentType;
};

struct Options
{
    QString dsn;
    QString databasePath;
    QString release;
    QString environment;
    QString dist;
    QVariantMap user;
    bool debug = false;
    bool enableLogs = true;
    bool enableMetrics = true;
    bool autoSessionTracking = true;
    double sampleRate = 1.0;
    int maxBreadcrumbs = 100;
    int shutdownTimeout = 2000;
    Hook beforeBreadcrumb;
    Hook beforeSendLog;
    Hook beforeSendMetric;
    Hook beforeSend;
    Hook onCrash;
};

bool isEnabled();
bool start(const Options &options);
void flush(int timeoutMs);
void close();

void setRelease(const QString &release);
void setEnvironment(const QString &environment);
void setUser(const QVariantMap &user);
void removeUser();
void setTag(const QString &key, const QString &value);
void removeTag(const QString &key);
void setContext(const QString &key, const QVariantMap &context);
void removeContext(const QString &key);
void setAttribute(const QString &key, const QVariant &value);
void removeAttribute(const QString &key);
void setFingerprint(const QStringList &fingerprint);
void clearFingerprint();
void setAttachments(const QList<Attachment> &attachments);
void clearAttachments();
void startSession();
void endSession();
void addBreadcrumb(const QVariantMap &breadcrumb);
void log(int level, const QString &message, const QVariantMap &attributes);
void count(const QString &name, quint64 value, const QVariantMap &attributes);
void gauge(const QString &name, double value, const QString &unit, const QVariantMap &attributes);
void distribution(const QString &name, double value, const QString &unit, const QVariantMap &attributes);
QString captureEvent(const QVariantMap &event, const QStringList &fingerprint);
void captureFeedback(const QVariantMap &feedback, const QList<Attachment> &attachments);

} // namespace SentryObjCBridge
