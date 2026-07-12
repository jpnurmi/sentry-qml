pragma Singleton

import QtQml
import Sentry 1.0

QtObject {
    property bool enabled: false
    property var transaction: null
    property var spans: []
    property bool valid: true

    function start(name, operation, description, customSamplingContext) {
        finish("cancelled");
        if (!enabled) {
            valid = true;
            return true;
        }

        valid = true;
        spans = [];
        transaction = Sentry.startTransaction(
            name,
            operation,
            description || "",
            true,
            customSamplingContext || {}
        );
        valid = !!transaction && transaction.valid;
        return valid;
    }

    function begin(name, operation, description) {
        if (!enabled)
            return null;
        if (!transaction && !start("Example runtime", "function", "Automatic runtime trace", { source: "example" }))
            return null;

        const parent = spans.length > 0 ? spans[spans.length - 1] : transaction;
        if (!parent || !parent.valid) {
            valid = false;
            return null;
        }

        const span = Sentry.startSpan(name, operation || "function", description || name, parent, true);
        if (!span || !span.valid) {
            valid = false;
            return null;
        }

        const stack = spans.slice();
        stack.push(span);
        spans = stack;
        return span;
    }

    function end(status) {
        if (spans.length === 0) {
            valid = false;
            return false;
        }

        const stack = spans.slice();
        const span = stack.pop();
        spans = stack;

        const finished = !!span && span.finish(status || "ok");
        valid = finished && valid;
        return finished;
    }

    function withSpan(name, callback, operation, description) {
        const span = begin(name, operation || "function", description || name);
        let status = "ok";
        try {
            return callback(span);
        } catch (error) {
            status = "internal_error";
            if (span)
                valid = span.setData("error", String(error)) && valid;
            throw error;
        } finally {
            if (span)
                end(status);
        }
    }

    function finish(status) {
        let finished = valid;
        while (spans.length > 0)
            finished = end(status || "cancelled") && finished;

        if (transaction) {
            const active = transaction;
            transaction = null;
            finished = active.finish(status || (finished ? "ok" : "internal_error")) && finished;
        }

        spans = [];
        valid = finished;
        return finished;
    }

    function flush(status, timeoutMs) {
        const finished = finish(status);
        return Sentry.flush(timeoutMs || 2000) && finished;
    }
}
