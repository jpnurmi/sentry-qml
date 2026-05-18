#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtQml/qqmlengine.h>

#include <memory>

class SentrySpanPrivate;

class SENTRYQML_EXPORT SentrySpan : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentrySpan)
    QML_UNCREATABLE("SentrySpan is returned by Sentry.startTransaction() and Sentry.startSpan().")

    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged)
    Q_PROPERTY(bool transaction READ isTransaction CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged)
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QVariantMap traceHeaders READ traceHeaders)

public:
    explicit SentrySpan(QObject *parent = nullptr);
    ~SentrySpan() override;

    bool isValid() const;
    bool isTransaction() const;
    bool isFinished() const;

    QString name() const;
    QString operation() const;
    QString description() const;

    QString status() const;
    void setStatus(const QString &status);

    QVariantMap data() const;
    QVariantMap tags() const;
    QVariantMap traceHeaders() const;

    Q_INVOKABLE bool setData(const QString &key, const QVariant &value);
    Q_INVOKABLE bool removeData(const QString &key);
    Q_INVOKABLE bool setTag(const QString &key, const QString &value);
    Q_INVOKABLE bool removeTag(const QString &key);
    Q_INVOKABLE bool finish(const QString &status = QString());
    Q_INVOKABLE QVariantMap toVariantMap() const;

signals:
    void validChanged();
    void nameChanged();
    void operationChanged();
    void descriptionChanged();
    void statusChanged();
    void dataChanged();
    void tagsChanged();

private:
    friend class SentrySdk;

    SentrySpan(void *handle,
               bool transaction,
               const QString &name,
               const QString &operation,
               const QString &description,
               QObject *parent = nullptr);

    void *handle() const;
    void invalidate();
    void setStatusLocally(const QString &status);
    void setDataLocally(const QString &key, const QVariant &value);
    void removeDataLocally(const QString &key);
    void setTagLocally(const QString &key, const QString &value);
    void removeTagLocally(const QString &key);

    std::unique_ptr<SentrySpanPrivate> d;
};
