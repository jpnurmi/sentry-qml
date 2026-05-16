#include <SentryQml/private/sentryscreenshot_p.h>

#include <QtCore/qbuffer.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qthread.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qimage.h>
#include <QtGui/qpixmap.h>
#include <QtGui/qscreen.h>
#include <QtGui/qwindow.h>
#include <QtQuick/qquickwindow.h>

namespace {

QWindow *preferredWindow()
{
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application) {
        return nullptr;
    }

    if (QWindow *window = application->focusWindow()) {
        if (window->isVisible()) {
            return window;
        }
    }

    for (QWindow *window : application->topLevelWindows()) {
        if (window && window->isVisible()) {
            return window;
        }
    }

    return application->topLevelWindows().value(0, nullptr);
}

QImage grabImage()
{
    QWindow *window = preferredWindow();
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
        return quickWindow->grabWindow();
    }

    QScreen *screen = window ? window->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return {};
    }

    const QPixmap pixmap = screen->grabWindow(window ? window->winId() : 0);
    return pixmap.toImage();
}

QByteArray imageToPng(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}

QByteArray grabPng()
{
    return imageToPng(grabImage());
}

} // namespace

namespace SentryScreenshot {

QByteArray toPng()
{
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application) {
        return {};
    }

    if (QThread::currentThread() == application->thread()) {
        return grabPng();
    }

    QByteArray bytes;
    const bool invoked = QMetaObject::invokeMethod(application,
                                                   [&bytes]
                                                   {
                                                       bytes = grabPng();
                                                   },
                                                   Qt::BlockingQueuedConnection);
    return invoked ? bytes : QByteArray {};
}

} // namespace SentryScreenshot
