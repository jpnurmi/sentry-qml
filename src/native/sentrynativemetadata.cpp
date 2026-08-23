#define SENTRY_BOOT_H_INCLUDED

#include <cinttypes>

extern "C"
{
#include <include/sentry.h>
}

#include "sentrynativemetadata_p.h"

extern "C"
{
#include "sentry_alloc.h"
#include "sentry_integration.h"
#include "sentry_options.h"
#include "sentry_scope.h"
#include "sentry_value.h"
}

#include <QtCore/qbytearray.h>
#include <QtCore/qset.h>

#include <cstring>

namespace {

void freeName(void *data)
{
    sentry_free(data);
}

sentry_value_t retainedValue(sentry_value_t object, const char *key)
{
    sentry_value_t value = sentry_value_get_by_key(object, key);
    sentry_value_incref(value);
    return value;
}

} // namespace

bool sentryQmlAddIntegrationNames(sentry_options_t *options, const QStringList &names)
{
    for (const QString &name : names) {
        const QByteArray utf8 = name.toUtf8();
        char *storedName = static_cast<char *>(sentry_malloc(static_cast<size_t>(utf8.size()) + 1));
        sentry_integration_t *integration = SENTRY_MAKE(sentry_integration_t);
        if (!storedName || !integration) {
            sentry_free(storedName);
            sentry_free(integration);
            return false;
        }

        std::memcpy(storedName, utf8.constData(), static_cast<size_t>(utf8.size()));
        storedName[utf8.size()] = '\0';
        integration->name = storedName;
        integration->data = storedName;
        integration->free_func = freeName;
        sentry__options_add_integration(options, integration);
    }
    return true;
}

void sentryQmlReplaceIntegrationNames(const QStringList &preparedNames, const QStringList &activeNames)
{
    const QSet<QString> prepared(preparedNames.cbegin(), preparedNames.cend());
    SENTRY_WITH_SCOPE_MUT(scope)
    {
        const sentry_value_t oldSdk = scope->client_sdk;
        sentry_value_t sdk = sentry_value_new_object();
        sentry_value_set_by_key(sdk, "name", retainedValue(oldSdk, "name"));
        sentry_value_set_by_key(sdk, "version", retainedValue(oldSdk, "version"));

        sentry_value_t integrations = sentry_value_new_list();
        const sentry_value_t oldIntegrations = sentry_value_get_by_key(oldSdk, "integrations");
        const size_t count = sentry_value_get_length(oldIntegrations);
        for (size_t index = 0; index < count; ++index) {
            const sentry_value_t value = sentry_value_get_by_index(oldIntegrations, index);
            const char *name = sentry_value_as_string(value);
            if (!name || prepared.contains(QString::fromUtf8(name))) {
                continue;
            }
            sentry_value_incref(value);
            sentry_value_append(integrations, value);
        }
        for (const QString &name : activeNames) {
            const QByteArray utf8 = name.toUtf8();
            sentry_value_append(integrations,
                                sentry_value_new_string_n(utf8.constData(), static_cast<size_t>(utf8.size())));
        }
        sentry_value_set_by_key(sdk, "integrations", integrations);

        const sentry_value_t packages = sentry_value_get_by_key(oldSdk, "packages");
        if (!sentry_value_is_null(packages)) {
            sentry_value_incref(packages);
            sentry_value_set_by_key(sdk, "packages", packages);
        }
        sentry_value_freeze(sdk);
        scope->client_sdk = sdk;
        sentry_value_decref(oldSdk);
    }
}
