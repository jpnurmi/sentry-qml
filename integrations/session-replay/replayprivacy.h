#pragma once

#include <QtCore/qrect.h>
#include <QtCore/qset.h>
#include <QtCore/qvector.h>

class QQuickItem;
class SentryIntegrationContext;

class ReplayPrivacy
{
public:
    QVector<QRectF> masks(QQuickItem *root,
                          bool maskAllText,
                          bool maskAllImages,
                          bool debug,
                          SentryIntegrationContext *context);

private:
    QSet<QString> m_diagnostics;
};
