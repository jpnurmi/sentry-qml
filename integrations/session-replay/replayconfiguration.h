#pragma once

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>

class SentryIntegrationContext;

struct ReplayConfiguration
{
    double crashSampleRate = 0.0;
    int durationMs = 5000;
    int frameRate = 1;
    int maxWidth = 1280;
    int maxHeight = 720;
    bool maskAllText = true;
    bool maskAllImages = true;
    int imageQuality = 70;
    qint64 maxSpoolBytes = 32 * 1024 * 1024;
    int maxPendingReplays = 2;
    bool debugArtifacts = false;

    static bool parse(const QVariantMap &values,
                      SentryIntegrationContext *context,
                      ReplayConfiguration *configuration,
                      QString *error);
};
