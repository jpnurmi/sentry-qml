#pragma once

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qstring.h>

enum class SentryHintAttachmentType
{
    File,
    Bytes
};

struct SentryHintAttachment
{
    SentryHintAttachmentType type = SentryHintAttachmentType::File;
    QString path;
    QByteArray bytes;
    QString filename;
    QString contentType;
};

struct SentryHintPrivate
{
    QList<SentryHintAttachment> attachments;
};
