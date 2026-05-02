#pragma once

#include <SentryQml/sentryqmlglobal.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qobject.h>
#include <QtCore/qstring.h>
#include <QtQml/qqmlengine.h>

#include <memory>

struct SentryHintPrivate;

class SENTRYQML_EXPORT SentryHint : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SentryHint)

    Q_PROPERTY(int attachmentCount READ attachmentCount NOTIFY attachmentsChanged)

public:
    explicit SentryHint(QObject *parent = nullptr);
    ~SentryHint() override;

    int attachmentCount() const;

    Q_INVOKABLE bool attachFile(const QString &path,
                                const QString &contentType = QString(),
                                const QString &filename = QString());
    Q_INVOKABLE bool attachBytes(const QByteArray &bytes,
                                 const QString &filename,
                                 const QString &contentType = QString());
    Q_INVOKABLE void clearAttachments();

signals:
    void attachmentsChanged();

private:
    friend class SentryNativeSdk;

    std::unique_ptr<SentryHintPrivate> d;
};
