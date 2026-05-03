#pragma once

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>

#include <memory>

class Sentry;
class SentryAttachment;
class SentryHint;
struct SentrySdkEventHookState;
struct SentrySdkCrashHookState;
class SentryOptions;

enum class SentrySdkCaptureMode
{
    Manual,
    Automatic
};

class SentrySdk : public QObject
{
    Q_OBJECT

public:
    static SentrySdk *instance();

    bool isInitialized() const;
    bool init(Sentry *sentry, SentryOptions *options);
    bool flush(int timeoutMs);
    bool close();
    void detachSentry(Sentry *sentry);

    bool setRelease(Sentry *sentry, const QString &release);
    bool setEnvironment(Sentry *sentry, const QString &environment);
    bool setUser(Sentry *sentry, const QVariantMap &user);
    bool removeUser(Sentry *sentry);
    bool setTag(Sentry *sentry, const QString &key, const QString &value);
    bool removeTag(Sentry *sentry, const QString &key);
    bool setContext(Sentry *sentry, const QString &key, const QVariantMap &context);
    bool removeContext(Sentry *sentry, const QString &key);
    bool setAttribute(Sentry *sentry, const QString &key, const QVariant &value);
    bool removeAttribute(Sentry *sentry, const QString &key);
    bool setFingerprint(Sentry *sentry, const QStringList &fingerprint);
    bool removeFingerprint(Sentry *sentry);
    SentryAttachment *attachFile(Sentry *sentry, const QString &path, const QString &contentType);
    SentryAttachment *attachBytes(Sentry *sentry,
                                  const QByteArray &bytes,
                                  const QString &filename,
                                  const QString &contentType);
    bool removeAttachment(Sentry *sentry, SentryAttachment *attachment);
    bool clearAttachments(Sentry *sentry);
    bool startSession(Sentry *sentry);
    bool endSession(Sentry *sentry, int status);
    bool addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb);
    bool log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes);
    bool count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes);
    bool gauge(Sentry *sentry,
               const QString &name,
               double value,
               const QString &unit,
               const QVariantMap &attributes);
    bool distribution(Sentry *sentry,
                      const QString &name,
                      double value,
                      const QString &unit,
                      const QVariantMap &attributes);
    QString captureMessage(Sentry *sentry, const QString &message, const QString &level);
    QString captureEvent(Sentry *sentry, const QVariantMap &event, SentrySdkCaptureMode mode);
    bool captureFeedback(Sentry *sentry, const QVariantMap &feedback, SentryHint *hint);
    void applyFingerprintToEvent(QVariantMap *event) const;

signals:
    void initializedChanged();

private:
    friend class SentryAttachment;

    explicit SentrySdk(QObject *parent = nullptr);
    ~SentrySdk() override;

    void closeBeforeApplicationShutdown();
    void connectToApplicationShutdown();
    bool ensureCanCall(Sentry *sentry,
                       const char *method,
                       const char *action,
                       const char *hookType = "event hooks") const;
    bool ensureInitialized(Sentry *sentry, const char *action) const;
    void trackAttachment(SentryAttachment *attachment);
    void detachAttachment(SentryAttachment *attachment);
    void invalidateAttachments();
    void updateAttachments();
    void setAttachmentFilename(SentryAttachment *attachment, const QString &filename);
    void setAttachmentContentType(SentryAttachment *attachment, const QString &contentType);
    void clearLocalScope();
    void applyLocalScopeToEvent(QVariantMap *event) const;
    void setInitialized(bool initialized);

    std::unique_ptr<SentrySdkEventHookState> m_beforeSendState;
    std::unique_ptr<SentrySdkEventHookState> m_beforeBreadcrumbState;
    std::unique_ptr<SentrySdkEventHookState> m_beforeSendLogState;
    std::unique_ptr<SentrySdkEventHookState> m_beforeSendMetricState;
    std::unique_ptr<SentrySdkEventHookState> m_onCrashState;
    std::unique_ptr<SentrySdkCrashHookState> m_crashHookState;
    QString m_release;
    QString m_environment;
    QString m_dist;
    QVariantMap m_user;
    QVariantMap m_tags;
    QVariantMap m_contexts;
    QVariantList m_breadcrumbs;
    QStringList m_fingerprint;
    QList<SentryAttachment *> m_attachments;
    QMetaObject::Connection m_applicationShutdownConnection;
    int m_maxBreadcrumbs = 100;
    bool m_applyHooksLocally = false;
    bool m_initialized = false;
};
