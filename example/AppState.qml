pragma Singleton

import QtCore
import QtQuick

QtObject {
    id: state

    property int captureMode: 0
    property int captureLevelIndex: 0
    property int exceptionKindIndex: 1
    property int breadcrumbCategoryIndex: 0
    property int scopeTab: 0
    property int crashKindIndex: 0
    property int statusSeverity: 0
    property bool statusOk: false
    property string statusMessage: qsTr("Ready")

    readonly property string defaultDatabasePath: toLocalPath(StandardPaths.writableLocation(StandardPaths.CacheLocation)) + "/sentry"

    property string dsn: ""
    property string databasePath: defaultDatabasePath
    property string messageText: ""
    property string release: qsTr("sentry-qml-example@0.1.0")
    property string environment: qsTr("qml")
    property string dist: ""
    property real sampleRate: 1.0
    property int maxBreadcrumbs: 100
    property int shutdownTimeout: 2000
    property bool debugEnabled: false
    property bool logsEnabled: true
    property bool metricsEnabled: true
    property bool autoSessionTrackingEnabled: true
    property bool requireUserConsentEnabled: false
    property bool screenshotEnabled: false
    property bool viewHierarchyEnabled: false
    property string tagKey: qsTr("backend")
    property string tagValue: qsTr("qml")
    property string contextKey: qsTr("example")
    property string contextValue: qsTr("qml")
    property string userId: ""
    property string username: ""
    property string email: ""
    property string ipAddress: ""
    property string sessionRelease: release
    property string sessionEnvironment: environment
    property bool sessionActive: false

    onDatabasePathChanged: {
        const path = toLocalPath(databasePath);
        if (path !== databasePath)
            databasePath = path;
    }

    property Settings settings: Settings {
        category: "sentry"
        property alias dsn: state.dsn
        property alias databasePath: state.databasePath
        property alias message: state.messageText
        property alias release: state.release
        property alias environment: state.environment
        property alias dist: state.dist
        property alias debug: state.debugEnabled
        property alias logs: state.logsEnabled
        property alias metrics: state.metricsEnabled
        property alias autoSessionTracking: state.autoSessionTrackingEnabled
        property alias requireUserConsent: state.requireUserConsentEnabled
        property alias screenshot: state.screenshotEnabled
        property alias viewHierarchy: state.viewHierarchyEnabled
        property alias sampleRate: state.sampleRate
        property alias maxBreadcrumbs: state.maxBreadcrumbs
        property alias shutdownTimeout: state.shutdownTimeout
        property alias sessionRelease: state.sessionRelease
        property alias sessionEnvironment: state.sessionEnvironment
    }

    function captureLevel() {
        if (captureLevelIndex === 1)
            return "warning";
        if (captureLevelIndex === 2)
            return "error";
        return "info";
    }

    function exceptionKind() {
        const kinds = ["native", "qml"];
        return kinds[Math.max(0, Math.min(exceptionKindIndex, kinds.length - 1))];
    }

    function breadcrumbCategory() {
        const categories = ["default", "debug", "info", "navigation", "http", "query", "transaction", "ui", "user", "error"];
        return categories[Math.max(0, Math.min(breadcrumbCategoryIndex, categories.length - 1))];
    }

    function toLocalPath(value) {
        let path = String(value);
        if (path.startsWith("file://")) {
            path = decodeURIComponent(path.substring(7));
            if (Qt.platform.os === "windows" && path.length > 2 && path[0] === "/" && path[2] === ":")
                path = path.substring(1);
        }
        return path;
    }

    function toFileUrl(value) {
        let path = String(value);
        if (path.length === 0)
            return StandardPaths.writableLocation(StandardPaths.CacheLocation);
        if (path.startsWith("file://"))
            return path;
        if (Qt.platform.os === "windows")
            return "file:///" + path.replace(/\\/g, "/");
        return "file://" + path;
    }

    function setStatus(message, ok, severity) {
        statusMessage = message;
        statusOk = ok;
        statusSeverity = severity === undefined ? ok ? 1 : 2 : severity;
    }
}
