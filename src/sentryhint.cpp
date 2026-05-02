#include <SentryQml/sentryhint.h>

#include <SentryQml/private/sentryhint_p.h>

SentryHint::SentryHint(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentryHintPrivate>())
{
}

SentryHint::~SentryHint() = default;

int SentryHint::attachmentCount() const
{
    return static_cast<int>(d->attachments.size());
}

bool SentryHint::attachFile(const QString &path, const QString &contentType, const QString &filename)
{
    if (path.isEmpty()) {
        return false;
    }

    SentryHintAttachment attachment;
    attachment.type = SentryHintAttachmentType::File;
    attachment.path = path;
    attachment.filename = filename;
    attachment.contentType = contentType;
    d->attachments.append(attachment);
    emit attachmentsChanged();
    return true;
}

bool SentryHint::attachBytes(const QByteArray &bytes, const QString &filename, const QString &contentType)
{
    if (bytes.isEmpty() || filename.isEmpty()) {
        return false;
    }

    SentryHintAttachment attachment;
    attachment.type = SentryHintAttachmentType::Bytes;
    attachment.bytes = bytes;
    attachment.filename = filename;
    attachment.contentType = contentType;
    d->attachments.append(attachment);
    emit attachmentsChanged();
    return true;
}

void SentryHint::clearAttachments()
{
    if (d->attachments.isEmpty()) {
        return;
    }

    d->attachments.clear();
    emit attachmentsChanged();
}
