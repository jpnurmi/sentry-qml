#pragma once

#include <SentryQml/sentryattachment.h>
#include <SentryQml/sentryhint.h>
#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <QtCore/qvariant.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>
#include <QtQml/qqmlengine.h>

#include <memory>

class SentryQmlEngine;

class SENTRYQML_EXPORT Sentry : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Sentry)
    QML_SINGLETON

    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    enum Level
    {
        Trace = -2,
        Debug = -1,
        Info = 0,
        Warning = 1,
        Error = 2,
        Fatal = 3
    };
    Q_ENUM(Level)

    enum MetricType
    {
        Count = 0,
        Gauge = 1,
        Distribution = 2
    };
    Q_ENUM(MetricType)

    enum SessionStatus
    {
        SessionOk = 0,
        SessionCrashed = 1,
        SessionAbnormal = 2,
        SessionExited = 3
    };
    Q_ENUM(SessionStatus)

    explicit Sentry(QObject *parent = nullptr);
    ~Sentry() override;

    static Sentry *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    bool isInitialized() const;

    Q_INVOKABLE bool init(SentryOptions *options);
    Q_INVOKABLE bool flush(int timeoutMs = 2000);
    Q_INVOKABLE bool close();
    Q_INVOKABLE bool setRelease(const QString &release);
    Q_INVOKABLE bool setEnvironment(const QString &environment);
    Q_INVOKABLE bool setUser(const QVariantMap &user);
    Q_INVOKABLE bool removeUser();
    Q_INVOKABLE bool setTag(const QString &key, const QString &value);
    Q_INVOKABLE bool removeTag(const QString &key);
    Q_INVOKABLE bool setContext(const QString &key, const QVariantMap &context);
    Q_INVOKABLE bool removeContext(const QString &key);
    Q_INVOKABLE bool setAttribute(const QString &key, const QVariant &value);
    Q_INVOKABLE bool removeAttribute(const QString &key);
    Q_INVOKABLE bool setFingerprint(const QStringList &fingerprint);
    Q_INVOKABLE bool removeFingerprint();
    Q_INVOKABLE SentryAttachment *attachFile(const QString &path, const QString &contentType = QString());
    Q_INVOKABLE SentryAttachment *attachBytes(const QByteArray &bytes,
                                              const QString &filename,
                                              const QString &contentType = QString());
    Q_INVOKABLE bool removeAttachment(SentryAttachment *attachment);
    Q_INVOKABLE bool clearAttachments();
    Q_INVOKABLE bool startSession();
    Q_INVOKABLE bool endSession();
    Q_INVOKABLE bool endSession(SessionStatus status);
    Q_INVOKABLE bool addBreadcrumb(const QVariantMap &breadcrumb);
    bool addBreadcrumb(const QString &message,
                       const QString &category = QString(),
                       const QString &type = QStringLiteral("default"),
                       const QString &level = QStringLiteral("info"),
                       const QVariantMap &data = {});
    Q_INVOKABLE bool log(Level level, const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool trace(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool debug(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool info(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool warn(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool error(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool fatal(const QString &message, const QVariantMap &attributes = {});
    Q_INVOKABLE bool metric(MetricType type,
                            const QString &name,
                            double value,
                            const QString &unit = QString(),
                            const QVariantMap &attributes = {});
    Q_INVOKABLE bool count(const QString &name, qint64 value = 1, const QVariantMap &attributes = {});
    Q_INVOKABLE bool gauge(const QString &name,
                           double value,
                           const QString &unit = QString(),
                           const QVariantMap &attributes = {});
    Q_INVOKABLE bool distribution(const QString &name,
                                  double value,
                                  const QString &unit = QString(),
                                  const QVariantMap &attributes = {});
    Q_INVOKABLE QString captureMessage(const QString &message, const QString &level = QStringLiteral("info"));
    Q_INVOKABLE QString captureException(const QJSValue &exception);
    Q_INVOKABLE bool captureFeedback(const QVariantMap &feedback, SentryHint *hint = nullptr);

signals:
    void initializedChanged();
    void errorOccurred(const QString &message);

private:
    void ensureQmlEngine(QQmlEngine *engine);

    std::unique_ptr<SentryQmlEngine> m_qmlEngine;
};
