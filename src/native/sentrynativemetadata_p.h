#pragma once

#include <QtCore/qstringlist.h>

struct sentry_options_s;
typedef struct sentry_options_s sentry_options_t;

bool sentryQmlAddIntegrationNames(sentry_options_t *options, const QStringList &names);
void sentryQmlReplaceIntegrationNames(const QStringList &preparedNames, const QStringList &activeNames);
