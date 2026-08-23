#include "replaycapture.h"

#include <SentryQml/sentryintegrationcontext.h>

#include <QtCore/qdatetime.h>
#include <QtGui/qguiapplication.h>
#include <QtGui/qpainter.h>
#include <QtGui/qwindow.h>
#include <QtQuick/qquickitem.h>
#include <QtQuick/qquickitemgrabresult.h>
#include <QtQuick/qquickwindow.h>

#include <utility>

namespace {

bool eligible(QQuickWindow *window)
{
    if (!window || !window->isVisible() || window->visibility() == QWindow::Minimized || window->width() <= 0
        || window->height() <= 0 || window->transientParent()) {
        return false;
    }
    const Qt::WindowType type =
        static_cast<Qt::WindowType>((window->flags() & Qt::WindowType_Mask).toInt());
    return type != Qt::Tool && type != Qt::ToolTip && type != Qt::Popup && type != Qt::SplashScreen;
}

} // namespace

ReplayCapture::ReplayCapture(const ReplayConfiguration &configuration,
                             SentryIntegrationContext *context,
                             FrameHandler handler,
                             QObject *parent)
    : QObject(parent)
    , m_configuration(configuration)
    , m_context(context)
    , m_handler(std::move(handler))
{
    m_timer.setTimerType(Qt::CoarseTimer);
    m_timer.setInterval(1000 / m_configuration.frameRate);
    connect(&m_timer, &QTimer::timeout, this, &ReplayCapture::capture);
}

void ReplayCapture::start()
{
    if (m_timer.isActive()) {
        return;
    }
    m_timer.start();
    QTimer::singleShot(0, this, &ReplayCapture::capture);
}

void ReplayCapture::stop()
{
    m_timer.stop();
    ++m_generation;
    QObject::disconnect(m_readyConnection);
    m_readyConnection = {};
    m_result.reset();
    m_window = nullptr;
    m_busy = false;
}

QQuickWindow *ReplayCapture::activeWindow()
{
    if (auto *focused = qobject_cast<QQuickWindow *>(QGuiApplication::focusWindow()); eligible(focused)) {
        return focused;
    }
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (auto *quickWindow = qobject_cast<QQuickWindow *>(window); eligible(quickWindow)) {
            return quickWindow;
        }
    }
    return nullptr;
}

QVector<QRectF> ReplayCapture::normalized(const QVector<QRectF> &rects, const QSizeF &size)
{
    QVector<QRectF> result;
    if (size.width() <= 0.0 || size.height() <= 0.0) {
        return result;
    }
    result.reserve(rects.size());
    for (const QRectF &rect : rects) {
        result.append(QRectF(rect.x() / size.width(),
                             rect.y() / size.height(),
                             rect.width() / size.width(),
                             rect.height() / size.height()));
    }
    return result;
}

void ReplayCapture::capture()
{
    if (m_busy) {
        return;
    }
    QQuickWindow *window = activeWindow();
    QQuickItem *root = window ? window->contentItem() : nullptr;
    if (!root || root->width() <= 0.0 || root->height() <= 0.0 || !window->isSceneGraphInitialized()) {
        return;
    }

    const QSizeF sourceSize(root->width(), root->height());
    QSize target = sourceSize.toSize().scaled(
        QSize(m_configuration.maxWidth, m_configuration.maxHeight), Qt::KeepAspectRatio);
    target.setWidth(qMax(2, target.width() & ~1));
    target.setHeight(qMax(2, target.height() & ~1));

    const QVector<QRectF> masks = normalized(
        m_privacy.masks(root,
                        m_configuration.maskAllText,
                        m_configuration.maskAllImages,
                        m_configuration.debugArtifacts,
                        m_context),
        sourceSize);
    const quint64 generation = ++m_generation;
    m_result = root->grabToImage(target);
    if (!m_result) {
        return;
    }

    m_busy = true;
    m_window = window;
    m_readyConnection = connect(m_result.get(), &QQuickItemGrabResult::ready, this,
                                [this, generation, window = QPointer<QQuickWindow>(window),
                                 root = QPointer<QQuickItem>(root), masks]() {
                                    frameReady(generation, window, root, masks);
                                });
}

void ReplayCapture::frameReady(quint64 generation,
                               QQuickWindow *window,
                               QQuickItem *root,
                               const QVector<QRectF> &requestMasks)
{
    QObject::disconnect(m_readyConnection);
    m_readyConnection = {};
    const QSharedPointer<QQuickItemGrabResult> result =
        std::exchange(m_result, QSharedPointer<QQuickItemGrabResult>());
    m_busy = false;
    if (generation != m_generation || !window || !root || window != m_window || !result) {
        return;
    }

    QVector<QRectF> masks = requestMasks;
    const QSizeF completionSize(root->width(), root->height());
    masks += normalized(m_privacy.masks(root,
                                         m_configuration.maskAllText,
                                         m_configuration.maskAllImages,
                                         m_configuration.debugArtifacts,
                                         m_context),
                        completionSize);

    QImage captured = result->image().convertToFormat(QImage::Format_RGB32);
    if (captured.isNull()) {
        return;
    }
    {
        QPainter painter(&captured);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (const QRectF &mask : masks) {
            const QRectF pixels(mask.x() * captured.width(),
                                mask.y() * captured.height(),
                                mask.width() * captured.width(),
                                mask.height() * captured.height());
            painter.drawRect(pixels.intersected(captured.rect()));
        }
    }

    QImage canvas(QSize(m_configuration.maxWidth, m_configuration.maxHeight), QImage::Format_RGB32);
    canvas.fill(Qt::black);
    const QPoint topLeft((canvas.width() - captured.width()) / 2, (canvas.height() - captured.height()) / 2);
    {
        QPainter painter(&canvas);
        painter.drawImage(topLeft, captured);
    }
    captured.fill(Qt::black);
    m_handler(std::move(canvas), QDateTime::currentMSecsSinceEpoch());
}
