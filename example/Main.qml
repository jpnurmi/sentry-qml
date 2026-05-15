import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Sentry 1.0

import "controls"

ApplicationWindow {
    id: window

    width: 1040
    height: 720
    minimumWidth: 320
    minimumHeight: 480
    visible: true
    title: qsTr("Sentry QML")
    color: AppTheme.background

    readonly property bool compact: width < 900
    readonly property int pageMargin: compact ? 14 : 20
    readonly property int panelMargin: compact ? 14 : 18
    readonly property int controlHeight: 42
    readonly property int actionWidth: Math.max(152, Math.ceil(Math.max(initializeActionMetrics.width + 28, reinitializeActionMetrics.width + 28, sendActionMetrics.width + 28, addActionMetrics.width + 28, crashActionMetrics.width + 28)))
    property var attachmentHandles: []

    TextMetrics {
        id: initializeActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Initialize")
    }

    TextMetrics {
        id: reinitializeActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Re-initialize")
    }

    TextMetrics {
        id: sendActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Send")
    }

    TextMetrics {
        id: addActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Add")
    }

    TextMetrics {
        id: crashActionMetrics

        font.pixelSize: 14
        font.weight: Font.DemiBold
        text: qsTr("Crash")
    }

    SentryOptions {
        id: sentryOptions

        dsn: AppState.dsn
        databasePath: AppState.databasePath
        release: AppState.release
        environment: AppState.environment
        dist: AppState.dist
        debug: AppState.debugEnabled
        enableLogs: AppState.logsEnabled
        enableMetrics: AppState.metricsEnabled
        autoSessionTracking: AppState.autoSessionTrackingEnabled
        requireUserConsent: AppState.requireUserConsentEnabled
        attachViewHierarchy: AppState.viewHierarchyEnabled
        sampleRate: AppState.sampleRate
        maxBreadcrumbs: AppState.maxBreadcrumbs
        shutdownTimeout: AppState.shutdownTimeout
        user: SentryUser {
            userId: AppState.userId
            username: AppState.username
            email: AppState.email
            ipAddress: AppState.ipAddress
        }
        beforeSend: function (event) {
            console.log("### beforeSend");
            event.extra = event.extra || {};
            event.extra.example = "sentry-qml";
            return event;
        }
        onCrash: function (event) {
            console.log("### onCrash");
            event.extra = event.extra || {};
            event.extra.exampleCrash = true;
            return event;
        }
    }

    ListModel {
        id: tagEntries

        ListElement {
            entryKey: "backend"
            entryValue: "qml"
        }
    }

    ListModel {
        id: contextEntries

        ListElement {
            entryKey: "example"
            entryValue: "qml"
        }
    }

    ListModel {
        id: attachmentEntries
    }

    function statusSeverity(message, ok) {
        return ok ? 1 : 2;
    }

    function setStatus(message, ok, severity) {
        AppState.setStatus(message, ok, severity === undefined ? statusSeverity(message, ok) : severity);
    }

    function isInitializeStatus(message) {
        return message === qsTr("Initialization failed") || message === qsTr("Re-initialization failed");
    }

    function resetRuntimeStatus() {
        if (!isInitializeStatus(AppState.statusMessage))
            setStatus(qsTr("Ready"), Sentry.initialized, Sentry.initialized ? 1 : 0);
    }

    function globalStatusText() {
        if (isInitializeStatus(AppState.statusMessage))
            return AppState.statusMessage;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusMessage;
        return Sentry.initialized ? qsTr("Ready") : qsTr("Not initialized");
    }

    function globalStatusSeverity() {
        if (isInitializeStatus(AppState.statusMessage))
            return 2;
        if (AppState.statusMessage !== qsTr("Ready"))
            return AppState.statusSeverity;
        return Sentry.initialized ? 1 : 0;
    }

    function upsertEntry(model, key, value) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i).entryKey === key) {
                model.set(i, {
                    entryKey: key,
                    entryValue: value
                });
                return;
            }
        }
        model.append({
            entryKey: key,
            entryValue: value
        });
    }

    function fileNameFromPath(path) {
        const localPath = AppState.toLocalPath(path);
        const normalized = localPath.replace(/\\/g, "/");
        const index = normalized.lastIndexOf("/");
        return index >= 0 ? normalized.substring(index + 1) : normalized;
    }

    function formattedAttachmentSize(size) {
        const bytes = Number(size);
        return bytes >= 0 ? Qt.locale().formattedDataSize(bytes) : qsTr("Unknown");
    }

    function clearAttachmentEntries() {
        attachmentHandles = [];
        attachmentEntries.clear();
    }

    function addAttachment(path) {
        if (!ensureInitialized())
            return;

        const localPath = AppState.toLocalPath(path);
        const attachment = Sentry.attachFile(localPath);
        const ok = attachment && attachment.valid;
        if (ok) {
            const handles = attachmentHandles.slice();
            handles.push(attachment);
            attachmentHandles = handles;
            const filename = String(attachment.filename || "");
            attachmentEntries.append({
                entryKey: filename.length > 0 ? filename : fileNameFromPath(localPath),
                entrySize: formattedAttachmentSize(attachment.size)
            });
        }
        setStatus(ok ? qsTr("Attachment added") : qsTr("Attachment was not added"), ok);
    }

    function removeAttachmentAt(index) {
        if (!ensureInitialized())
            return;

        const attachment = attachmentHandles[index];
        const ok = attachment && Sentry.removeAttachment(attachment);
        if (ok) {
            const handles = attachmentHandles.slice();
            handles.splice(index, 1);
            attachmentHandles = handles;
            attachmentEntries.remove(index);
        }
        setStatus(ok ? qsTr("Attachment removed") : qsTr("Attachment was not removed"), ok);
    }

    function initializeSentry() {
        if (Sentry.initialized && !Sentry.close()) {
            setStatus(qsTr("Re-initialization failed"), false);
            return false;
        }

        const ok = Sentry.init(sentryOptions);
        if (ok) {
            AppState.sessionActive = false;
            clearAttachmentEntries();
            setStatus(qsTr("Ready"), true);
            if (pageStack.depth === 1)
                pageStack.push(runtimePageComponent);
        } else {
            setStatus(qsTr("Initialization failed"), false);
        }
        return ok;
    }

    function ensureInitialized() {
        return Sentry.initialized || initializeSentry();
    }

    function capture() {
        if (!ensureInitialized())
            return;
        if (AppState.captureMode === 1) {
            captureException();
        } else if (AppState.captureMode === 2) {
            addBreadcrumb();
        } else {
            captureMessage();
        }
    }

    function captureMessage() {
        const eventId = Sentry.captureMessage(AppState.messageText, AppState.captureLevel());
        setStatus(eventId.length > 0 ? qsTr("Captured event %1").arg(eventId) : qsTr("Message was not captured"), eventId.length > 0);
    }

    function applyScope() {
        if (!ensureInitialized())
            return;
        if (AppState.scopeTab === 0) {
            const tagKey = AppState.tagKey.trim();
            if (tagKey.length === 0) {
                setStatus(qsTr("Tag key is required"), false);
                return;
            }

            const ok = Sentry.setTag(tagKey, AppState.tagValue);
            if (ok)
                upsertEntry(tagEntries, tagKey, AppState.tagValue);
            setStatus(ok ? qsTr("Tag added") : qsTr("Tag was not added"), ok);
        } else if (AppState.scopeTab === 1) {
            const contextKey = AppState.contextKey.trim();
            if (contextKey.length === 0) {
                setStatus(qsTr("Context key is required"), false);
                return;
            }

            const ok = Sentry.setContext(contextKey, {
                value: AppState.contextValue,
                messageLength: AppState.messageText.length
            });
            if (ok)
                upsertEntry(contextEntries, contextKey, AppState.contextValue);
            setStatus(ok ? qsTr("Context added") : qsTr("Context was not added"), ok);
        }
    }

    function syncUser() {
        if (!Sentry.initialized)
            return;
        const ok = Sentry.setUser({
            id: AppState.userId,
            username: AppState.username,
            email: AppState.email,
            ipAddress: AppState.ipAddress
        });
        if (!ok)
            setStatus(qsTr("User was not updated"), false);
    }

    function consentFooterColor() {
        if (!Sentry.userConsentRequired)
            return AppTheme.surfaceRaised;
        if (Sentry.userConsent === Sentry.UserConsentGiven)
            return AppTheme.success;
        if (Sentry.userConsent === Sentry.UserConsentRevoked)
            return AppTheme.critical;
        return "#9b6b17";
    }

    function consentFooterText() {
        if (!Sentry.userConsentRequired)
            return qsTr("Not required");
        if (Sentry.userConsent === Sentry.UserConsentGiven)
            return qsTr("Given — events will be sent to Sentry");
        if (Sentry.userConsent === Sentry.UserConsentRevoked)
            return qsTr("Revoked — events will not be sent to Sentry");
        return qsTr("Unknown — events will not be sent to Sentry");
    }

    function toggleUserConsent() {
        if (!Sentry.userConsentRequired) {
            setStatus(qsTr("Consent is not required"), false);
            return;
        }

        const giveConsent = Sentry.userConsent !== Sentry.UserConsentGiven;
        const ok = giveConsent ? Sentry.giveUserConsent() : Sentry.revokeUserConsent();
        if (ok)
            setStatus(giveConsent ? qsTr("User consent given") : qsTr("User consent revoked"), true, giveConsent ? 1 : 2);
        else
            setStatus(giveConsent ? qsTr("User consent was not given") : qsTr("User consent was not revoked"), false);
    }

    function sendFeedback(name, email, message) {
        if (!ensureInitialized())
            return false;

        const feedbackMessage = message.trim();
        if (feedbackMessage.length === 0) {
            setStatus(qsTr("Feedback message is required"), false);
            return false;
        }

        const feedback = {
            message: feedbackMessage
        };
        const feedbackName = name.trim();
        const feedbackEmail = email.trim();
        if (feedbackName.length > 0)
            feedback.name = feedbackName;
        if (feedbackEmail.length > 0)
            feedback.email = feedbackEmail;

        const ok = Sentry.captureFeedback(feedback);
        setStatus(ok ? qsTr("Feedback sent") : qsTr("Feedback was not sent"), ok);
        return ok;
    }

    function startSession() {
        if (!ensureInitialized())
            return;
        let ok = true;
        const release = AppState.sessionRelease.trim();
        const environment = AppState.sessionEnvironment.trim();

        if (release.length > 0)
            ok = Sentry.setRelease(release) && ok;
        if (environment.length > 0)
            ok = Sentry.setEnvironment(environment) && ok;

        ok = Sentry.startSession() && ok;
        if (ok)
            AppState.sessionActive = true;
        setStatus(ok ? qsTr("Session started") : qsTr("Session was not started"), ok);
    }

    function endSession() {
        if (!ensureInitialized())
            return;
        const ok = Sentry.endSession(Sentry.SessionExited);
        if (ok)
            AppState.sessionActive = false;
        setStatus(ok ? qsTr("Session ended") : qsTr("Session was not ended"), ok);
    }

    function toggleSession() {
        if (AppState.sessionActive)
            endSession();
        else
            startSession();
    }

    function addBreadcrumb() {
        const ok = Sentry.addBreadcrumb({
            message: AppState.messageText,
            category: AppState.breadcrumbCategory(),
            type: "manual",
            level: AppState.captureLevel(),
            data: {
                message: AppState.messageText
            }
        });
        setStatus(ok ? qsTr("Breadcrumb added") : qsTr("Breadcrumb was not added"), ok);
    }

    function captureException() {
        if (AppState.exceptionKind() === "native") {
            setStatus(qsTr("Native exception capture is not available"), false);
            return;
        }

        try {
            throw new Error(AppState.messageText);
        } catch (exception) {
            const eventId = Sentry.captureException(exception);
            setStatus(eventId.length > 0 ? qsTr("Captured exception %1").arg(eventId) : qsTr("Exception was not captured"), eventId.length > 0);
        }
    }

    function triggerQmlError() {
        callMissingQmlFunction();
    }

    function triggerCrash() {
        if (AppState.crashKindIndex === 0) {
            setStatus(qsTr("Crashing..."), false);
            exampleActions.crash();
        } else {
            setStatus(qsTr("Triggering QML error..."), false);
            triggerQmlError();
        }
    }

    function callMissingQmlFunction() {
        missingQmlFunction();
    }

    Connections {
        target: Sentry

        function onErrorOccurred(message) {
            setStatus(message, false);
        }
    }

    Popup {
        id: scopeEditorPopup

        property int scopeTab: 0

        function openFor(tab) {
            scopeTab = tab;
            scopeKeyField.text = "";
            scopeValueField.text = "";
            open();
        }

        x: (window.width - width) / 2
        y: Math.max(window.pageMargin, (window.height - height) / 2)
        width: Math.min(window.width - window.pageMargin * 2, 420)
        modal: true
        focus: true
        padding: window.panelMargin
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                text: scopeEditorPopup.scopeTab === 0 ? qsTr("Add tag") : qsTr("Add context")
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            LabeledTextField {
                id: scopeKeyField

                label: scopeEditorPopup.scopeTab === 0 ? qsTr("Key") : qsTr("Context")
                placeholderText: scopeEditorPopup.scopeTab === 0 ? qsTr("feature") : qsTr("device")
            }

            LabeledTextField {
                id: scopeValueField

                label: qsTr("Value")
                placeholderText: scopeEditorPopup.scopeTab === 0 ? qsTr("checkout") : qsTr("mobile")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")

                    onClicked: {
                        scopeEditorPopup.close();
                    }
                }

                ActionButton {
                    text: qsTr("Add")
                    primary: true

                    onClicked: {
                        if (scopeEditorPopup.scopeTab === 0) {
                            AppState.tagKey = scopeKeyField.text;
                            AppState.tagValue = scopeValueField.text;
                        } else {
                            AppState.contextKey = scopeKeyField.text;
                            AppState.contextValue = scopeValueField.text;
                        }
                        AppState.scopeTab = scopeEditorPopup.scopeTab;
                        applyScope();
                        scopeEditorPopup.close();
                    }
                }
            }
        }

        background: Rectangle {
            color: AppTheme.surface
            border.color: AppTheme.border
            radius: 8
        }
    }

    FileDialog {
        id: attachmentFileDialog

        title: qsTr("Attach file")
        currentFolder: AppState.toFileUrl(AppState.databasePath)

        onAccepted: {
            addAttachment(selectedFile);
        }
    }

    Popup {
        id: feedbackPopup

        function openFeedback() {
            feedbackNameField.text = "";
            feedbackEmailField.text = AppState.email;
            feedbackMessageArea.text = "";
            open();
        }

        x: (window.width - width) / 2
        y: Math.max(window.pageMargin, (window.height - height) / 2)
        width: Math.min(window.width - window.pageMargin * 2, 520)
        modal: true
        focus: true
        padding: window.panelMargin
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                text: qsTr("Feedback")
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: window.compact ? 1 : 2
                uniformCellWidths: true
                rowSpacing: 10
                columnSpacing: 10

                LabeledTextField {
                    id: feedbackNameField

                    label: qsTr("Name")
                    placeholderText: qsTr("Jane")
                }

                LabeledTextField {
                    id: feedbackEmailField

                    label: qsTr("Email")
                    placeholderText: qsTr("jane@example.com")
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: qsTr("Message")
                    color: AppTheme.muted
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                TextArea {
                    id: feedbackMessageArea

                    placeholderText: qsTr("What happened?")
                    placeholderTextColor: AppTheme.subtle
                    color: AppTheme.text
                    selectedTextColor: AppTheme.text
                    selectionColor: AppTheme.accent
                    font.pixelSize: 15
                    wrapMode: TextArea.Wrap
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 10
                    bottomPadding: 10
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(220, Math.max(130, window.height * 0.24))

                    background: Rectangle {
                        color: feedbackMessageArea.activeFocus ? AppTheme.inputFocus : AppTheme.input
                        border.color: feedbackMessageArea.activeFocus ? AppTheme.accent : AppTheme.border
                        border.width: feedbackMessageArea.activeFocus ? 2 : 1
                        radius: 7
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item {
                    Layout.fillWidth: true
                }

                ActionButton {
                    text: qsTr("Cancel")

                    onClicked: {
                        feedbackPopup.close();
                    }
                }

                ActionButton {
                    text: qsTr("Send")
                    primary: true
                    enabled: feedbackMessageArea.text.trim().length > 0

                    onClicked: {
                        if (sendFeedback(feedbackNameField.text, feedbackEmailField.text, feedbackMessageArea.text))
                            feedbackPopup.close();
                    }
                }
            }
        }

        background: Rectangle {
            color: AppTheme.surface
            border.color: AppTheme.border
            radius: 8
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackView {
            id: pageStack

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            initialItem: initializePageComponent
        }
    }

    Popup {
        id: globalStatusPopup

        parent: Overlay.overlay
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        padding: 0
        visible: true
        width: globalStatusPill.implicitWidth
        height: globalStatusPill.implicitHeight
        x: parent ? Math.max(window.pageMargin, parent.width - width - window.pageMargin) : window.pageMargin
        y: window.pageMargin
        background: Item {}
        contentItem: Banner {
            id: globalStatusPill

            severity: globalStatusSeverity()
            text: globalStatusText()
        }
    }

    Component {
        id: initializePageComponent

        InitPage {
            width: pageStack.width
            height: pageStack.height
            compact: window.compact
            initialized: Sentry.initialized
            pageMargin: window.pageMargin
            panelMargin: window.panelMargin

            onInitializeRequested: {
                initializeSentry();
            }
        }
    }

    Component {
        id: runtimePageComponent

        RuntimePage {
            width: pageStack.width
            height: pageStack.height
            compact: window.compact
            pageMargin: window.pageMargin
            panelMargin: window.panelMargin
            controlHeight: window.controlHeight
            actionWidth: window.actionWidth
            consentText: consentFooterText()
            consentColor: consentFooterColor()
            tagModel: tagEntries
            contextModel: contextEntries
            attachmentModel: attachmentEntries

            onBackRequested: {
                resetRuntimeStatus();
                pageStack.pop();
            }
            onFeedbackRequested: feedbackPopup.openFeedback()
            onCaptureRequested: capture()
            onAddScopeItemRequested: function(tab) {
                scopeEditorPopup.openFor(tab);
            }
            onAddAttachmentRequested: attachmentFileDialog.open()
            onSyncUserRequested: syncUser()
            onToggleSessionRequested: toggleSession()
            onTriggerCrashRequested: triggerCrash()
            onToggleUserConsentRequested: toggleUserConsent()
            onRemoveScopeEntryRequested: function(scopeTab, index, key, entryModel) {
                if (!ensureInitialized())
                    return;

                const ok = scopeTab === 0 ? Sentry.removeTag(key) : Sentry.removeContext(key);
                if (ok)
                    entryModel.remove(index);

                const successMessage = scopeTab === 0 ? qsTr("Tag removed") : qsTr("Context removed");
                const failureMessage = scopeTab === 0 ? qsTr("Tag was not removed") : qsTr("Context was not removed");
                setStatus(ok ? successMessage : failureMessage, ok);
            }
            onRemoveAttachmentRequested: function(index) {
                removeAttachmentAt(index);
            }
        }
    }
}
