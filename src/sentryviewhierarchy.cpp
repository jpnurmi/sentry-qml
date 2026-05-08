#include <SentryQml/private/sentryviewhierarchy_p.h>

#include <QtCore/qcoreapplication.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qthread.h>
#include <QtGui/qguiapplication.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickwindow.h>

namespace {

QJsonObject viewObject(const QQuickItem *item)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QString::fromLatin1(item->metaObject()->className()));

    const QString identifier = item->objectName();
    if (!identifier.isEmpty()) {
        object.insert(QStringLiteral("identifier"), identifier);
    }

    object.insert(QStringLiteral("width"), item->width());
    object.insert(QStringLiteral("height"), item->height());
    object.insert(QStringLiteral("x"), item->x());
    object.insert(QStringLiteral("y"), item->y());
    object.insert(QStringLiteral("alpha"), item->opacity());
    object.insert(QStringLiteral("visible"), item->isVisible());

    QJsonArray children;
    const QList<QQuickItem *> childItems = item->childItems();
    for (const QQuickItem *child : childItems) {
        if (child) {
            children.append(viewObject(child));
        }
    }
    object.insert(QStringLiteral("children"), children);

    return object;
}

QJsonObject windowObject(const QQuickWindow *window)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QString::fromLatin1(window->metaObject()->className()));

    const QString identifier = window->objectName();
    if (!identifier.isEmpty()) {
        object.insert(QStringLiteral("identifier"), identifier);
    }

    object.insert(QStringLiteral("width"), window->width());
    object.insert(QStringLiteral("height"), window->height());
    object.insert(QStringLiteral("x"), window->x());
    object.insert(QStringLiteral("y"), window->y());
    object.insert(QStringLiteral("alpha"), window->opacity());
    object.insert(QStringLiteral("visible"), window->isVisible());

    QJsonArray children;
    if (const QQuickItem *contentItem = window->contentItem()) {
        children.append(viewObject(contentItem));
    }
    object.insert(QStringLiteral("children"), children);

    return object;
}

} // namespace

QByteArray SentryViewHierarchy::toJson()
{
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application || QThread::currentThread() != application->thread()) {
        return {};
    }

    QJsonArray windows;
    const QList<QWindow *> applicationWindows = QGuiApplication::allWindows();
    for (const QWindow *window : applicationWindows) {
        if (const auto *quickWindow = qobject_cast<const QQuickWindow *>(window)) {
            windows.append(windowObject(quickWindow));
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("rendering_system"), QStringLiteral("Qt Quick"));
    root.insert(QStringLiteral("windows"), windows);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
