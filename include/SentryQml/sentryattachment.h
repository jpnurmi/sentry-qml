#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtQml/qqmlengine.h>

#include <memory>

class SentryAttachmentPrivate;

class SENTRYQML_EXPORT SentryAttachment : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentryAttachment)
    QML_UNCREATABLE("SentryAttachment is returned by Sentry.attachFile() and Sentry.attachBytes().")

    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged)
    Q_PROPERTY(QString filename READ filename WRITE setFilename NOTIFY filenameChanged)
    Q_PROPERTY(QString contentType READ contentType WRITE setContentType NOTIFY contentTypeChanged)

public:
    explicit SentryAttachment(QObject *parent = nullptr);
    ~SentryAttachment() override;

    bool isValid() const;

    QString filename() const;
    void setFilename(const QString &filename);

    QString contentType() const;
    void setContentType(const QString &contentType);

signals:
    void validChanged();
    void filenameChanged();
    void contentTypeChanged();

private:
    friend class SentryNativeSdk;

    explicit SentryAttachment(void *handle, QObject *parent = nullptr);

    void *handle() const;
    void invalidate();

    std::unique_ptr<SentryAttachmentPrivate> d;
};
