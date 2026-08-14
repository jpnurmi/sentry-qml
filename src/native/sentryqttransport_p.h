#pragma once

extern "C" {
#include <include/sentry.h>
}

#include <QtQml/qqmlnetworkaccessmanagerfactory.h>

sentry_transport_t *sentryQtTransportNew(QQmlNetworkAccessManagerFactory *factory);
