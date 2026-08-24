#include "replayconfiguration.h"

#include <SentryQml/sentryintegrationcontext.h>

#include <QtCore/qmetatype.h>
#include <QtCore/qset.h>

#include <cmath>

namespace {

bool readNumber(const QVariantMap &values, const QString &key, double *result, QString *error)
{
    const QVariant value = values.value(key);
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        *error = QStringLiteral("'%1' must be a finite number").arg(key);
        return false;
    }
    *result = number;
    return true;
}

bool readInteger(const QVariantMap &values, const QString &key, qint64 *result, QString *error)
{
    double number = 0.0;
    if (!readNumber(values, key, &number, error)) {
        return false;
    }
    constexpr double maximumExactInteger = 9007199254740991.0;
    if (std::trunc(number) != number || number < -maximumExactInteger || number > maximumExactInteger) {
        *error = QStringLiteral("'%1' must be an integer").arg(key);
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

bool readBoolean(const QVariantMap &values, const QString &key, bool *result, QString *error)
{
    const QVariant value = values.value(key);
    if (value.metaType().id() != QMetaType::Bool) {
        *error = QStringLiteral("'%1' must be a boolean").arg(key);
        return false;
    }
    *result = value.toBool();
    return true;
}

} // namespace

bool ReplayConfiguration::parse(const QVariantMap &values,
                                SentryIntegrationContext *context,
                                ReplayConfiguration *configuration,
                                QString *error)
{
    ReplayConfiguration parsed;
    const QSet<QString> known = {
        QStringLiteral("crashSampleRate"), QStringLiteral("durationMs"),
        QStringLiteral("frameRate"),       QStringLiteral("maxWidth"),
        QStringLiteral("maxHeight"),       QStringLiteral("maskAllText"),
        QStringLiteral("maskAllImages"),   QStringLiteral("imageQuality"),
        QStringLiteral("maxSpoolBytes"),   QStringLiteral("maxPendingReplays"),
        QStringLiteral("debugArtifacts"),
    };

    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!known.contains(it.key()) && context) {
            context->reportWarning(
                QStringLiteral("Sentry Session Replay ignored unknown configuration key '%1'.").arg(it.key()));
        }
    }

    double number = 0.0;
    qint64 integer = 0;
    if (values.contains(QStringLiteral("crashSampleRate"))) {
        if (!readNumber(values, QStringLiteral("crashSampleRate"), &number, error) || number < 0.0 || number > 1.0) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'crashSampleRate' must be between 0.0 and 1.0");
            }
            return false;
        }
        parsed.crashSampleRate = number;
    }
    if (values.contains(QStringLiteral("durationMs"))) {
        if (!readInteger(values, QStringLiteral("durationMs"), &integer, error) || integer < 1000 || integer > 20000) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'durationMs' must be between 1000 and 20000");
            }
            return false;
        }
        parsed.durationMs = static_cast<int>(integer);
    }
    if (values.contains(QStringLiteral("frameRate"))) {
        if (!readInteger(values, QStringLiteral("frameRate"), &integer, error) || integer < 1 || integer > 2) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'frameRate' must be 1 or 2");
            }
            return false;
        }
        parsed.frameRate = static_cast<int>(integer);
    }
    if (values.contains(QStringLiteral("maxWidth"))) {
        if (!readInteger(values, QStringLiteral("maxWidth"), &integer, error) || integer < 2 || integer > 4096) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'maxWidth' must be between 2 and 4096");
            }
            return false;
        }
        parsed.maxWidth = static_cast<int>(integer) & ~1;
    }
    if (values.contains(QStringLiteral("maxHeight"))) {
        if (!readInteger(values, QStringLiteral("maxHeight"), &integer, error) || integer < 2 || integer > 4096) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'maxHeight' must be between 2 and 4096");
            }
            return false;
        }
        parsed.maxHeight = static_cast<int>(integer) & ~1;
    }
    if (values.contains(QStringLiteral("maskAllText"))
        && !readBoolean(values, QStringLiteral("maskAllText"), &parsed.maskAllText, error)) {
        return false;
    }
    if (values.contains(QStringLiteral("maskAllImages"))
        && !readBoolean(values, QStringLiteral("maskAllImages"), &parsed.maskAllImages, error)) {
        return false;
    }
    if (values.contains(QStringLiteral("imageQuality"))) {
        if (!readInteger(values, QStringLiteral("imageQuality"), &integer, error) || integer < 40 || integer > 90) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'imageQuality' must be between 40 and 90");
            }
            return false;
        }
        parsed.imageQuality = static_cast<int>(integer);
    }
    if (values.contains(QStringLiteral("maxSpoolBytes"))) {
        if (!readInteger(values, QStringLiteral("maxSpoolBytes"), &integer, error)
            || integer < 1024 * 1024 || integer > 256LL * 1024 * 1024) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'maxSpoolBytes' must be between 1 MiB and 256 MiB");
            }
            return false;
        }
        parsed.maxSpoolBytes = integer;
    }
    if (values.contains(QStringLiteral("maxPendingReplays"))) {
        if (!readInteger(values, QStringLiteral("maxPendingReplays"), &integer, error) || integer < 1 || integer > 8) {
            if (error->isEmpty()) {
                *error = QStringLiteral("'maxPendingReplays' must be between 1 and 8");
            }
            return false;
        }
        parsed.maxPendingReplays = static_cast<int>(integer);
    }
    if (values.contains(QStringLiteral("debugArtifacts"))
        && !readBoolean(values, QStringLiteral("debugArtifacts"), &parsed.debugArtifacts, error)) {
        return false;
    }

    *configuration = parsed;
    return true;
}
