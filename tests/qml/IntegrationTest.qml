import QtQml
import Sentry 1.0

QtObject {
    property bool initialized: false
    property bool releaseSet: false
    property bool environmentSet: false
    property bool userSet: false
    property bool tagSet: false
    property bool removedTagSet: false
    property bool tagRemoved: false
    property bool contextSet: false
    property bool removedContextSet: false
    property bool contextRemoved: false
    property bool fingerprintSet: false
    property bool breadcrumbAdded: false
    property bool fileAttached: false
    property bool bytesAttached: false
    property bool feedbackFileAttached: false
    property bool feedbackBytesAttached: false
    property bool bareFeedbackCaptured: false
    property bool feedbackCaptured: false
    property bool sessionStarted: false
    property bool sessionEnded: false
    property bool beforeBreadcrumbCalled: false
    property bool beforeSendCalled: false
    property bool beforeSendLogCalled: false
    property bool beforeSendMetricCalled: false
    property bool genericMetricCaptured: false
    property bool countCaptured: false
    property bool gaugeCaptured: false
    property bool distributionCaptured: false
    property bool logCaptured: false
    property bool enumLogCaptured: false
    property bool uncaughtTriggered: false
    property bool flushed: false
    property bool closed: false
    property bool fileInvalidated: false
    property bool bytesInvalidated: false
    property string declarativeEventId: ""
    property string messageEventId: ""
    property string exceptionEventId: ""
    property int feedbackAttachmentCount: 0
    property var fileAttachment: null
    property var bytesAttachment: null
    property SentryHint feedbackHint: SentryHint {}
    property SentryOptions options: SentryOptions {
        dsn: testDsn
        databasePath: testDatabasePath
        release: "sentry-qml@declarative"
        environment: "declarative"
        dist: "42"
        autoSessionTracking: false
        enableLogs: true
        enableMetrics: true
        maxBreadcrumbs: 10
        shutdownTimeout: 2000
        user: SentryUser {
            userId: "declarative-user"
            username: "ada"
            email: "ada@example.com"
            ipAddress: "127.0.0.1"
            data: ({ role: "admin" })
        }
        beforeBreadcrumb: function(breadcrumb) {
            beforeBreadcrumbCalled = true
            breadcrumb.data.hook = "beforeBreadcrumb"
            return breadcrumb
        }
        beforeSend: function(event) {
            beforeSendCalled = true
            event.tags = event.tags || {}
            event.tags.before_send = "qml"
            return event
        }
        beforeSendLog: function(log) {
            beforeSendLogCalled = true
            log.attributes["qml.integration.before_send_log"] = {
                type: "boolean",
                value: true
            }
            return log
        }
        beforeSendMetric: function(metric) {
            beforeSendMetricCalled = true
            metric.attributes["qml.integration.before_send_metric"] = {
                type: "boolean",
                value: true
            }
            return metric
        }
    }

    Component.onCompleted: {
        initialized = Sentry.init(options)
        declarativeEventId = Sentry.captureMessage("Declarative options integration message")
        releaseSet = Sentry.setRelease("sentry-qml@runtime")
        environmentSet = Sentry.setEnvironment("runtime")
        userSet = Sentry.setUser({
            id: "integration-user",
            username: "grace",
            email: "grace@example.com",
            ipAddress: "127.0.0.2",
            role: "operator"
        })
        tagSet = Sentry.setTag("screen", "integration")
        removedTagSet = Sentry.setTag("removed_tag", "yes")
        tagRemoved = Sentry.removeTag("removed_tag")
        contextSet = Sentry.setContext("qml", {
            screen: "integration",
            retries: 3,
            nested: { enabled: true }
        })
        removedContextSet = Sentry.setContext("removed_context", { present: true })
        contextRemoved = Sentry.removeContext("removed_context")
        fingerprintSet = Sentry.setFingerprint(["{{ default }}", "integration"])
        breadcrumbAdded = Sentry.addBreadcrumb({
            message: "integration breadcrumb",
            category: "navigation",
            type: "user",
            level: "info",
            data: { screen: "integration" }
        })
        fileAttachment = Sentry.attachFile(testAttachmentPath, "text/plain")
        fileAttached = !!fileAttachment && fileAttachment.valid
        bytesAttachment = Sentry.attachBytes("integration bytes payload", "inline.txt", "text/plain")
        bytesAttached = !!bytesAttachment && bytesAttachment.valid
        feedbackFileAttached = feedbackHint.attachFile(testAttachmentPath, "text/plain", "feedback-diagnostic.log")
        feedbackBytesAttached = feedbackHint.attachBytes("integration feedback bytes payload", "feedback-inline.txt", "text/plain")
        feedbackAttachmentCount = feedbackHint.attachmentCount
        sessionStarted = Sentry.startSession()
        messageEventId = Sentry.captureMessage("Integration message", "warning")
        bareFeedbackCaptured = Sentry.captureFeedback({
            message: "Integration bare feedback",
            email: "bare-feedback@example.com"
        })
        feedbackCaptured = Sentry.captureFeedback({
            message: "Integration feedback",
            email: "feedback@example.com",
            name: "Feedback User",
            associatedEventId: messageEventId
        }, feedbackHint)
        try {
            throw new Error("Integration exception")
        } catch (exception) {
            exceptionEventId = Sentry.captureException(exception)
        }
        logCaptured = Sentry.warn("Integration log", {
            "qml.integration.screen": "settings"
        })
        enumLogCaptured = Sentry.log(Sentry.Info, "Integration enum log", {
            "qml.integration.screen": "details"
        })
        genericMetricCaptured = Sentry.metric(Sentry.Count, "qml.integration.generic", 2, "", {})
        countCaptured = Sentry.count("qml.integration.clicks", 3, {
            "qml.integration.screen": "settings"
        })
        gaugeCaptured = Sentry.gauge("qml.integration.active_items", 4, "item", {})
        distributionCaptured = Sentry.distribution("qml.integration.duration", 12.5, "millisecond", {})
        sessionEnded = Sentry.endSession(Sentry.SessionExited)
    }

    function triggerUncaughtQmlError() {
        uncaughtTriggered = true
        missingIntegrationFunction()
    }

    function finish() {
        flushed = Sentry.flush(2000)
        closed = Sentry.close()
        fileInvalidated = !!fileAttachment && !fileAttachment.valid
        bytesInvalidated = !!bytesAttachment && !bytesAttachment.valid
    }
}
