#include "replayprivacy.h"

#include <SentryQml/sentryintegrationcontext.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qvariant.h>
#include <QtQuick/qquickitem.h>

#include <algorithm>

namespace {

bool inheritsClass(const QObject *object, const char *name)
{
    for (const QMetaObject *metaObject = object ? object->metaObject() : nullptr; metaObject;
         metaObject = metaObject->superClass()) {
        if (qstrcmp(metaObject->className(), name) == 0) {
            return true;
        }
    }
    return false;
}
bool hasClassPart(const QObject *object, const char *part)
{
    for (const QMetaObject *metaObject = object ? object->metaObject() : nullptr; metaObject;
         metaObject = metaObject->superClass()) {
        if (QByteArray(metaObject->className()).contains(part)) {
            return true;
        }
    }
    return false;
}

bool isText(const QQuickItem *item)
{
    return inheritsClass(item, "QQuickText") || inheritsClass(item, "QQuickTextInput")
        || inheritsClass(item, "QQuickTextEdit") || hasClassPart(item, "Label")
        || hasClassPart(item, "TextField") || hasClassPart(item, "TextArea");
}

bool isImage(const QQuickItem *item)
{
    return inheritsClass(item, "QQuickImageBase") || hasClassPart(item, "Image")
        || hasClassPart(item, "IconImage");
}

bool isPassword(const QQuickItem *item)
{
    if (!isText(item)) {
        return false;
    }
    const QVariant echoMode = item->property("echoMode");
    return echoMode.isValid() && echoMode.canConvert<int>() && echoMode.toInt() != 0;
}

bool marker(QQuickItem *item,
            const QByteArray &name,
            bool debug,
            SentryIntegrationContext *context,
            QSet<QString> *diagnostics)
{
    if (!item->dynamicPropertyNames().contains(name)) {
        return false;
    }
    const QVariant value = item->property(name.constData());
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    if (debug && context) {
        const QString key = QStringLiteral("%1:%2").arg(QString::fromLatin1(item->metaObject()->className()),
                                                        QString::fromLatin1(name));
        if (!diagnostics->contains(key)) {
            diagnostics->insert(key);
            context->reportWarning(
                QStringLiteral("Sentry Session Replay ignored non-boolean property '%1' on %2.")
                    .arg(QString::fromLatin1(name), QString::fromLatin1(item->metaObject()->className())));
        }
    }
    return false;
}

bool isKnownCustomRenderer(const QQuickItem *item)
{
    return hasClassPart(item, "ShaderEffect") || hasClassPart(item, "Canvas")
        || hasClassPart(item, "VideoOutput");
}

void appendMask(QVector<QRectF> *masks, const QRectF &rect)
{
    QRectF merged = rect.normalized();
    if (merged.isEmpty()) {
        return;
    }
    merged.adjust(-2.0, -2.0, 2.0, 2.0);
    for (qsizetype i = masks->size() - 1; i >= 0; --i) {
        const QRectF candidate = masks->at(i);
        if (candidate.intersects(merged) || candidate.adjusted(-1, -1, 1, 1).contains(merged.center())) {
            merged = merged.united(candidate);
            masks->removeAt(i);
        }
    }
    masks->append(merged);
}

void walk(QQuickItem *item,
          QQuickItem *root,
          const QRectF &ancestorClip,
          bool ancestorMasked,
          bool ancestorUnmasked,
          bool maskAllText,
          bool maskAllImages,
          bool debug,
          SentryIntegrationContext *context,
          QSet<QString> *diagnostics,
          QVector<QRectF> *masks)
{
    if (!item || !item->isVisible() || item->opacity() <= 0.0 || item->width() <= 0.0 || item->height() <= 0.0) {
        return;
    }

    const bool directMask = marker(item, QByteArrayLiteral("sentryReplayMask"), debug, context, diagnostics);
    const bool directUnmask = marker(item, QByteArrayLiteral("sentryReplayUnmask"), debug, context, diagnostics);
    const bool masked = ancestorMasked || directMask;
    const bool unmasked = !masked && (ancestorUnmasked || directUnmask);

    QRectF rect = item->mapRectToItem(root, QRectF(0.0, 0.0, item->width(), item->height()));
    if (!ancestorClip.isNull()) {
        rect = rect.intersected(ancestorClip);
    }

    const bool password = isPassword(item);
    if (masked || password || (!unmasked && ((maskAllText && isText(item)) || (maskAllImages && isImage(item))))) {
        appendMask(masks, rect);
    } else if (debug && isKnownCustomRenderer(item) && context) {
        const QString className = QString::fromLatin1(item->metaObject()->className());
        if (!diagnostics->contains(className)) {
            diagnostics->insert(className);
            context->reportWarning(
                QStringLiteral("Sentry Session Replay cannot classify pixels rendered by %1; mark sensitive content "
                               "with sentryReplayMask: true.")
                    .arg(className));
        }
    }

    QRectF childClip = ancestorClip;
    if (item->clip()) {
        const QRectF ownClip = item->mapRectToItem(root, QRectF(0.0, 0.0, item->width(), item->height()));
        childClip = childClip.isNull() ? ownClip : childClip.intersected(ownClip);
    }
    for (QQuickItem *child : item->childItems()) {
        walk(child,
             root,
             childClip,
             masked,
             unmasked,
             maskAllText,
             maskAllImages,
             debug,
             context,
             diagnostics,
             masks);
    }
}

} // namespace

QVector<QRectF> ReplayPrivacy::masks(QQuickItem *root,
                                     bool maskAllText,
                                     bool maskAllImages,
                                     bool debug,
                                     SentryIntegrationContext *context)
{
    QVector<QRectF> result;
    walk(root,
         root,
         QRectF(),
         false,
         false,
         maskAllText,
         maskAllImages,
         debug,
         context,
         &m_diagnostics,
         &result);
    return result;
}
