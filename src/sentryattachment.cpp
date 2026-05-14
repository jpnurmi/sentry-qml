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

SentryAttachment::SentryAttachment(void *handle, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentryAttachmentPrivate>())
{
    d->handle = handle;
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

void SentryAttachment::setFilename(const QString &filename)
{
    if (d->filename == filename) {
        return;
    }

    d->filename = filename;
    SentrySdk::instance()->setAttachmentFilename(this, filename);
    emit filenameChanged();
}

QString SentryAttachment::contentType() const
{
    return d->contentType;
}

void SentryAttachment::setContentType(const QString &contentType)
{
    if (d->contentType == contentType) {
        return;
    }

    d->contentType = contentType;
    SentrySdk::instance()->setAttachmentContentType(this, contentType);
    emit contentTypeChanged();
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

void SentryAttachment::setSize(qint64 size)
{
    if (d->size == size) {
        return;
    }

    d->size = size;
    emit sizeChanged();
}
