#include <SentryQml/sentryattachment.h>

#include <SentryQml/private/sentrysdk_p.h>

struct SentryAttachmentPrivate
{
    void *handle = nullptr;
    QString filename;
    QString contentType;
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
