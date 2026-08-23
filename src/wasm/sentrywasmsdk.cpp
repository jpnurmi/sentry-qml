#include <SentryQml/private/sentryintegrationmanager_p.h>
#include <SentryQml/private/sentrysdk_p.h>

#include <SentryQml/private/sentryevent_p.h>
#include <SentryQml/private/sentryhint_p.h>
#include <SentryQml/private/sentryscreenshot_p.h>
#include <SentryQml/private/sentryviewhierarchy_p.h>

#include <SentryQml/sentry.h>
#include <SentryQml/sentryattachment.h>
#include <SentryQml/sentryhint.h>
#include <SentryQml/sentryoptions.h>
#include <SentryQml/sentryspan.h>
#include <SentryQml/sentryuser.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qfile.h>
#include <QtCore/qfileinfo.h>
#include <QtCore/qjsonarray.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qjsonobject.h>
#include <QtCore/qjsonvalue.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qpointer.h>
#include <QtCore/qscopedvaluerollback.h>
#include <QtCore/qthread.h>
#include <QtCore/quuid.h>
#include <QtQml/qjsengine.h>
#include <QtQml/qjsvalue.h>
#include <QtQml/qqmlinfo.h>

#include <emscripten/emscripten.h>

#include <cmath>
#include <cstdlib>

#ifndef SENTRY_QML_SDK_NAME
#    define SENTRY_QML_SDK_NAME "sentry.javascript.qml"
#endif

struct SentrySdkEventHookState
{
    QPointer<Sentry> sentry;
    QPointer<QJSEngine> engine;
    QJSValue callback;
    QThread *thread = nullptr;
    QString propertyName;
};

struct SentrySdkCrashHookState
{
};

struct SentrySdkAttachmentState
{
    enum Type
    {
        File,
        Bytes
    };

    Type type = File;
    QString path;
    QByteArray bytes;
    QString filename;
    QString contentType;
    QString attachmentType;
};

struct SentrySdkSpanState
{
    int handle = 0;
    bool transaction = false;
};

EM_JS(void, sentry_qml_wasm_ensure_bridge, (), {
    var root = typeof globalThis !== "undefined" ? globalThis : this;
    if (root.__sentryQmlWasmBridge) {
        return;
    }

    function sentry() {
        if (root.SentryQmlSentry) {
            return root.SentryQmlSentry;
        }
        if (root.Sentry) {
            return root.Sentry;
        }
        return root.SentryQml && root.SentryQml.Sentry ? root.SentryQml.Sentry : null;
    }

    function parseJson(json, fallback) {
        if (!json) {
            return fallback;
        }
        try {
            return JSON.parse(json);
        } catch (error) {
            console.error("Sentry QML: Failed to parse bridge JSON.", error);
            return fallback;
        }
    }

    function scope() {
        var S = sentry();
        return S && S.getCurrentScope ? S.getCurrentScope() : null;
    }

    function globalScope() {
        var S = sentry();
        return S && S.getGlobalScope ? S.getGlobalScope() : scope();
    }

    function configureScope(callback) {
        var S = sentry();
        if (!S) {
            return false;
        }
        if (S.configureScope) {
            S.configureScope(callback);
            return true;
        }
        var current = scope();
        if (!current) {
            return false;
        }
        callback(current);
        return true;
    }

    function base64ToBytes(value) {
        var binary = root.atob(value);
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; ++i) {
            bytes[i] = binary.charCodeAt(i);
        }
        return bytes;
    }

    function fileName(path) {
        if (!path) {
            return "attachment";
        }
        var index = Math.max(path.lastIndexOf("/"), path.lastIndexOf(String.fromCharCode(92)));
        return index < 0 ? path : path.slice(index + 1);
    }

    function attachmentFromJson(attachment) {
        var bytes = null;
        if (attachment.bytes) {
            bytes = base64ToBytes(attachment.bytes);
        } else if (attachment.path) {
            var fs = root.FS || (typeof FS !== "undefined" ? FS : null);
            if (fs && fs.readFile) {
                try {
                    bytes = fs.readFile(attachment.path);
                } catch (error) {
                    console.debug("Sentry QML: Skipping unreadable attachment.", attachment.path, error);
                }
            }
        }
        if (!bytes) {
            return null;
        }

        var result = {
            data: bytes,
            filename: attachment.filename || fileName(attachment.path),
        };
        if (attachment.contentType) {
            result.contentType = attachment.contentType;
        }
        if (attachment.attachmentType) {
            result.attachmentType = attachment.attachmentType;
        }
        return result;
    }

    function attachmentsFromJson(json) {
        var input = parseJson(json, []);
        var attachments = [];
        for (var i = 0; i < input.length; ++i) {
            var attachment = attachmentFromJson(input[i]);
            if (attachment) {
                attachments.push(attachment);
            }
        }
        return attachments;
    }

    function logMethod(level) {
        switch (level) {
        case -2:
            return "trace";
        case -1:
            return "debug";
        case 1:
            return "warn";
        case 2:
            return "error";
        case 3:
            return "fatal";
        case 0:
        default:
            return "info";
        }
    }

    function spanStatus(status) {
        if (!status) {
            return undefined;
        }
        return status;
    }

    function bindSpan(span) {
        var current = scope();
        if (current && current.setSpan) {
            current.setSpan(span);
            return true;
        }
        if (current && current.setActiveSpan) {
            current.setActiveSpan(span);
            return true;
        }
        return false;
    }

    function setSpanData(span, key, value) {
        if (span.setData) {
            span.setData(key, value);
            return true;
        }
        if (span.setAttribute) {
            span.setAttribute(key, value);
            return true;
        }
        if (span.setAttributes) {
            var attributes = {};
            attributes[key] = value;
            span.setAttributes(attributes);
            return true;
        }
        return false;
    }

    function setSpanStatus(span, status) {
        if (!status) {
            return true;
        }
        if (!span.setStatus) {
            return false;
        }
        try {
            span.setStatus(status);
        } catch (error) {
            span.setStatus({ code: status === "ok" ? 1 : 2, message: status });
        }
        return true;
    }

    function finishSpan(span) {
        if (span.end) {
            span.end();
            return true;
        }
        if (span.finish) {
            span.finish();
            return true;
        }
        return false;
    }

    function spanTraceHeaders(span) {
        var headers = {};
        if (!span) {
            return headers;
        }
        if (span.toSentryTrace) {
            headers["sentry-trace"] = span.toSentryTrace();
        } else {
            var S = sentry();
            if (S && S.spanToTraceHeader) {
                headers["sentry-trace"] = S.spanToTraceHeader(span);
            } else if (span.toTraceparent) {
                headers["sentry-trace"] = span.toTraceparent();
            }
        }
        if (span.toBaggageHeader) {
            headers.baggage = span.toBaggageHeader();
        } else {
            var SDK = sentry();
            if (SDK && SDK.spanToBaggageHeader) {
                headers.baggage = SDK.spanToBaggageHeader(span);
            }
        }
        Object.keys(headers).forEach(function (key) {
            if (!headers[key]) {
                delete headers[key];
            }
        });
        return headers;
    }

    root.__sentryQmlWasmBridge = {
        nextSpanHandle: 1,
        requireUserConsent: false,
        pending: Promise.resolve(true),
        spans: {},
        userConsent: -1,

        enqueue: function (operation) {
            this.pending = this.pending.catch(function () {
                return true;
            }).then(operation).catch(function (error) {
                console.error("Sentry QML: asynchronous bridge operation failed.", error);
                return false;
            });
            return true;
        },

        hasConsent: function () {
            return !this.requireUserConsent || this.userConsent === 1;
        },

        init: function (optionsJson, sdkName) {
            var S = sentry();
            if (!S || !S.init) {
                console.error("Sentry QML: Sentry JavaScript SDK is not available as globalThis.Sentry.");
                return false;
            }

            var options = parseJson(optionsJson, {});
            this.requireUserConsent = !!options.requireUserConsent;
            this.userConsent = -1;

            var sentryOptions = {
                beforeSend: function (event) {
                    return root.__sentryQmlWasmBridge.hasConsent() ? event : null;
                },
                _metadata: {
                    sdk: {
                        name: sdkName || "sentry.javascript.qml",
                        integrations: Array.isArray(options.integrations)
                            ? options.integrations.slice()
                            : [],
                    },
                },
            };

            if (options.dsn) {
                sentryOptions.dsn = options.dsn;
            }
            if (options.release) {
                sentryOptions.release = options.release;
            }
            if (options.environment) {
                sentryOptions.environment = options.environment;
            }
            if (options.dist) {
                sentryOptions.dist = options.dist;
            }
            if (typeof options.debug === "boolean") {
                sentryOptions.debug = options.debug;
            }
            if (typeof options.sendClientReports === "boolean") {
                sentryOptions.sendClientReports = options.sendClientReports;
            }
            if (typeof options.autoSessionTracking === "boolean") {
                sentryOptions.autoSessionTracking = options.autoSessionTracking;
            }
            if (typeof options.sampleRate === "number") {
                sentryOptions.sampleRate = options.sampleRate;
            }
            if (typeof options.tracesSampleRate === "number" && options.tracesSampleRate >= 0) {
                sentryOptions.tracesSampleRate = options.tracesSampleRate;
            }
            if (Array.isArray(options.tracePropagationTargets)) {
                sentryOptions.tracePropagationTargets = options.tracePropagationTargets;
            }
            if (options.orgId) {
                sentryOptions.orgId = options.orgId;
            }
            if (typeof options.strictTraceContinuation === "boolean") {
                sentryOptions.strictTraceContinuation = options.strictTraceContinuation;
            }
            if (typeof options.maxBreadcrumbs === "number") {
                sentryOptions.maxBreadcrumbs = options.maxBreadcrumbs;
            }

            if (S.wasmIntegration) {
                sentryOptions.integrations = function (integrations) {
                    var result = integrations ? integrations.slice() : [];
                    result.push(S.wasmIntegration());
                    return result;
                };
            }

            S.init(sentryOptions);
            if (options.user) {
                this.setUser(JSON.stringify(options.user));
            }
            return true;
        },

        flush: function (timeoutMs) {
            var S = sentry();
            if (S && S.flush) {
                return this.enqueue(function () {
                    return S.flush(timeoutMs);
                });
            }
            return true;
        },

        close: function (timeoutMs) {
            var S = sentry();
            this.spans = {};
            if (S && S.close) {
                return this.enqueue(function () {
                    return S.close(timeoutMs);
                });
            }
            return true;
        },

        setIntegrations: function (integrationsJson) {
            var names = parseJson(integrationsJson, []);
            if (!Array.isArray(names)) {
                return false;
            }
            var S = sentry();
            var client = S && S.getClient ? S.getClient() : null;
            var options = client && client.getOptions ? client.getOptions() : null;
            if (!options) {
                return false;
            }
            options._metadata = options._metadata || {};
            options._metadata.sdk = options._metadata.sdk || {};
            options._metadata.sdk.integrations = names.slice();
            return true;
        },

        setUserConsent: function (required, consent) {
            this.requireUserConsent = !!required;
            this.userConsent = consent;
            return true;
        },

        setRelease: function (release) {
            var S = sentry();
            var client = S && S.getClient ? S.getClient() : null;
            var options = client && client.getOptions ? client.getOptions() : null;
            if (options) {
                options.release = release || undefined;
            }
            return true;
        },

        setEnvironment: function (environment) {
            var S = sentry();
            var client = S && S.getClient ? S.getClient() : null;
            var options = client && client.getOptions ? client.getOptions() : null;
            if (options) {
                options.environment = environment || undefined;
            }
            return true;
        },

        setLevel: function (level) {
            var S = sentry();
            if (!S || !S.getCurrentScope) {
                return false;
            }
            var names = {
                "-2": "trace",
                "-1": "debug",
                "0": "info",
                "1": "warning",
                "2": "error",
                "3": "fatal"
            };
            var name = names[String(level)] || "info";
            var scope = S.getCurrentScope();
            if (scope && scope.setLevel) {
                scope.setLevel(name);
                return true;
            }
            return false;
        },

        setTransaction: function (transaction) {
            var S = sentry();
            if (!S || !S.getCurrentScope) {
                return false;
            }
            var scope = S.getCurrentScope();
            if (scope && scope.setTransactionName) {
                scope.setTransactionName(transaction);
                return true;
            }
            if (scope && scope.setTransaction) {
                scope.setTransaction(transaction);
                return true;
            }
            return false;
        },

        setUser: function (userJson) {
            var S = sentry();
            if (!S || !S.setUser) {
                return false;
            }
            S.setUser(parseJson(userJson, {}));
            return true;
        },

        removeUser: function () {
            var S = sentry();
            if (!S || !S.setUser) {
                return false;
            }
            S.setUser(null);
            return true;
        },

        setTag: function (key, value) {
            var S = sentry();
            if (!S || !S.setTag) {
                return false;
            }
            S.setTag(key, value);
            return true;
        },

        removeTag: function (key) {
            var S = sentry();
            if (!S) {
                return false;
            }
            if (S.setTag) {
                S.setTag(key, undefined);
                return true;
            }
            return false;
        },

        setContext: function (key, contextJson) {
            var S = sentry();
            if (!S || !S.setContext) {
                return false;
            }
            S.setContext(key, parseJson(contextJson, {}));
            return true;
        },

        removeContext: function (key) {
            var S = sentry();
            if (!S || !S.setContext) {
                return false;
            }
            S.setContext(key, null);
            return true;
        },

        setAttribute: function (key, valueJson) {
            var target = globalScope();
            if (!target || !target.setAttribute) {
                return false;
            }
            target.setAttribute(key, parseJson(valueJson, null));
            return true;
        },

        removeAttribute: function (key) {
            var target = globalScope();
            if (!target || !target.removeAttribute) {
                return false;
            }
            target.removeAttribute(key);
            return true;
        },

        setFingerprint: function (fingerprintJson) {
            var fingerprint = parseJson(fingerprintJson, []);
            return configureScope(function (current) {
                if (current.setFingerprint) {
                    current.setFingerprint(fingerprint);
                }
            });
        },

        setAttachments: function (attachmentsJson) {
            var attachments = attachmentsFromJson(attachmentsJson);
            return configureScope(function (current) {
                if (current.clearAttachments) {
                    current.clearAttachments();
                }
                if (current.addAttachment) {
                    for (var i = 0; i < attachments.length; ++i) {
                        current.addAttachment(attachments[i]);
                    }
                }
            });
        },

        startSession: function () {
            var S = sentry();
            if (S && S.startSession) {
                S.startSession();
            }
            return true;
        },

        endSession: function () {
            var S = sentry();
            if (S && S.endSession) {
                S.endSession();
            }
            return true;
        },

        addBreadcrumb: function (breadcrumbJson) {
            var S = sentry();
            if (!S || !S.addBreadcrumb) {
                return false;
            }
            S.addBreadcrumb(parseJson(breadcrumbJson, {}));
            return true;
        },

        log: function (level, message, attributesJson) {
            var S = sentry();
            var logger = S && S.logger ? S.logger : null;
            var method = logMethod(level);
            if (!logger || !logger[method]) {
                return false;
            }
            logger[method](message, parseJson(attributesJson, {}));
            return true;
        },

        count: function (name, value, attributesJson) {
            var S = sentry();
            if (!S || !S.metrics || !S.metrics.count) {
                return false;
            }
            S.metrics.count(name, value, { attributes: parseJson(attributesJson, {}) });
            return true;
        },

        metric: function (method, name, value, unit, attributesJson) {
            var S = sentry();
            if (!S || !S.metrics || !S.metrics[method]) {
                return false;
            }
            var options = { attributes: parseJson(attributesJson, {}) };
            if (unit) {
                options.unit = unit;
            }
            S.metrics[method](name, value, options);
            return true;
        },

        registerSpan: function (span) {
            if (!span) {
                return 0;
            }
            var handle = this.nextSpanHandle++;
            this.spans[handle] = span;
            return handle;
        },

        startTransaction: function (name, operation, description, bindToScope, customSamplingContextJson, sampleRate) {
            var S = sentry();
            if (!S) {
                return 0;
            }

            var options = {
                name: name,
                op: operation || undefined,
                forceTransaction: true,
            };
            if (description) {
                options.description = description;
            }
            if (sampleRate >= 0) {
                options.sampled = Math.random() < sampleRate;
            }

            var span = null;
            if (S.startInactiveSpan) {
                span = S.startInactiveSpan(options);
            } else if (S.startTransaction) {
                var context = {
                    name: name,
                    op: operation || undefined,
                    description: description || undefined,
                };
                if (sampleRate >= 0) {
                    context.sampled = options.sampled;
                }
                span = S.startTransaction(context, parseJson(customSamplingContextJson, {}));
            }
            if (!span) {
                return 0;
            }
            if (bindToScope) {
                bindSpan(span);
            }
            return this.registerSpan(span);
        },

        startSpan: function (parentHandle, name, operation, description, bindToScope) {
            var S = sentry();
            var parent = parentHandle ? this.spans[parentHandle] : null;
            var span = null;
            if (parent && parent.startChild) {
                try {
                    span = parent.startChild(operation || undefined, description || undefined);
                } catch (error) {
                    span = parent.startChild({
                        name: name,
                        op: operation || undefined,
                        description: description || undefined,
                    });
                }
            } else if (S && S.startInactiveSpan) {
                var options = {
                    name: name,
                    op: operation || undefined,
                    description: description || undefined,
                };
                if (parent) {
                    options.parentSpan = parent;
                }
                span = S.startInactiveSpan(options);
            }
            if (!span) {
                return 0;
            }
            if (bindToScope) {
                bindSpan(span);
            }
            return this.registerSpan(span);
        },

        finishSpan: function (handle, status) {
            var span = this.spans[handle];
            if (!span) {
                return false;
            }
            delete this.spans[handle];
            setSpanStatus(span, spanStatus(status));
            return finishSpan(span);
        },

        setSpanStatus: function (handle, status) {
            var span = this.spans[handle];
            return !!span && setSpanStatus(span, spanStatus(status));
        },

        setSpanData: function (handle, key, valueJson) {
            var span = this.spans[handle];
            return !!span && setSpanData(span, key, parseJson(valueJson, null));
        },

        removeSpanData: function (handle, key) {
            var span = this.spans[handle];
            return !!span && setSpanData(span, key, undefined);
        },

        setSpanTag: function (handle, key, value) {
            var span = this.spans[handle];
            if (!span) {
                return false;
            }
            if (span.setTag) {
                span.setTag(key, value);
                return true;
            }
            return setSpanData(span, key, value);
        },

        removeSpanTag: function (handle, key) {
            var span = this.spans[handle];
            if (!span) {
                return false;
            }
            if (span.setTag) {
                span.setTag(key, undefined);
                return true;
            }
            return setSpanData(span, key, undefined);
        },

        spanTraceHeaders: function (handle) {
            return JSON.stringify(spanTraceHeaders(this.spans[handle]));
        },

        releaseSpan: function (handle) {
            delete this.spans[handle];
            return true;
        },

        recordBeforeSendError: function () {
            var S = sentry();
            var client = S && S.getClient ? S.getClient() : null;
            if (!client || !client.recordDroppedEvent) {
                return false;
            }
            try {
                client.recordDroppedEvent("before_send", "error");
                return true;
            } catch (error) {
                console.error("Sentry QML: Could not record beforeSend client report.", error);
                return false;
            }
        },

        captureEvent: function (eventJson, attachmentsJson) {
            var S = sentry();
            if (!S || !S.captureEvent) {
                return "";
            }
            var event = parseJson(eventJson, {});
            var attachments = attachmentsFromJson(attachmentsJson);
            var hint = attachments.length > 0 ? { attachments: attachments } : undefined;
            return S.captureEvent(event, hint) || "";
        },

        captureFeedback: function (feedbackJson, attachmentsJson) {
            var S = sentry();
            if (!S || !S.captureFeedback) {
                return false;
            }
            var feedback = parseJson(feedbackJson, {});
            var attachments = attachmentsFromJson(attachmentsJson);
            var hint = attachments.length > 0 ? { attachments: attachments } : undefined;
            return !!S.captureFeedback(feedback, hint);
        },
    };
});

EM_JS(int, sentry_qml_wasm_init, (const char *optionsJson, const char *sdkName), {
    return globalThis.__sentryQmlWasmBridge.init(UTF8ToString(optionsJson), UTF8ToString(sdkName)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_flush, (int timeoutMs), {
    return globalThis.__sentryQmlWasmBridge.flush(timeoutMs) ? 1 : 0;
});

EM_JS(void, sentry_qml_wasm_close, (int timeoutMs), {
    globalThis.__sentryQmlWasmBridge.close(timeoutMs);
});

EM_JS(int, sentry_qml_wasm_set_user_consent, (int required, int consent), {
    return globalThis.__sentryQmlWasmBridge.setUserConsent(required, consent) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_call_string, (const char *methodName, const char *arg), {
    var bridge = globalThis.__sentryQmlWasmBridge;
    var method = bridge[UTF8ToString(methodName)];
    return method && method.call(bridge, UTF8ToString(arg)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_call_two_strings, (const char *methodName, const char *first, const char *second), {
    var bridge = globalThis.__sentryQmlWasmBridge;
    var method = bridge[UTF8ToString(methodName)];
    return method && method.call(bridge, UTF8ToString(first), UTF8ToString(second)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_log, (int level, const char *message, const char *attributesJson), {
    return globalThis.__sentryQmlWasmBridge.log(level, UTF8ToString(message), UTF8ToString(attributesJson)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_count, (const char *name, double value, const char *attributesJson), {
    return globalThis.__sentryQmlWasmBridge.count(UTF8ToString(name), value, UTF8ToString(attributesJson)) ? 1 : 0;
});

EM_JS(int,
      sentry_qml_wasm_metric,
      (const char *methodName, const char *name, double value, const char *unit, const char *attributesJson),
      {
          return globalThis.__sentryQmlWasmBridge.metric(
                     UTF8ToString(methodName),
                     UTF8ToString(name),
                     value,
                     UTF8ToString(unit),
                     UTF8ToString(attributesJson))
              ? 1
              : 0;
      });

EM_JS(int,
      sentry_qml_wasm_start_transaction,
      (const char *name,
       const char *operation,
       const char *description,
       int bindToScope,
       const char *customSamplingContextJson,
       double sampleRate),
      {
          return globalThis.__sentryQmlWasmBridge.startTransaction(
              UTF8ToString(name),
              UTF8ToString(operation),
              UTF8ToString(description),
              !!bindToScope,
              UTF8ToString(customSamplingContextJson),
              sampleRate);
      });

EM_JS(int,
      sentry_qml_wasm_start_span,
      (int parentHandle, const char *name, const char *operation, const char *description, int bindToScope),
      {
          return globalThis.__sentryQmlWasmBridge.startSpan(
              parentHandle,
              UTF8ToString(name),
              UTF8ToString(operation),
              UTF8ToString(description),
              !!bindToScope);
      });

EM_JS(int, sentry_qml_wasm_finish_span, (int handle, const char *status), {
    return globalThis.__sentryQmlWasmBridge.finishSpan(handle, UTF8ToString(status)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_set_span_status, (int handle, const char *status), {
    return globalThis.__sentryQmlWasmBridge.setSpanStatus(handle, UTF8ToString(status)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_set_span_data, (int handle, const char *key, const char *valueJson), {
    return globalThis.__sentryQmlWasmBridge.setSpanData(handle, UTF8ToString(key), UTF8ToString(valueJson)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_remove_span_data, (int handle, const char *key), {
    return globalThis.__sentryQmlWasmBridge.removeSpanData(handle, UTF8ToString(key)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_set_span_tag, (int handle, const char *key, const char *value), {
    return globalThis.__sentryQmlWasmBridge.setSpanTag(handle, UTF8ToString(key), UTF8ToString(value)) ? 1 : 0;
});

EM_JS(int, sentry_qml_wasm_remove_span_tag, (int handle, const char *key), {
    return globalThis.__sentryQmlWasmBridge.removeSpanTag(handle, UTF8ToString(key)) ? 1 : 0;
});

EM_JS(char *, sentry_qml_wasm_span_trace_headers, (int handle), {
    return stringToNewUTF8(globalThis.__sentryQmlWasmBridge.spanTraceHeaders(handle) || "{}");
});

EM_JS(void, sentry_qml_wasm_release_span, (int handle), {
    globalThis.__sentryQmlWasmBridge.releaseSpan(handle);
});

EM_JS(int, sentry_qml_wasm_record_before_send_error, (), {
    return globalThis.__sentryQmlWasmBridge.recordBeforeSendError() ? 1 : 0;
});

EM_JS(char *, sentry_qml_wasm_capture_event, (const char *eventJson, const char *attachmentsJson), {
    var eventId = globalThis.__sentryQmlWasmBridge.captureEvent(
        UTF8ToString(eventJson), UTF8ToString(attachmentsJson));
    return stringToNewUTF8(eventId || "");
});

EM_JS(int, sentry_qml_wasm_capture_feedback, (const char *feedbackJson, const char *attachmentsJson), {
    return globalThis.__sentryQmlWasmBridge.captureFeedback(
               UTF8ToString(feedbackJson), UTF8ToString(attachmentsJson))
        ? 1
        : 0;
});

namespace {

thread_local int hookDepth = 0;

struct HookResult
{
    enum Action
    {
        Keep,
        Drop,
        Replace
    };

    Action action = Keep;
    QVariant value;
};

bool isSupportedInteger(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return true;
    default:
        return false;
    }
}

bool createEventHookState(Sentry *sentry,
                          SentryOptions *options,
                          const QJSValue &callback,
                          const QString &propertyName,
                          bool requireSameThread,
                          std::unique_ptr<SentrySdkEventHookState> *state)
{
    if (callback.isUndefined() || callback.isNull()) {
        return true;
    }

    if (!callback.isCallable()) {
        emit sentry->errorOccurred(QStringLiteral("SentryOptions.%1 must be a function.").arg(propertyName));
        return false;
    }

    QJSEngine *engine = qjsEngine(options);
    if (!engine) {
        emit sentry->errorOccurred(
            QStringLiteral("SentryOptions.%1 requires options created by a QML engine.").arg(propertyName));
        return false;
    }

    auto hookState = std::make_unique<SentrySdkEventHookState>();
    hookState->sentry = sentry;
    hookState->engine = engine;
    hookState->callback = callback;
    hookState->thread = requireSameThread ? QThread::currentThread() : nullptr;
    hookState->propertyName = propertyName;
    *state = std::move(hookState);
    return true;
}

HookResult invokeValueHook(const QVariant &value, SentrySdkEventHookState *state)
{
    if (!state || !state->engine || !state->callback.isCallable()) {
        return {};
    }

    if (state->thread && QThread::currentThread() != state->thread) {
        return {};
    }

    const QScopedValueRollback<int> rollback(hookDepth, hookDepth + 1);
    QJSValue scriptValue = SentryEvent::toScriptValue(state->engine, value);
    if (scriptValue.isUndefined()) {
        return {};
    }

    QJSValue result = state->callback.call({scriptValue});
    if (result.isError()) {
        if (state->sentry) {
            emit state->sentry->errorOccurred(
                QStringLiteral("SentryOptions.%1 failed: %2").arg(state->propertyName, result.toString()));
        }
        return {};
    }

    if (result.isUndefined() || (result.isBool() && result.toBool())) {
        return {};
    }

    if (result.isNull() || (result.isBool() && !result.toBool())) {
        return {HookResult::Drop, {}};
    }

    return {HookResult::Replace, result.toVariant()};
}

QVariant normalizedScriptValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        return value.value<QJSValue>().toVariant();
    }

    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            it.value() = normalizedScriptValue(it.value());
        }
        return map;
    }

    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list) {
            item = normalizedScriptValue(item);
        }
        return list;
    }

    return value;
}

bool isSupportedMetricAttribute(const QVariant &value)
{
    const QVariant attributeValue = normalizedScriptValue(value);

    if (attributeValue.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = attributeValue.toMap();
        if (map.contains(QStringLiteral("value"))) {
            return isSupportedMetricAttribute(map.value(QStringLiteral("value")));
        }
        return true;
    }

    switch (attributeValue.metaType().id()) {
    case QMetaType::Bool:
    case QMetaType::Float:
    case QMetaType::Double:
    case QMetaType::QString:
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        return true;
    default:
        return isSupportedInteger(attributeValue);
    }
}

QByteArray jsonFromVariant(const QVariant &value)
{
    const QVariant normalizedValue = normalizedScriptValue(value);

    if (normalizedValue.metaType().id() == QMetaType::QVariantMap) {
        return QJsonDocument::fromVariant(normalizedValue.toMap()).toJson(QJsonDocument::Compact);
    }
    if (normalizedValue.metaType().id() == QMetaType::QVariantList) {
        return QJsonDocument::fromVariant(normalizedValue.toList()).toJson(QJsonDocument::Compact);
    }
    if (normalizedValue.metaType().id() == QMetaType::QStringList) {
        QJsonArray array;
        for (const QString &item : normalizedValue.toStringList()) {
            array.append(item);
        }
        return QJsonDocument(array).toJson(QJsonDocument::Compact);
    }

    QJsonArray wrapper;
    wrapper.append(QJsonValue::fromVariant(normalizedValue));
    QByteArray json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (json.size() >= 2) {
        json = json.mid(1, json.size() - 2);
    }
    return json;
}

QString jsonStringFromVariant(const QVariant &value)
{
    return QString::fromUtf8(jsonFromVariant(value));
}

QVariantMap variantMapFromJsonString(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return document.object().toVariantMap();
}

QString generatedEventId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString normalizedEventId(const QString &eventId)
{
    QString value = eventId.trimmed();
    if (value.size() == 32) {
        value.insert(20, QLatin1Char('-'));
        value.insert(16, QLatin1Char('-'));
        value.insert(12, QLatin1Char('-'));
        value.insert(8, QLatin1Char('-'));
    }
    return value;
}

QVariantMap userFromVariantMap(const QVariantMap &user)
{
    QVariantMap nativeUser = user;
    const QString ipAddressKey = QStringLiteral("ipAddress");
    const QString protocolIpAddressKey = QStringLiteral("ip_address");
    if (nativeUser.contains(ipAddressKey) && !nativeUser.contains(protocolIpAddressKey)) {
        nativeUser.insert(protocolIpAddressKey, nativeUser.value(ipAddressKey));
        nativeUser.remove(ipAddressKey);
    }
    return nativeUser;
}

QString levelNameFromInt(int level)
{
    switch (level) {
    case -2:
        return QStringLiteral("trace");
    case -1:
        return QStringLiteral("debug");
    case 1:
        return QStringLiteral("warning");
    case 2:
        return QStringLiteral("error");
    case 3:
        return QStringLiteral("fatal");
    case 0:
    default:
        return QStringLiteral("info");
    }
}

int levelFromVariant(const QVariant &value, int fallback)
{
    if (isSupportedInteger(value)) {
        return value.toInt();
    }

    const QString level = SentryEvent::levelNameFromString(value.toString());
    if (level == QLatin1String("trace")) {
        return -2;
    }
    if (level == QLatin1String("debug")) {
        return -1;
    }
    if (level == QLatin1String("warning")) {
        return 1;
    }
    if (level == QLatin1String("error")) {
        return 2;
    }
    if (level == QLatin1String("fatal")) {
        return 3;
    }
    if (level == QLatin1String("info")) {
        return 0;
    }
    return fallback;
}

QString feedbackValue(const QVariantMap &feedback,
                      const QString &key,
                      const QString &fallbackKey = {},
                      const QString &secondFallbackKey = {})
{
    if (feedback.contains(key)) {
        return feedback.value(key).toString();
    }
    if (!fallbackKey.isEmpty() && feedback.contains(fallbackKey)) {
        return feedback.value(fallbackKey).toString();
    }
    if (!secondFallbackKey.isEmpty() && feedback.contains(secondFallbackKey)) {
        return feedback.value(secondFallbackKey).toString();
    }
    return {};
}

QByteArray attachmentBytes(const SentrySdkAttachmentState &attachment)
{
    if (attachment.type == SentrySdkAttachmentState::Bytes) {
        return attachment.bytes;
    }

    QFile file(attachment.path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QVariantMap attachmentToVariantMap(const SentrySdkAttachmentState &attachment)
{
    QVariantMap map;
    const QByteArray bytes = attachmentBytes(attachment);
    if (!bytes.isEmpty()) {
        map.insert(QStringLiteral("type"), QStringLiteral("bytes"));
        map.insert(QStringLiteral("bytes"), QString::fromLatin1(bytes.toBase64()));
    } else {
        map.insert(QStringLiteral("type"), QStringLiteral("file"));
        if (!attachment.path.isEmpty()) {
            map.insert(QStringLiteral("path"), attachment.path);
        }
    }

    QString filename = attachment.filename;
    if (filename.isEmpty() && !attachment.path.isEmpty()) {
        filename = QFileInfo(attachment.path).fileName();
    }
    if (!filename.isEmpty()) {
        map.insert(QStringLiteral("filename"), filename);
    }
    if (!attachment.contentType.isEmpty()) {
        map.insert(QStringLiteral("contentType"), attachment.contentType);
    }
    if (!attachment.attachmentType.isEmpty()) {
        map.insert(QStringLiteral("attachmentType"), attachment.attachmentType);
    }
    return map;
}

QString jsonStringFromAttachments(const QList<SentrySdkAttachmentState> &attachments)
{
    QVariantList values;
    for (const SentrySdkAttachmentState &attachment : attachments) {
        values.append(attachmentToVariantMap(attachment));
    }
    return jsonStringFromVariant(values);
}

SentrySdkAttachmentState hintAttachment(const SentryHintAttachment &attachment)
{
    SentrySdkAttachmentState native;
    native.type = attachment.type == SentryHintAttachmentType::File ? SentrySdkAttachmentState::File
                                                                    : SentrySdkAttachmentState::Bytes;
    native.path = attachment.path;
    native.bytes = attachment.bytes;
    native.filename = attachment.filename;
    native.contentType = attachment.contentType;
    return native;
}

SentrySdkAttachmentState viewHierarchyAttachment(const QByteArray &bytes)
{
    SentrySdkAttachmentState attachment;
    attachment.type = SentrySdkAttachmentState::Bytes;
    attachment.bytes = bytes;
    attachment.filename = QStringLiteral("view-hierarchy.json");
    attachment.contentType = QStringLiteral("application/json");
    attachment.attachmentType = QStringLiteral("event.view_hierarchy");
    return attachment;
}

SentrySdkAttachmentState screenshotAttachment(const QByteArray &bytes)
{
    SentrySdkAttachmentState attachment;
    attachment.type = SentrySdkAttachmentState::Bytes;
    attachment.bytes = bytes;
    attachment.filename = QStringLiteral("screenshot.png");
    attachment.contentType = QStringLiteral("image/png");
    return attachment;
}

QByteArray utf8(const QString &value)
{
    return value.toUtf8();
}

bool callBridgeString(const char *method, const QString &arg)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray methodUtf8(method);
    const QByteArray argUtf8 = utf8(arg);
    return sentry_qml_wasm_call_string(methodUtf8.constData(), argUtf8.constData()) != 0;
}

bool callBridgeStrings(const char *method, const QString &first, const QString &second)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray methodUtf8(method);
    const QByteArray firstUtf8 = utf8(first);
    const QByteArray secondUtf8 = utf8(second);
    return sentry_qml_wasm_call_two_strings(methodUtf8.constData(), firstUtf8.constData(), secondUtf8.constData()) != 0;
}

bool initBridge(const QVariantMap &options)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray optionsJson = jsonFromVariant(options);
    const QByteArray sdkName(SENTRY_QML_SDK_NAME);
    return sentry_qml_wasm_init(optionsJson.constData(), sdkName.constData()) != 0;
}

bool flushBridge(int timeoutMs)
{
    sentry_qml_wasm_ensure_bridge();
    return sentry_qml_wasm_flush(timeoutMs) != 0;
}

void closeBridge(int timeoutMs)
{
    sentry_qml_wasm_ensure_bridge();
    sentry_qml_wasm_close(timeoutMs);
}

void setBridgeUserConsent(bool required, int consent)
{
    sentry_qml_wasm_ensure_bridge();
    sentry_qml_wasm_set_user_consent(required ? 1 : 0, consent);
}

void setBridgeIntegrations(const QStringList &integrations)
{
    callBridgeString("setIntegrations", jsonStringFromVariant(integrations));
}

QString captureEventBridge(const QVariantMap &event, const QList<SentrySdkAttachmentState> &attachments)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray eventJson = jsonFromVariant(event);
    const QByteArray attachmentsJson = utf8(jsonStringFromAttachments(attachments));
    char *eventId = sentry_qml_wasm_capture_event(eventJson.constData(), attachmentsJson.constData());
    const QString result = eventId ? normalizedEventId(QString::fromUtf8(eventId)) : QString();
    std::free(eventId);
    return result;
}

bool captureFeedbackBridge(const QVariantMap &feedback, const QList<SentrySdkAttachmentState> &attachments)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray feedbackJson = jsonFromVariant(feedback);
    const QByteArray attachmentsJson = utf8(jsonStringFromAttachments(attachments));
    return sentry_qml_wasm_capture_feedback(feedbackJson.constData(), attachmentsJson.constData()) != 0;
}

bool logBridge(int level, const QString &message, const QVariantMap &attributes)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray messageUtf8 = utf8(message);
    const QByteArray attributesJson = jsonFromVariant(attributes);
    return sentry_qml_wasm_log(level, messageUtf8.constData(), attributesJson.constData()) != 0;
}

bool countBridge(const QString &name, qint64 value, const QVariantMap &attributes)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray nameUtf8 = utf8(name);
    const QByteArray attributesJson = jsonFromVariant(attributes);
    return sentry_qml_wasm_count(nameUtf8.constData(), static_cast<double>(value), attributesJson.constData()) != 0;
}

bool metricBridge(const char *method,
                  const QString &name,
                  double value,
                  const QString &unit,
                  const QVariantMap &attributes)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray methodUtf8(method);
    const QByteArray nameUtf8 = utf8(name);
    const QByteArray unitUtf8 = utf8(unit);
    const QByteArray attributesJson = jsonFromVariant(attributes);
    return sentry_qml_wasm_metric(
               methodUtf8.constData(), nameUtf8.constData(), value, unitUtf8.constData(), attributesJson.constData())
        != 0;
}

int startTransactionBridge(const QString &name,
                           const QString &operation,
                           const QString &description,
                           bool bindToScope,
                           const QVariantMap &customSamplingContext,
                           double sampleRate)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray nameUtf8 = utf8(name);
    const QByteArray operationUtf8 = utf8(operation);
    const QByteArray descriptionUtf8 = utf8(description);
    const QByteArray customSamplingContextJson = jsonFromVariant(customSamplingContext);
    return sentry_qml_wasm_start_transaction(nameUtf8.constData(),
                                             operationUtf8.constData(),
                                             descriptionUtf8.constData(),
                                             bindToScope ? 1 : 0,
                                             customSamplingContextJson.constData(),
                                             sampleRate);
}

int startSpanBridge(int parentHandle,
                    const QString &name,
                    const QString &operation,
                    const QString &description,
                    bool bindToScope)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray nameUtf8 = utf8(name);
    const QByteArray operationUtf8 = utf8(operation);
    const QByteArray descriptionUtf8 = utf8(description);
    return sentry_qml_wasm_start_span(
        parentHandle, nameUtf8.constData(), operationUtf8.constData(), descriptionUtf8.constData(), bindToScope ? 1 : 0);
}

bool finishSpanBridge(int handle, const QString &status)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray statusUtf8 = utf8(status);
    return sentry_qml_wasm_finish_span(handle, statusUtf8.constData()) != 0;
}

bool setSpanStatusBridge(int handle, const QString &status)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray statusUtf8 = utf8(status);
    return sentry_qml_wasm_set_span_status(handle, statusUtf8.constData()) != 0;
}

bool setSpanDataBridge(int handle, const QString &key, const QVariant &value)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray keyUtf8 = utf8(key);
    const QByteArray valueJson = jsonFromVariant(value);
    return sentry_qml_wasm_set_span_data(handle, keyUtf8.constData(), valueJson.constData()) != 0;
}

bool removeSpanDataBridge(int handle, const QString &key)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray keyUtf8 = utf8(key);
    return sentry_qml_wasm_remove_span_data(handle, keyUtf8.constData()) != 0;
}

bool setSpanTagBridge(int handle, const QString &key, const QString &value)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray keyUtf8 = utf8(key);
    const QByteArray valueUtf8 = utf8(value);
    return sentry_qml_wasm_set_span_tag(handle, keyUtf8.constData(), valueUtf8.constData()) != 0;
}

bool removeSpanTagBridge(int handle, const QString &key)
{
    sentry_qml_wasm_ensure_bridge();
    const QByteArray keyUtf8 = utf8(key);
    return sentry_qml_wasm_remove_span_tag(handle, keyUtf8.constData()) != 0;
}

QVariantMap spanTraceHeadersBridge(int handle)
{
    sentry_qml_wasm_ensure_bridge();
    char *headersJson = sentry_qml_wasm_span_trace_headers(handle);
    const QString json = headersJson ? QString::fromUtf8(headersJson) : QString();
    std::free(headersJson);
    return variantMapFromJsonString(json);
}

void releaseSpanBridge(int handle)
{
    sentry_qml_wasm_ensure_bridge();
    sentry_qml_wasm_release_span(handle);
}

bool recordBeforeSendErrorBridge()
{
    sentry_qml_wasm_ensure_bridge();
    return sentry_qml_wasm_record_before_send_error() != 0;
}

bool hasConsent(bool requireUserConsent, int userConsent)
{
    return !requireUserConsent || userConsent == 1;
}

} // namespace

SentrySdk *SentrySdk::instance()
{
    static SentrySdk sdk;
    return &sdk;
}

SentrySdk::SentrySdk(QObject *parent)
    : QObject(parent)
    , m_integrationManager(std::make_unique<SentryIntegrationManager>(this))
{
}

SentrySdk::~SentrySdk()
{
    if (m_initialized && QCoreApplication::instance() && !QCoreApplication::closingDown()) {
        close();
    }
}

bool SentrySdk::isInitialized() const
{
    return m_initialized;
}

bool SentrySdk::init(Sentry *sentry, SentryOptions *options)
{
    if (m_initialized) {
        return true;
    }

    if (!sentry) {
        return false;
    }

    if (!options) {
        emit sentry->errorOccurred(QStringLiteral("Sentry.init requires a SentryOptions object."));
        return false;
    }

    if (!std::isfinite(options->sampleRate()) || options->sampleRate() < 0.0 || options->sampleRate() > 1.0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry sampleRate must be between 0.0 and 1.0."));
        return false;
    }

    if (options->maxBreadcrumbs() < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry maxBreadcrumbs must not be negative."));
        return false;
    }

    if (options->shutdownTimeout() < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry shutdownTimeout must not be negative."));
        return false;
    }

    const double tracesSampleRate = options->tracesSampleRate();
    if (!qFuzzyCompare(tracesSampleRate, -1.0)
        && (!std::isfinite(tracesSampleRate) || tracesSampleRate < 0.0 || tracesSampleRate > 1.0)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry tracesSampleRate must be between 0.0 and 1.0."));
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeBreadcrumbState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeBreadcrumb(),
                              QStringLiteral("beforeBreadcrumb"),
                              true,
                              &beforeBreadcrumbState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendLogState;
    if (!createEventHookState(
            sentry, options, options->beforeSendLog(), QStringLiteral("beforeSendLog"), true, &beforeSendLogState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendMetricState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeSendMetric(),
                              QStringLiteral("beforeSendMetric"),
                              true,
                              &beforeSendMetricState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendState;
    if (!createEventHookState(
            sentry, options, options->beforeSend(), QStringLiteral("beforeSend"), true, &beforeSendState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendTransactionState;
    if (!createEventHookState(sentry,
                              options,
                              options->beforeSendTransaction(),
                              QStringLiteral("beforeSendTransaction"),
                              true,
                              &beforeSendTransactionState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> beforeSendSpanState;
    if (!createEventHookState(
            sentry, options, options->beforeSendSpan(), QStringLiteral("beforeSendSpan"), true, &beforeSendSpanState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> tracesSamplerState;
    if (!createEventHookState(
            sentry, options, options->tracesSampler(), QStringLiteral("tracesSampler"), true, &tracesSamplerState)) {
        return false;
    }

    std::unique_ptr<SentrySdkEventHookState> onCrashState;
    if (!createEventHookState(sentry, options, options->onCrash(), QStringLiteral("onCrash"), false, &onCrashState)) {
        return false;
    }
    if (onCrashState) {
        qmlWarning(options) << "SentryOptions.onCrash is not supported by the Sentry JavaScript bridge "
                               "and will be ignored.";
    }

    m_integrationManager->beginInitialization(sentry, options, QStringLiteral("wasm"));
    if (!m_integrationManager->prepare(options)) {
        return false;
    }

    const bool didChangeUserConsentRequired = m_requireUserConsent != options->requireUserConsent();
    m_beforeBreadcrumbState = std::move(beforeBreadcrumbState);
    m_beforeSendLogState = std::move(beforeSendLogState);
    m_beforeSendMetricState = std::move(beforeSendMetricState);
    m_beforeSendState = std::move(beforeSendState);
    m_beforeSendTransactionState = std::move(beforeSendTransactionState);
    m_beforeSendSpanState = std::move(beforeSendSpanState);
    m_tracesSamplerState = std::move(tracesSamplerState);
    m_onCrashState = std::move(onCrashState);
    m_crashHookState = std::make_unique<SentrySdkCrashHookState>();
    m_applyHooksLocally = true;
    m_dsn = options->dsn();
    m_release = options->release();
    m_environment = options->environment();
    m_dist = options->dist();
    m_user = options->user() ? userFromVariantMap(options->user()->toVariantMap()) : QVariantMap {};
    m_tags.clear();
    m_contexts.clear();
    m_breadcrumbs.clear();
    m_fingerprint.clear();
    m_maxBreadcrumbs = options->maxBreadcrumbs();
    m_attachScreenshot = options->attachScreenshot();
    m_attachViewHierarchy = options->attachViewHierarchy();
    m_requireUserConsent = options->requireUserConsent();
    m_userConsent = -1;

    QVariantMap nativeOptions = {
        {QStringLiteral("dsn"), options->dsn()},
        {QStringLiteral("release"), options->release()},
        {QStringLiteral("environment"), options->environment()},
        {QStringLiteral("dist"), options->dist()},
        {QStringLiteral("debug"), options->debug()},
        {QStringLiteral("sendClientReports"), options->sendClientReports()},
        {QStringLiteral("autoSessionTracking"), options->autoSessionTracking()},
        {QStringLiteral("requireUserConsent"), options->requireUserConsent()},
        {QStringLiteral("attachScreenshot"), options->attachScreenshot()},
        {QStringLiteral("sampleRate"), options->sampleRate()},
        {QStringLiteral("tracesSampleRate"), options->tracesSampleRate()},
        {QStringLiteral("tracePropagationTargets"), options->tracePropagationTargets()},
        {QStringLiteral("orgId"), options->orgId()},
        {QStringLiteral("strictTraceContinuation"), options->strictTraceContinuation()},
        {QStringLiteral("maxBreadcrumbs"), options->maxBreadcrumbs()},
        {QStringLiteral("integrations"), m_integrationManager->preparedIntegrationIds()},
    };
    if (!m_user.isEmpty()) {
        nativeOptions.insert(QStringLiteral("user"), m_user);
    }

    if (!m_dsn.isEmpty() && !initBridge(nativeOptions)) {
        m_integrationManager->stop();
        clearLocalScope();
        m_beforeBreadcrumbState.reset();
        m_beforeSendLogState.reset();
        m_beforeSendMetricState.reset();
        m_beforeSendState.reset();
        m_beforeSendTransactionState.reset();
        m_beforeSendSpanState.reset();
        m_tracesSamplerState.reset();
        m_onCrashState.reset();
        m_crashHookState.reset();
        emit sentry->errorOccurred(QStringLiteral("Sentry JavaScript could not be initialized."));
        return false;
    }

    if (!m_integrationManager->start()) {
        if (!m_dsn.isEmpty()) {
            closeBridge(0);
        }
        clearLocalScope();
        m_beforeBreadcrumbState.reset();
        m_beforeSendLogState.reset();
        m_beforeSendMetricState.reset();
        m_beforeSendState.reset();
        m_beforeSendTransactionState.reset();
        m_beforeSendSpanState.reset();
        m_tracesSamplerState.reset();
        m_onCrashState.reset();
        m_crashHookState.reset();
        return false;
    }

    if (!m_dsn.isEmpty()) {
        setBridgeIntegrations(m_integrationManager->activeIntegrationIds());
    }

    setInitialized(true);
    if (didChangeUserConsentRequired) {
        emit userConsentRequiredChanged();
    }
    emit userConsentChanged();
    connectToApplicationShutdown();
    return true;
}

bool SentrySdk::flush(int timeoutMs)
{
    if (!m_initialized) {
        return true;
    }

    int remainingTimeoutMs = timeoutMs;
    const bool integrationsFlushed = m_integrationManager->flush(timeoutMs, &remainingTimeoutMs);
    const bool backendFlushed = m_dsn.isEmpty() || flushBridge(remainingTimeoutMs);
    return integrationsFlushed && backendFlushed;
}

bool SentrySdk::close()
{
    if (!m_initialized) {
        return true;
    }

    m_integrationManager->stop();
    if (!m_dsn.isEmpty()) {
        closeBridge(0);
    }

    if (m_applicationShutdownConnection) {
        QObject::disconnect(m_applicationShutdownConnection);
        m_applicationShutdownConnection = {};
    }

    const bool didChangeUserConsentRequired = m_requireUserConsent;
    m_beforeBreadcrumbState.reset();
    m_beforeSendLogState.reset();
    m_beforeSendMetricState.reset();
    m_beforeSendState.reset();
    m_beforeSendTransactionState.reset();
    m_beforeSendSpanState.reset();
    m_tracesSamplerState.reset();
    m_onCrashState.reset();
    m_crashHookState.reset();
    m_applyHooksLocally = false;
    clearLocalScope();
    invalidateAttachments();
    invalidateSpans();
    setInitialized(false);
    if (didChangeUserConsentRequired) {
        emit userConsentRequiredChanged();
    }
    emit userConsentChanged();
    return true;
}

void SentrySdk::closeBeforeApplicationShutdown()
{
    close();
}

void SentrySdk::connectToApplicationShutdown()
{
    if (m_applicationShutdownConnection || !QCoreApplication::instance()) {
        return;
    }

    m_applicationShutdownConnection = QObject::connect(QCoreApplication::instance(),
                                                       &QCoreApplication::aboutToQuit,
                                                       this,
                                                       &SentrySdk::closeBeforeApplicationShutdown,
                                                       Qt::DirectConnection);
}

void SentrySdk::detachSentry(Sentry *sentry)
{
    auto detach = [sentry](std::unique_ptr<SentrySdkEventHookState> &state)
    {
        if (state && state->sentry == sentry) {
            state->sentry = nullptr;
            state->engine = nullptr;
            state->callback = QJSValue();
        }
    };

    detach(m_beforeSendState);
    detach(m_beforeBreadcrumbState);
    detach(m_beforeSendLogState);
    detach(m_beforeSendMetricState);
    detach(m_onCrashState);
    detach(m_beforeSendTransactionState);
    detach(m_beforeSendSpanState);
    detach(m_tracesSamplerState);
}

bool SentrySdk::ensureCanCall(Sentry *sentry,
                              const char *method,
                              const char *action,
                              const char *hookType) const
{
    if (hookDepth > 0) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.%1 cannot be called from Sentry %2.")
                                           .arg(QString::fromLatin1(method), QString::fromLatin1(hookType)));
        }
        return false;
    }

    return ensureInitialized(sentry, action);
}

bool SentrySdk::ensureInitialized(Sentry *sentry, const char *action) const
{
    if (m_initialized || m_integrationOperation) {
        return true;
    }

    if (sentry) {
        emit sentry->errorOccurred(
            QStringLiteral("Sentry must be initialized before %1.").arg(QString::fromLatin1(action)));
    }
    return false;
}

int SentrySdk::userConsent() const
{
    return m_requireUserConsent ? m_userConsent : -1;
}

bool SentrySdk::isUserConsentRequired() const
{
    return m_requireUserConsent;
}

bool SentrySdk::giveUserConsent(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "giveUserConsent", "changing user consent")) {
        return false;
    }

    if (!m_requireUserConsent) {
        return false;
    }

    if (m_userConsent != 1) {
        m_userConsent = 1;
        if (!m_dsn.isEmpty()) {
            setBridgeUserConsent(m_requireUserConsent, m_userConsent);
        }
        emit userConsentChanged();
    }
    return true;
}

bool SentrySdk::revokeUserConsent(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "revokeUserConsent", "changing user consent")) {
        return false;
    }

    if (!m_requireUserConsent) {
        return false;
    }

    if (m_userConsent != 0) {
        m_userConsent = 0;
        if (!m_dsn.isEmpty()) {
            setBridgeUserConsent(m_requireUserConsent, m_userConsent);
        }
        emit userConsentChanged();
    }
    return true;
}

bool SentrySdk::resetUserConsent(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "resetUserConsent", "changing user consent")) {
        return false;
    }

    if (!m_requireUserConsent) {
        return false;
    }

    if (m_userConsent != -1) {
        m_userConsent = -1;
        if (!m_dsn.isEmpty()) {
            setBridgeUserConsent(m_requireUserConsent, m_userConsent);
        }
        emit userConsentChanged();
    }
    return true;
}

bool SentrySdk::setRelease(Sentry *sentry, const QString &release)
{
    if (!ensureCanCall(sentry, "setRelease", "setting releases")) {
        return false;
    }

    m_release = release;
    return m_dsn.isEmpty() || callBridgeString("setRelease", release);
}

bool SentrySdk::setEnvironment(Sentry *sentry, const QString &environment)
{
    if (!ensureCanCall(sentry, "setEnvironment", "setting environments")) {
        return false;
    }

    m_environment = environment;
    return m_dsn.isEmpty() || callBridgeString("setEnvironment", environment);
}

bool SentrySdk::setLevel(Sentry *sentry, int level)
{
    if (!ensureCanCall(sentry, "setLevel", "setting levels")) {
        return false;
    }

    m_level = SentryEvent::levelNameFromInt(level);
    return m_dsn.isEmpty() || callBridgeString("setLevel", QString::number(level));
}

bool SentrySdk::setTransaction(Sentry *sentry, const QString &transaction)
{
    if (!ensureCanCall(sentry, "setTransaction", "setting transactions")) {
        return false;
    }

    if (transaction.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry transaction must not be empty."));
        return false;
    }

    m_transaction = transaction;
    return m_dsn.isEmpty() || callBridgeString("setTransaction", transaction);
}

bool SentrySdk::setUser(Sentry *sentry, const QVariantMap &user)
{
    if (!ensureCanCall(sentry, "setUser", "setting users")) {
        return false;
    }

    m_user = userFromVariantMap(user);
    return m_dsn.isEmpty() || callBridgeString("setUser", jsonStringFromVariant(m_user));
}

bool SentrySdk::removeUser(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeUser", "removing users")) {
        return false;
    }

    m_user.clear();
    return m_dsn.isEmpty() || callBridgeString("removeUser", QString());
}

bool SentrySdk::setTag(Sentry *sentry, const QString &key, const QString &value)
{
    if (!ensureCanCall(sentry, "setTag", "setting tags")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        return false;
    }

    m_tags.insert(key, value);
    return m_dsn.isEmpty() || callBridgeStrings("setTag", key, value);
}

bool SentrySdk::removeTag(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeTag", "removing tags")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry tag key must not be empty."));
        return false;
    }

    m_tags.remove(key);
    return m_dsn.isEmpty() || callBridgeString("removeTag", key);
}

bool SentrySdk::setContext(Sentry *sentry, const QString &key, const QVariantMap &context)
{
    if (!ensureCanCall(sentry, "setContext", "setting contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        return false;
    }

    m_contexts.insert(key, context);
    return m_dsn.isEmpty() || callBridgeStrings("setContext", key, jsonStringFromVariant(context));
}

bool SentrySdk::removeContext(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeContext", "removing contexts")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry context key must not be empty."));
        return false;
    }

    m_contexts.remove(key);
    return m_dsn.isEmpty() || callBridgeString("removeContext", key);
}

bool SentrySdk::setAttribute(Sentry *sentry, const QString &key, const QVariant &value)
{
    if (!ensureCanCall(sentry, "setAttribute", "setting attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        return false;
    }

    if (!isSupportedMetricAttribute(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute value must be supported."));
        return false;
    }

    return m_dsn.isEmpty() || callBridgeStrings("setAttribute", key, jsonStringFromVariant(value));
}

bool SentrySdk::removeAttribute(Sentry *sentry, const QString &key)
{
    if (!ensureCanCall(sentry, "removeAttribute", "removing attributes")) {
        return false;
    }

    if (key.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attribute key must not be empty."));
        return false;
    }

    return m_dsn.isEmpty() || callBridgeString("removeAttribute", key);
}

bool SentrySdk::setFingerprint(Sentry *sentry, const QStringList &fingerprint)
{
    if (!ensureCanCall(sentry, "setFingerprint", "setting fingerprints")) {
        return false;
    }

    m_fingerprint = fingerprint;
    return m_dsn.isEmpty() || callBridgeString("setFingerprint", jsonStringFromVariant(fingerprint));
}

bool SentrySdk::removeFingerprint(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "removeFingerprint", "removing fingerprints")) {
        return false;
    }

    m_fingerprint.clear();
    return m_dsn.isEmpty() || callBridgeString("setFingerprint", QStringLiteral("[]"));
}

SentryAttachment *SentrySdk::attachFile(Sentry *sentry, const QString &path, const QString &contentType)
{
    if (!ensureCanCall(sentry, "attachFile", "attaching files")) {
        return nullptr;
    }

    if (path.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry attachment path must not be empty."));
        return nullptr;
    }

    auto *state = new SentrySdkAttachmentState;
    state->type = SentrySdkAttachmentState::File;
    state->path = path;
    auto *wrapper = new SentryAttachment(state, sentry);
    const QFileInfo fileInfo(path);
    wrapper->setSize(fileInfo.exists() ? fileInfo.size() : -1);
    if (!contentType.isEmpty()) {
        wrapper->setContentType(contentType);
    }
    trackAttachment(wrapper);
    return wrapper;
}

SentryAttachment *SentrySdk::attachBytes(Sentry *sentry,
                                         const QByteArray &bytes,
                                         const QString &filename,
                                         const QString &contentType)
{
    if (!ensureCanCall(sentry, "attachBytes", "attaching bytes")) {
        return nullptr;
    }

    if (bytes.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment must not be empty."));
        return nullptr;
    }

    if (filename.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry byte attachment filename must not be empty."));
        return nullptr;
    }

    auto *state = new SentrySdkAttachmentState;
    state->type = SentrySdkAttachmentState::Bytes;
    state->bytes = bytes;
    state->filename = filename;
    auto *wrapper = new SentryAttachment(state, sentry);
    wrapper->setFilename(filename);
    wrapper->setSize(bytes.size());
    if (!contentType.isEmpty()) {
        wrapper->setContentType(contentType);
    }
    trackAttachment(wrapper);
    return wrapper;
}

bool SentrySdk::clearAttachments(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "clearAttachments", "clearing attachments")) {
        return false;
    }

    invalidateAttachments();
    return m_dsn.isEmpty() || callBridgeString("setAttachments", QStringLiteral("[]"));
}

void SentrySdk::trackAttachment(SentryAttachment *attachment)
{
    if (!attachment) {
        return;
    }

    m_attachments.append(attachment);
    updateAttachments();
}

void SentrySdk::detachAttachment(SentryAttachment *attachment)
{
    if (!attachment) {
        return;
    }

    const bool wasTracked = m_attachments.removeAll(attachment) > 0;
    delete static_cast<SentrySdkAttachmentState *>(attachment->handle());
    attachment->invalidate();
    if (wasTracked && m_initialized) {
        updateAttachments();
    }
}

void SentrySdk::invalidateAttachments()
{
    const QList<SentryAttachment *> attachments = m_attachments;
    m_attachments.clear();
    for (SentryAttachment *attachment : attachments) {
        if (!attachment) {
            continue;
        }
        delete static_cast<SentrySdkAttachmentState *>(attachment->handle());
        attachment->invalidate();
    }
}

void SentrySdk::updateAttachments()
{
    QList<SentrySdkAttachmentState> attachments;
    for (SentryAttachment *attachment : m_attachments) {
        auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr;
        if (state) {
            attachments.append(*state);
        }
    }

    if (!m_dsn.isEmpty()) {
        callBridgeString("setAttachments", jsonStringFromAttachments(attachments));
    }
}

void SentrySdk::setAttachmentFilename(SentryAttachment *attachment, const QString &filename)
{
    if (auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr) {
        state->filename = filename;
        if (m_attachments.contains(attachment)) {
            updateAttachments();
        }
    }
}

void SentrySdk::setAttachmentContentType(SentryAttachment *attachment, const QString &contentType)
{
    if (auto *state = attachment ? static_cast<SentrySdkAttachmentState *>(attachment->handle()) : nullptr) {
        state->contentType = contentType;
        if (m_attachments.contains(attachment)) {
            updateAttachments();
        }
    }
}

bool SentrySdk::removeAttachment(Sentry *sentry, SentryAttachment *attachment)
{
    if (!ensureCanCall(sentry, "removeAttachment", "removing attachments")) {
        return false;
    }

    if (!attachment || !attachment->handle()) {
        return true;
    }

    detachAttachment(attachment);
    return true;
}

bool SentrySdk::startSession(Sentry *sentry)
{
    if (!ensureCanCall(sentry, "startSession", "starting sessions")) {
        return false;
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent)) {
        return true;
    }

    return m_dsn.isEmpty() || callBridgeString("startSession", QString());
}

bool SentrySdk::endSession(Sentry *sentry, int)
{
    if (!ensureCanCall(sentry, "endSession", "ending sessions")) {
        return false;
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent)) {
        return true;
    }

    return m_dsn.isEmpty() || callBridgeString("endSession", QString());
}

bool SentrySdk::addBreadcrumb(Sentry *sentry, const QVariantMap &breadcrumb)
{
    if (!ensureCanCall(sentry, "addBreadcrumb", "adding breadcrumbs")) {
        return false;
    }

    QVariantMap nativeBreadcrumb = breadcrumb;
    const HookResult result = invokeValueHook(nativeBreadcrumb, m_beforeBreadcrumbState.get());
    if (result.action == HookResult::Drop) {
        return true;
    }
    if (result.action == HookResult::Replace) {
        nativeBreadcrumb = result.value.toMap();
    }

    if (m_maxBreadcrumbs > 0) {
        m_breadcrumbs.append(nativeBreadcrumb);
        while (m_breadcrumbs.size() > m_maxBreadcrumbs) {
            m_breadcrumbs.removeFirst();
        }
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent)) {
        return true;
    }

    return m_dsn.isEmpty() || callBridgeString("addBreadcrumb", jsonStringFromVariant(nativeBreadcrumb));
}

bool SentrySdk::log(Sentry *sentry, int level, const QString &message, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "log", "logging", "hooks")) {
        return false;
    }

    if (message.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry log message must not be empty."));
        return false;
    }

    QVariantMap log = {
        {QStringLiteral("body"), message},
        {QStringLiteral("message"), message},
        {QStringLiteral("level"), levelNameFromInt(level)},
        {QStringLiteral("attributes"), attributes},
    };
    const HookResult result = invokeValueHook(log, m_beforeSendLogState.get());
    if (result.action == HookResult::Drop) {
        return true;
    }
    if (result.action == HookResult::Replace) {
        log = result.value.toMap();
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent) || m_dsn.isEmpty()) {
        return true;
    }

    const QString nativeMessage =
        log.contains(QStringLiteral("body")) ? log.value(QStringLiteral("body")).toString()
                                             : log.value(QStringLiteral("message"), message).toString();
    const int nativeLevel = levelFromVariant(log.value(QStringLiteral("level")), level);
    return logBridge(nativeLevel, nativeMessage, log.value(QStringLiteral("attributes")).toMap());
}

bool SentrySdk::count(Sentry *sentry, const QString &name, qint64 value, const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "count", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (value < 0) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric count must not be negative."));
        return false;
    }

    QVariantMap metric = {
        {QStringLiteral("name"), name},
        {QStringLiteral("value"), value},
        {QStringLiteral("attributes"), attributes},
    };
    const HookResult result = invokeValueHook(metric, m_beforeSendMetricState.get());
    if (result.action == HookResult::Drop) {
        return true;
    }
    if (result.action == HookResult::Replace) {
        metric = result.value.toMap();
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent) || m_dsn.isEmpty()) {
        return true;
    }

    return countBridge(metric.value(QStringLiteral("name"), name).toString(),
                       metric.value(QStringLiteral("value"), value).toLongLong(),
                       metric.value(QStringLiteral("attributes")).toMap());
}

bool SentrySdk::gauge(Sentry *sentry,
                      const QString &name,
                      double value,
                      const QString &unit,
                      const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "gauge", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (!std::isfinite(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        return false;
    }

    QVariantMap metric = {
        {QStringLiteral("name"), name},
        {QStringLiteral("value"), value},
        {QStringLiteral("unit"), unit},
        {QStringLiteral("attributes"), attributes},
    };
    const HookResult result = invokeValueHook(metric, m_beforeSendMetricState.get());
    if (result.action == HookResult::Drop) {
        return true;
    }
    if (result.action == HookResult::Replace) {
        metric = result.value.toMap();
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent) || m_dsn.isEmpty()) {
        return true;
    }

    return metricBridge("gauge",
                        metric.value(QStringLiteral("name"), name).toString(),
                        metric.value(QStringLiteral("value"), value).toDouble(),
                        metric.value(QStringLiteral("unit"), unit).toString(),
                        metric.value(QStringLiteral("attributes")).toMap());
}

bool SentrySdk::distribution(Sentry *sentry,
                             const QString &name,
                             double value,
                             const QString &unit,
                             const QVariantMap &attributes)
{
    if (!ensureCanCall(sentry, "distribution", "recording metrics", "hooks")) {
        return false;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric name must not be empty."));
        return false;
    }

    if (!std::isfinite(value)) {
        emit sentry->errorOccurred(QStringLiteral("Sentry metric value must be finite."));
        return false;
    }

    QVariantMap metric = {
        {QStringLiteral("name"), name},
        {QStringLiteral("value"), value},
        {QStringLiteral("unit"), unit},
        {QStringLiteral("attributes"), attributes},
    };
    const HookResult result = invokeValueHook(metric, m_beforeSendMetricState.get());
    if (result.action == HookResult::Drop) {
        return true;
    }
    if (result.action == HookResult::Replace) {
        metric = result.value.toMap();
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent) || m_dsn.isEmpty()) {
        return true;
    }

    return metricBridge("distribution",
                        metric.value(QStringLiteral("name"), name).toString(),
                        metric.value(QStringLiteral("value"), value).toDouble(),
                        metric.value(QStringLiteral("unit"), unit).toString(),
                        metric.value(QStringLiteral("attributes")).toMap());
}

SentrySpan *SentrySdk::startTransaction(Sentry *sentry,
                                        const QString &name,
                                        const QString &operation,
                                        const QString &description,
                                        bool bindToScope,
                                        const QVariantMap &customSamplingContext)
{
    if (!ensureInitialized(sentry, "starting transactions")) {
        return nullptr;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry transaction name must not be empty."));
        return nullptr;
    }

    double sampleRate = -1.0;
    if (m_tracesSamplerState) {
        QVariantMap transactionContext;
        transactionContext.insert(QStringLiteral("name"), name);
        transactionContext.insert(QStringLiteral("operation"), operation);
        QVariantMap samplingContext;
        samplingContext.insert(QStringLiteral("transactionContext"), transactionContext);
        samplingContext.insert(QStringLiteral("customSamplingContext"), customSamplingContext);

        const HookResult result = invokeValueHook(samplingContext, m_tracesSamplerState.get());
        if (result.action == HookResult::Drop) {
            sampleRate = 0.0;
        } else if (result.action == HookResult::Replace) {
            sampleRate = result.value.toDouble();
        }
        if (sampleRate >= 0.0 && (!std::isfinite(sampleRate) || sampleRate > 1.0)) {
            emit sentry->errorOccurred(QStringLiteral("SentryOptions.tracesSampler must return a number between 0.0 and 1.0."));
            return nullptr;
        }
    }

    int handle = 0;
    if (!m_dsn.isEmpty() && hasConsent(m_requireUserConsent, m_userConsent)) {
        handle = startTransactionBridge(name, operation, description, bindToScope, customSamplingContext, sampleRate);
        if (!handle) {
            return nullptr;
        }
    }

    auto *state = new SentrySdkSpanState;
    state->handle = handle;
    state->transaction = true;
    auto *span = new SentrySpan(state, true, name, operation, description, sentry);
    trackSpan(span);
    return span;
}

SentrySpan *SentrySdk::startSpan(Sentry *sentry,
                                 const QString &name,
                                 const QString &operation,
                                 const QString &description,
                                 SentrySpan *parentSpan,
                                 bool bindToScope)
{
    if (!ensureInitialized(sentry, "starting spans")) {
        return nullptr;
    }

    if (name.isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry span name must not be empty."));
        return nullptr;
    }

    auto *parent = parentSpan ? static_cast<SentrySdkSpanState *>(parentSpan->handle()) : nullptr;
    if (parentSpan && !parent) {
        emit sentry->errorOccurred(QStringLiteral("Sentry.startSpan requires a valid parent span."));
        return nullptr;
    }

    int handle = 0;
    if (!m_dsn.isEmpty() && hasConsent(m_requireUserConsent, m_userConsent)) {
        handle = startSpanBridge(parent ? parent->handle : 0, name, operation, description, bindToScope);
        if (!handle) {
            emit sentry->errorOccurred(QStringLiteral("Sentry.startSpan requires a parent or active span."));
            return nullptr;
        }
    }

    auto *state = new SentrySdkSpanState;
    state->handle = handle;
    auto *span = new SentrySpan(state, false, name, operation, description, sentry);
    trackSpan(span);
    return span;
}

bool SentrySdk::finishSpan(SentrySpan *span, const QString &status)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    if (!state) {
        return false;
    }

    if (!status.isEmpty()) {
        setSpanStatus(span, status);
        span->setStatusLocally(status);
    }

    SentrySdkEventHookState *hookState =
        span->isTransaction() ? m_beforeSendTransactionState.get() : m_beforeSendSpanState.get();
    const HookResult result = invokeValueHook(span->toVariantMap(), hookState);
    if (result.action == HookResult::Drop) {
        detachSpan(span);
        return true;
    }
    if (result.action == HookResult::Replace) {
        const QVariantMap replacement = result.value.toMap();
        if (replacement.contains(QStringLiteral("status"))) {
            const QString replacementStatus = replacement.value(QStringLiteral("status")).toString();
            setSpanStatus(span, replacementStatus);
            span->setStatusLocally(replacementStatus);
        }
        const QVariantMap data = replacement.value(QStringLiteral("data")).toMap();
        for (auto it = data.cbegin(); it != data.cend(); ++it) {
            if (!it.key().isEmpty()) {
                setSpanData(span, it.key(), it.value());
                span->setDataLocally(it.key(), it.value());
            }
        }
        const QVariantMap tags = replacement.value(QStringLiteral("tags")).toMap();
        for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
            if (!it.key().isEmpty()) {
                setSpanTag(span, it.key(), it.value().toString());
                span->setTagLocally(it.key(), it.value().toString());
            }
        }
    }

    if (state->handle && !finishSpanBridge(state->handle, span->status())) {
        return false;
    }
    state->handle = 0;
    detachSpan(span);
    return true;
}

bool SentrySdk::setSpanStatus(SentrySpan *span, const QString &status)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    return state && (!state->handle || setSpanStatusBridge(state->handle, status));
}

bool SentrySdk::setSpanData(SentrySpan *span, const QString &key, const QVariant &value)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    return state && (!state->handle || setSpanDataBridge(state->handle, key, value));
}

bool SentrySdk::removeSpanData(SentrySpan *span, const QString &key)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    return state && (!state->handle || removeSpanDataBridge(state->handle, key));
}

bool SentrySdk::setSpanTag(SentrySpan *span, const QString &key, const QString &value)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    return state && (!state->handle || setSpanTagBridge(state->handle, key, value));
}

bool SentrySdk::removeSpanTag(SentrySpan *span, const QString &key)
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    return state && (!state->handle || removeSpanTagBridge(state->handle, key));
}

QVariantMap SentrySdk::spanTraceHeaders(const SentrySpan *span) const
{
    auto *state = span ? static_cast<SentrySdkSpanState *>(span->handle()) : nullptr;
    if (!state || !state->handle) {
        return {};
    }

    return spanTraceHeadersBridge(state->handle);
}

void SentrySdk::trackSpan(SentrySpan *span)
{
    if (span) {
        m_spans.append(span);
    }
}

void SentrySdk::detachSpan(SentrySpan *span)
{
    if (!span) {
        return;
    }

    m_spans.removeAll(span);
    auto *state = static_cast<SentrySdkSpanState *>(span->handle());
    if (state) {
        if (state->handle) {
            releaseSpanBridge(state->handle);
        }
        delete state;
    }
    span->invalidate();
}

void SentrySdk::invalidateSpans()
{
    const QList<SentrySpan *> spans = m_spans;
    m_spans.clear();
    for (SentrySpan *span : spans) {
        if (!span) {
            continue;
        }
        auto *state = static_cast<SentrySdkSpanState *>(span->handle());
        if (state) {
            if (state->handle) {
                releaseSpanBridge(state->handle);
            }
            delete state;
        }
        span->invalidate();
    }
}

QString SentrySdk::captureMessage(Sentry *sentry, const QString &message, const QString &level)
{
    if (!ensureInitialized(sentry, "capturing messages")) {
        return {};
    }

    if (message.isEmpty()) {
        if (sentry) {
            emit sentry->errorOccurred(QStringLiteral("Sentry message must not be empty."));
        }
        return {};
    }

    QVariantMap event = {
        {QStringLiteral("logger"), QStringLiteral("qml")},
        {QStringLiteral("message"),
         QVariantMap{
             {QStringLiteral("formatted"), message},
         }},
    };
    if (!level.isEmpty()) {
        event.insert(QStringLiteral("level"), SentryEvent::levelNameFromString(level));
    }

    return captureEvent(sentry, event, SentrySdkCaptureMode::Manual);
}

bool SentrySdk::captureFeedback(Sentry *sentry, const QVariantMap &feedback, SentryHint *hint)
{
    if (!ensureCanCall(sentry, "captureFeedback", "capturing feedback", "hooks")) {
        return false;
    }

    const QString message = feedbackValue(feedback, QStringLiteral("message"));
    if (message.trimmed().isEmpty()) {
        emit sentry->errorOccurred(QStringLiteral("Sentry feedback message must not be empty."));
        return false;
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent) || m_dsn.isEmpty()) {
        return true;
    }

    QList<SentrySdkAttachmentState> attachments;
    if (hint) {
        for (const SentryHintAttachment &attachment : hint->d->attachments) {
            attachments.append(hintAttachment(attachment));
        }
    }

    return captureFeedbackBridge(feedback, attachments);
}

QString SentrySdk::captureEvent(Sentry *sentry, const QVariantMap &event, SentrySdkCaptureMode mode)
{
    if (hookDepth > 0) {
        if (mode == SentrySdkCaptureMode::Manual && sentry) {
            emit sentry->errorOccurred(
                QStringLiteral("Sentry.capture* cannot be called from Sentry event hooks."));
        }
        return {};
    }

    if (!m_initialized) {
        return {};
    }

    QVariantMap nativeEvent = event;
    applyLocalScopeToEvent(&nativeEvent);
    applyFingerprintToEvent(&nativeEvent);

    const HookResult result = invokeValueHook(nativeEvent, m_beforeSendState.get());
    if (result.action == HookResult::Drop) {
        if (!m_dsn.isEmpty() && hasConsent(m_requireUserConsent, m_userConsent)) {
            recordBeforeSendErrorBridge();
        }
        return {};
    }
    if (result.action == HookResult::Replace) {
        nativeEvent = result.value.toMap();
    }

    if (!hasConsent(m_requireUserConsent, m_userConsent)) {
        return generatedEventId();
    }

    QList<SentrySdkAttachmentState> attachments;
    if (m_attachScreenshot) {
        const QByteArray screenshot = SentryScreenshot::toPng();
        if (!screenshot.isEmpty()) {
            attachments.append(screenshotAttachment(screenshot));
        }
    }
    if (m_attachViewHierarchy) {
        const QByteArray viewHierarchy = SentryViewHierarchy::toJson();
        if (!viewHierarchy.isEmpty()) {
            attachments.append(viewHierarchyAttachment(viewHierarchy));
        }
    }

    if (m_dsn.isEmpty()) {
        return generatedEventId();
    }

    return captureEventBridge(nativeEvent, attachments);
}

void SentrySdk::clearLocalScope()
{
    m_dsn.clear();
    m_release.clear();
    m_environment.clear();
    m_dist.clear();
    m_level.clear();
    m_transaction.clear();
    m_user.clear();
    m_tags.clear();
    m_contexts.clear();
    m_breadcrumbs.clear();
    m_fingerprint.clear();
    m_maxBreadcrumbs = 100;
    m_attachScreenshot = false;
    m_attachViewHierarchy = false;
    m_requireUserConsent = false;
    m_userConsent = -1;
}

void SentrySdk::applyLocalScopeToEvent(QVariantMap *event) const
{
    if (!event) {
        return;
    }

    if (!m_release.isEmpty() && !event->contains(QStringLiteral("release"))) {
        event->insert(QStringLiteral("release"), m_release);
    }
    if (!m_environment.isEmpty() && !event->contains(QStringLiteral("environment"))) {
        event->insert(QStringLiteral("environment"), m_environment);
    }
    if (!m_dist.isEmpty() && !event->contains(QStringLiteral("dist"))) {
        event->insert(QStringLiteral("dist"), m_dist);
    }
    if (!m_level.isEmpty() && !event->contains(QStringLiteral("level"))) {
        event->insert(QStringLiteral("level"), m_level);
    }
    if (!m_transaction.isEmpty() && !event->contains(QStringLiteral("transaction"))) {
        event->insert(QStringLiteral("transaction"), m_transaction);
    }
    if (!m_user.isEmpty() && !event->contains(QStringLiteral("user"))) {
        event->insert(QStringLiteral("user"), m_user);
    }
    if (!m_tags.isEmpty()) {
        QVariantMap tags = m_tags;
        const QVariantMap eventTags = event->value(QStringLiteral("tags")).toMap();
        for (auto it = eventTags.cbegin(); it != eventTags.cend(); ++it) {
            tags.insert(it.key(), it.value());
        }
        event->insert(QStringLiteral("tags"), tags);
    }
    if (!m_contexts.isEmpty()) {
        QVariantMap contexts = m_contexts;
        const QVariantMap eventContexts = event->value(QStringLiteral("contexts")).toMap();
        for (auto it = eventContexts.cbegin(); it != eventContexts.cend(); ++it) {
            contexts.insert(it.key(), it.value());
        }
        event->insert(QStringLiteral("contexts"), contexts);
    }
    if (!m_breadcrumbs.isEmpty() && !event->contains(QStringLiteral("breadcrumbs"))) {
        event->insert(QStringLiteral("breadcrumbs"), m_breadcrumbs);
    }
}

void SentrySdk::applyFingerprintToEvent(QVariantMap *event) const
{
    if (!event || m_fingerprint.isEmpty()) {
        return;
    }

    event->insert(QStringLiteral("fingerprint"), m_fingerprint);
}

void SentrySdk::setInitialized(bool initialized)
{
    if (m_initialized == initialized) {
        return;
    }

    m_initialized = initialized;
    emit initializedChanged();
}
