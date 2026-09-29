#include <SentryQml/sentryattachment.h>

#include <SentryQml/private/sentrysdk_p.h>

struct SentryAttachmentPrivate
{
    void *handle = nullptr;
    QString filename;
    QString contentType;
    qint64 size = -1;
};

SentryAttachment::SentryAttachment(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentryAttachmentPrivate>())
{
}

SentryAttachment::SentryAttachment(void *handle,
                                 const QString &filename,
                                 const QString &contentType,
                                 qint64 size,
                                 QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentryAttachmentPrivate>())
{
    d->handle = handle;
    d->filename = filename;
    d->contentType = contentType;
    d->size = size;
}

SentryAttachment::~SentryAttachment()
{
    SentrySdk::instance()->detachAttachment(this);
}

bool SentryAttachment::isValid() const
{
    return d->handle != nullptr;
}

QString SentryAttachment::filename() const
{
    return d->filename;
}

QString SentryAttachment::contentType() const
{
    return d->contentType;
}

qint64 SentryAttachment::size() const
{
    return d->size;
}

void *SentryAttachment::handle() const
{
    return d->handle;
}

void SentryAttachment::invalidate()
{
    if (!d->handle) {
        return;
    }

    d->handle = nullptr;
    emit validChanged();
}
