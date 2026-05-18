#include <SentryQml/sentryspan.h>

#include <SentryQml/private/sentrysdk_p.h>

struct SentrySpanPrivate
{
    void *handle = nullptr;
    bool transaction = false;
    bool finished = false;
    QString name;
    QString operation;
    QString description;
    QString status;
    QVariantMap data;
    QVariantMap tags;
};

SentrySpan::SentrySpan(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentrySpanPrivate>())
{
}

SentrySpan::SentrySpan(void *handle,
                       bool transaction,
                       const QString &name,
                       const QString &operation,
                       const QString &description,
                       QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SentrySpanPrivate>())
{
    d->handle = handle;
    d->transaction = transaction;
    d->name = name;
    d->operation = operation;
    d->description = description;
}

SentrySpan::~SentrySpan()
{
    SentrySdk::instance()->detachSpan(this);
}

bool SentrySpan::isValid() const
{
    return d->handle != nullptr;
}

bool SentrySpan::isTransaction() const
{
    return d->transaction;
}

bool SentrySpan::isFinished() const
{
    return d->finished;
}

QString SentrySpan::name() const
{
    return d->name;
}

QString SentrySpan::operation() const
{
    return d->operation;
}

QString SentrySpan::description() const
{
    return d->description;
}

QString SentrySpan::status() const
{
    return d->status;
}

void SentrySpan::setStatus(const QString &status)
{
    if (d->status == status) {
        return;
    }

    if (SentrySdk::instance()->setSpanStatus(this, status)) {
        setStatusLocally(status);
    }
}

QVariantMap SentrySpan::data() const
{
    return d->data;
}

QVariantMap SentrySpan::tags() const
{
    return d->tags;
}

QVariantMap SentrySpan::traceHeaders() const
{
    return SentrySdk::instance()->spanTraceHeaders(this);
}

bool SentrySpan::setData(const QString &key, const QVariant &value)
{
    if (key.isEmpty()) {
        return false;
    }

    if (!SentrySdk::instance()->setSpanData(this, key, value)) {
        return false;
    }

    setDataLocally(key, value);
    return true;
}

bool SentrySpan::removeData(const QString &key)
{
    if (key.isEmpty()) {
        return false;
    }

    if (!SentrySdk::instance()->removeSpanData(this, key)) {
        return false;
    }

    removeDataLocally(key);
    return true;
}

bool SentrySpan::setTag(const QString &key, const QString &value)
{
    if (key.isEmpty()) {
        return false;
    }

    if (!SentrySdk::instance()->setSpanTag(this, key, value)) {
        return false;
    }

    setTagLocally(key, value);
    return true;
}

bool SentrySpan::removeTag(const QString &key)
{
    if (key.isEmpty()) {
        return false;
    }

    if (!SentrySdk::instance()->removeSpanTag(this, key)) {
        return false;
    }

    removeTagLocally(key);
    return true;
}

bool SentrySpan::finish(const QString &status)
{
    return SentrySdk::instance()->finishSpan(this, status);
}

QVariantMap SentrySpan::toVariantMap() const
{
    QVariantMap span;
    span.insert(QStringLiteral("name"), d->name);
    span.insert(QStringLiteral("op"), d->operation);
    span.insert(QStringLiteral("operation"), d->operation);
    span.insert(QStringLiteral("description"), d->description);
    span.insert(QStringLiteral("status"), d->status);
    span.insert(QStringLiteral("data"), d->data);
    span.insert(QStringLiteral("tags"), d->tags);
    span.insert(QStringLiteral("transaction"), d->transaction);
    return span;
}

void *SentrySpan::handle() const
{
    return d->handle;
}

void SentrySpan::invalidate()
{
    if (!d->handle && d->finished) {
        return;
    }

    d->handle = nullptr;
    d->finished = true;
    emit validChanged();
}

void SentrySpan::setStatusLocally(const QString &status)
{
    if (d->status == status) {
        return;
    }

    d->status = status;
    emit statusChanged();
}

void SentrySpan::setDataLocally(const QString &key, const QVariant &value)
{
    if (d->data.value(key) == value) {
        return;
    }

    d->data.insert(key, value);
    emit dataChanged();
}

void SentrySpan::removeDataLocally(const QString &key)
{
    if (!d->data.contains(key)) {
        return;
    }

    d->data.remove(key);
    emit dataChanged();
}

void SentrySpan::setTagLocally(const QString &key, const QString &value)
{
    if (d->tags.value(key).toString() == value) {
        return;
    }

    d->tags.insert(key, value);
    emit tagsChanged();
}

void SentrySpan::removeTagLocally(const QString &key)
{
    if (!d->tags.contains(key)) {
        return;
    }

    d->tags.remove(key);
    emit tagsChanged();
}
