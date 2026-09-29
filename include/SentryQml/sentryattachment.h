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
    Q_PROPERTY(QString filename READ filename CONSTANT)
    Q_PROPERTY(QString contentType READ contentType CONSTANT)
    Q_PROPERTY(qint64 size READ size CONSTANT)

public:
    explicit SentryAttachment(QObject *parent = nullptr);
    ~SentryAttachment() override;

    bool isValid() const;

    QString filename() const;

    QString contentType() const;

    qint64 size() const;

signals:
    void validChanged();

private:
    friend class SentrySdk;

    SentryAttachment(void *handle,
                     const QString &filename,
                     const QString &contentType,
                     qint64 size,
                     QObject *parent = nullptr);

    void *handle() const;
    void invalidate();

    std::unique_ptr<SentryAttachmentPrivate> d;
};
