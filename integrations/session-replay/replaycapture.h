#pragma once

#include "replayconfiguration.h"
#include "replayprivacy.h"

#include <QtCore/qobject.h>
#include <QtCore/qpointer.h>
#include <QtCore/qsharedpointer.h>
#include <QtCore/qtimer.h>
#include <QtGui/qimage.h>

#include <functional>

class QQuickItem;
class QQuickItemGrabResult;
class QQuickWindow;
class SentryIntegrationContext;

class ReplayCapture final : public QObject
{
    Q_OBJECT

public:
    using FrameHandler = std::function<void(QImage, qint64)>;

    ReplayCapture(const ReplayConfiguration &configuration,
                  SentryIntegrationContext *context,
                  FrameHandler handler,
                  QObject *parent = nullptr);

    void start();
    void stop();

private slots:
    void capture();

private:
    static QQuickWindow *activeWindow();
    static QVector<QRectF> normalized(const QVector<QRectF> &rects, const QSizeF &size);
    void frameReady(quint64 generation,
                    QQuickWindow *window,
                    QQuickItem *root,
                    const QVector<QRectF> &requestMasks);

    ReplayConfiguration m_configuration;
    QPointer<SentryIntegrationContext> m_context;
    FrameHandler m_handler;
    ReplayPrivacy m_privacy;
    QTimer m_timer;
    QPointer<QQuickWindow> m_window;
    QSharedPointer<QQuickItemGrabResult> m_result;
    QMetaObject::Connection m_readyConnection;
    quint64 m_generation = 0;
    bool m_busy = false;
};
