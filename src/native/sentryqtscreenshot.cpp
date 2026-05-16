extern "C" {
#include <sentry_screenshot.h>
}

#include <QtCore/qcoreapplication.h>
#include <QtCore/qfile.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qthread.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpixmap.h>
#include <QtGui/qscreen.h>

#include <memory>

namespace {

QString pathToString(const sentry_path_t *path)
{
    if (!path) {
        return {};
    }

#if defined(Q_OS_WIN)
    if (path->path_w) {
        return QString::fromWCharArray(path->path_w);
    }
#endif

    return path->path ? QFile::decodeName(path->path) : QString {};
}

QGuiApplication *ensureGuiApplication(std::unique_ptr<QGuiApplication> *ownedApplication)
{
    QCoreApplication *application = QCoreApplication::instance();
    if (auto *guiApplication = qobject_cast<QGuiApplication *>(application)) {
        return guiApplication;
    }

    if (application) {
        return nullptr;
    }

    static int argc = 1;
    static char appName[] = "sentry-screenshot";
    static char *argv[] = {appName, nullptr};
    ownedApplication->reset(new QGuiApplication(argc, argv));
    return ownedApplication->get();
}

bool grabPrimaryScreen(const QString &path)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return false;
    }

    const QPixmap screenshot = screen->grabWindow(0);
    if (screenshot.isNull()) {
        return false;
    }

    return screenshot.save(path, "PNG");
}

} // namespace

extern "C" bool sentry__screenshot_capture(const sentry_path_t *path, uint32_t)
{
    const QString screenshotPath = pathToString(path);
    if (screenshotPath.isEmpty()) {
        return false;
    }

    std::unique_ptr<QGuiApplication> ownedApplication;
    QGuiApplication *application = ensureGuiApplication(&ownedApplication);
    if (!application) {
        return false;
    }

    if (QThread::currentThread() == application->thread()) {
        return grabPrimaryScreen(screenshotPath);
    }

    bool captured = false;
    const bool invoked = QMetaObject::invokeMethod(application,
                                                   [&captured, &screenshotPath]
                                                   {
                                                       captured = grabPrimaryScreen(screenshotPath);
                                                   },
                                                   Qt::BlockingQueuedConnection);
    return invoked && captured;
}
