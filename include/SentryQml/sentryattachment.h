#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qtypes.h>
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
    Q_PROPERTY(qint64 size READ size NOTIFY sizeChanged)

public:
    explicit SentryAttachment(QObject *parent = nullptr);
    ~SentryAttachment() override;

    bool isValid() const;

    QString filename() const;
    void setFilename(const QString &filename);

    QString contentType() const;
    void setContentType(const QString &contentType);

    qint64 size() const;

signals:
    void validChanged();
    void filenameChanged();
    void contentTypeChanged();
    void sizeChanged();

private:
    friend class SentrySdk;

    explicit SentryAttachment(void *handle, QObject *parent = nullptr);

    void *handle() const;
    void invalidate();
    void setSize(qint64 size);

    std::unique_ptr<SentryAttachmentPrivate> d;
};
