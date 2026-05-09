package io.sentry.qml;

import android.content.Context;
import android.util.Base64;
import android.util.Log;
import io.sentry.Attachment;
import io.sentry.Breadcrumb;
import io.sentry.Hint;
import io.sentry.ISerializer;
import io.sentry.Sentry;
import io.sentry.SentryAttribute;
import io.sentry.SentryAttributes;
import io.sentry.SentryEvent;
import io.sentry.SentryLevel;
import io.sentry.SentryLogLevel;
import io.sentry.android.core.SentryAndroid;
import io.sentry.logger.SentryLogParameters;
import io.sentry.metrics.SentryMetricsParameters;
import io.sentry.protocol.Feedback;
import io.sentry.protocol.SentryId;
import io.sentry.protocol.User;
import java.io.File;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONTokener;

public final class SentryQmlBridge {
    private static final String TAG = "SentryQml";
    private static final String VIEW_HIERARCHY_ATTACHMENT_TYPE = "event.view_hierarchy";

    private SentryQmlBridge() {
    }

    public static boolean init(Context context, String optionsJson, String sdkName) {
        try {
            final JSONObject json = object(optionsJson);
            SentryAndroid.init(context, options -> {
                setStringOption(json, "dsn", options::setDsn);
                setStringOption(json, "databasePath", options::setCacheDirPath);
                setStringOption(json, "release", options::setRelease);
                setStringOption(json, "environment", options::setEnvironment);
                setStringOption(json, "dist", options::setDist);
                if (json.has("debug")) {
                    options.setDebug(json.optBoolean("debug"));
                }
                if (json.has("enableLogs")) {
                    options.getLogs().setEnabled(json.optBoolean("enableLogs"));
                }
                if (json.has("enableMetrics")) {
                    options.getMetrics().setEnabled(json.optBoolean("enableMetrics"));
                }
                if (json.has("autoSessionTracking")) {
                    options.setEnableAutoSessionTracking(json.optBoolean("autoSessionTracking"));
                }
                if (json.has("sampleRate") && !json.isNull("sampleRate")) {
                    options.setSampleRate(json.optDouble("sampleRate"));
                }
                if (json.has("maxBreadcrumbs")) {
                    options.setMaxBreadcrumbs(json.optInt("maxBreadcrumbs"));
                }
                if (json.has("shutdownTimeout")) {
                    options.setShutdownTimeoutMillis(json.optLong("shutdownTimeout"));
                }
                if (json.has("attachViewHierarchy")) {
                    options.setAttachViewHierarchy(json.optBoolean("attachViewHierarchy"));
                }
                if (sdkName != null && !sdkName.isEmpty()) {
                    options.setNativeSdkName(sdkName);
                }
                options.setEnableNdk(true);
                options.setEnableScopeSync(true);
            });

            if (json.has("user") && !json.isNull("user")) {
                setUser(json.getJSONObject("user").toString());
            }
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not initialize Sentry Android.", t);
            return false;
        }
    }

    public static boolean flush(int timeoutMs) {
        try {
            Sentry.flush(timeoutMs);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not flush Sentry.", t);
            return false;
        }
    }

    public static void close() {
        Sentry.close();
    }

    public static boolean setRelease(String release) {
        try {
            Sentry.getCurrentScopes().getOptions().setRelease(emptyToNull(release));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set release.", t);
            return false;
        }
    }

    public static boolean setEnvironment(String environment) {
        try {
            Sentry.getCurrentScopes().getOptions().setEnvironment(emptyToNull(environment));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set environment.", t);
            return false;
        }
    }

    public static boolean setUser(String userJson) {
        try {
            Map<String, Object> userMap = toMap(object(userJson));
            User user = User.fromMap(userMap, Sentry.getCurrentScopes().getOptions());
            Sentry.setUser(user);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set user.", t);
            return false;
        }
    }

    public static boolean removeUser() {
        try {
            Sentry.setUser(null);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not remove user.", t);
            return false;
        }
    }

    public static boolean setTag(String key, String value) {
        try {
            Sentry.setTag(key, value);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set tag.", t);
            return false;
        }
    }

    public static boolean removeTag(String key) {
        try {
            Sentry.removeTag(key);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not remove tag.", t);
            return false;
        }
    }

    public static boolean setContext(String key, String contextJson) {
        try {
            Object value = parseJsonValue(contextJson);
            Sentry.configureScope(scope -> scope.setContexts(key, value));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set context.", t);
            return false;
        }
    }

    public static boolean removeContext(String key) {
        try {
            Sentry.configureScope(scope -> scope.removeContexts(key));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not remove context.", t);
            return false;
        }
    }

    public static boolean setAttribute(String key, String valueJson) {
        try {
            Sentry.setAttribute(attributeFromJson(key, valueJson));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set attribute.", t);
            return false;
        }
    }

    public static boolean removeAttribute(String key) {
        try {
            Sentry.removeAttribute(key);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not remove attribute.", t);
            return false;
        }
    }

    public static boolean setFingerprint(String fingerprintJson) {
        try {
            Sentry.setFingerprint(stringList(array(fingerprintJson)));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set fingerprint.", t);
            return false;
        }
    }

    public static boolean clearFingerprint() {
        try {
            Sentry.configureScope(scope -> scope.setFingerprint(new ArrayList<String>()));
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not clear fingerprint.", t);
            return false;
        }
    }

    public static boolean setAttachments(String attachmentsJson) {
        try {
            List<Attachment> attachments = attachmentsFromJson(attachmentsJson);
            Sentry.configureScope(scope -> {
                scope.clearAttachments();
                for (Attachment attachment : attachments) {
                    scope.addAttachment(attachment);
                }
            });
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not set attachments.", t);
            return false;
        }
    }

    public static boolean startSession() {
        try {
            Sentry.startSession();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not start session.", t);
            return false;
        }
    }

    public static boolean endSession(int status) {
        try {
            Sentry.endSession();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not end session.", t);
            return false;
        }
    }

    public static boolean addBreadcrumb(String breadcrumbJson) {
        try {
            Breadcrumb breadcrumb = Breadcrumb.fromMap(
                toMap(object(breadcrumbJson)),
                Sentry.getCurrentScopes().getOptions()
            );
            Sentry.addBreadcrumb(breadcrumb);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not add breadcrumb.", t);
            return false;
        }
    }

    public static boolean log(int level, String message, String attributesJson) {
        try {
            SentryLogParameters params = SentryLogParameters.create(attributesFromJson(attributesJson));
            params.setOrigin("qml");
            Sentry.logger().log(logLevel(level), params, message);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture log.", t);
            return false;
        }
    }

    public static boolean count(String name, long value, String attributesJson) {
        try {
            SentryMetricsParameters params = metricsParameters(attributesJson);
            Sentry.metrics().count(name, Double.valueOf(value), null, params);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture count metric.", t);
            return false;
        }
    }

    public static boolean gauge(String name, double value, String unit, String attributesJson) {
        try {
            SentryMetricsParameters params = metricsParameters(attributesJson);
            Sentry.metrics().gauge(name, Double.valueOf(value), emptyToNull(unit), params);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture gauge metric.", t);
            return false;
        }
    }

    public static boolean distribution(String name, double value, String unit, String attributesJson) {
        try {
            SentryMetricsParameters params = metricsParameters(attributesJson);
            Sentry.metrics().distribution(name, Double.valueOf(value), emptyToNull(unit), params);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture distribution metric.", t);
            return false;
        }
    }

    public static String captureEvent(String eventJson, String attachmentsJson) {
        try {
            ISerializer serializer = Sentry.getCurrentScopes().getOptions().getSerializer();
            SentryEvent event = serializer.deserialize(new StringReader(eventJson), SentryEvent.class);
            if (event == null) {
                return "";
            }

            SentryId eventId = Sentry.captureEvent(event, hintFromAttachments(attachmentsJson));
            return eventId == null ? "" : eventId.toString();
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture event.", t);
            return "";
        }
    }

    @SuppressWarnings("deprecation")
    public static boolean captureFeedback(String feedbackJson, String attachmentsJson) {
        try {
            JSONObject json = object(feedbackJson);
            String message = optString(json, "message");
            if (message == null || message.trim().isEmpty()) {
                return false;
            }

            Feedback feedback = new Feedback(message);
            String email = firstString(json, "email", "contactEmail", "contact_email");
            if (email != null) {
                feedback.setContactEmail(email);
            }
            String name = optString(json, "name");
            if (name != null) {
                feedback.setName(name);
            }
            String associatedEventId =
                firstString(json, "associatedEventId", "associated_event_id", "eventId");
            if (associatedEventId != null && !associatedEventId.trim().isEmpty()) {
                feedback.setAssociatedEventId(new SentryId(associatedEventId));
            }

            SentryId feedbackId = Sentry.captureFeedback(feedback, hintFromAttachments(attachmentsJson));
            return feedbackId != null;
        } catch (Throwable t) {
            Log.e(TAG, "Could not capture feedback.", t);
            return false;
        }
    }

    private interface StringSetter {
        void set(String value);
    }

    private static void setStringOption(JSONObject json, String key, StringSetter setter) {
        String value = optString(json, key);
        if (value != null) {
            setter.set(value);
        }
    }

    private static SentryMetricsParameters metricsParameters(String attributesJson) throws Exception {
        SentryMetricsParameters params = SentryMetricsParameters.create(attributesFromJson(attributesJson));
        params.setOrigin("qml");
        return params;
    }

    private static SentryAttributes attributesFromJson(String json) throws Exception {
        return SentryAttributes.fromMap(toMap(object(json)));
    }

    private static SentryAttribute attributeFromJson(String key, String json) throws Exception {
        Object value = parseJsonValue(json);
        if (value instanceof Map) {
            Map<?, ?> map = (Map<?, ?>) value;
            if (map.containsKey("value")) {
                return SentryAttribute.named(key, map.get("value"));
            }
        }
        return SentryAttribute.named(key, value);
    }

    private static SentryLogLevel logLevel(int level) {
        switch (level) {
            case -2:
                return SentryLogLevel.TRACE;
            case -1:
                return SentryLogLevel.DEBUG;
            case 1:
                return SentryLogLevel.WARN;
            case 2:
                return SentryLogLevel.ERROR;
            case 3:
                return SentryLogLevel.FATAL;
            case 0:
            default:
                return SentryLogLevel.INFO;
        }
    }

    private static List<String> stringList(JSONArray array) throws Exception {
        List<String> values = new ArrayList<>(array.length());
        for (int i = 0; i < array.length(); ++i) {
            if (!array.isNull(i)) {
                values.add(array.get(i).toString());
            }
        }
        return values;
    }

    private static Hint hintFromAttachments(String attachmentsJson) throws Exception {
        Hint hint = new Hint();
        for (Attachment attachment : attachmentsFromJson(attachmentsJson)) {
            if (VIEW_HIERARCHY_ATTACHMENT_TYPE.equals(attachment.getAttachmentType())) {
                hint.setViewHierarchy(attachment);
            } else {
                hint.addAttachment(attachment);
            }
        }
        return hint;
    }

    private static List<Attachment> attachmentsFromJson(String attachmentsJson) throws Exception {
        JSONArray json = array(attachmentsJson);
        List<Attachment> attachments = new ArrayList<>(json.length());
        for (int i = 0; i < json.length(); ++i) {
            attachments.add(attachmentFromJson(json.getJSONObject(i)));
        }
        return attachments;
    }

    private static Attachment attachmentFromJson(JSONObject json) throws Exception {
        String type = optString(json, "type");
        String filename = optString(json, "filename");
        String contentType = optString(json, "contentType");
        String attachmentType = optString(json, "attachmentType");
        if (attachmentType == null || attachmentType.isEmpty()) {
            attachmentType = "event.attachment";
        }

        if ("bytes".equals(type)) {
            byte[] bytes = Base64.decode(optString(json, "bytes"), Base64.DEFAULT);
            return new Attachment(bytes, filename, contentType, attachmentType, false);
        }

        String path = optString(json, "path");
        if (filename == null || filename.isEmpty()) {
            filename = path == null ? "attachment" : new File(path).getName();
        }
        return new Attachment(path, filename, contentType, attachmentType, false);
    }

    private static Object parseJsonValue(String json) throws Exception {
        Object value = new JSONTokener(json == null ? "null" : json).nextValue();
        return toObject(value);
    }

    private static JSONObject object(String json) throws Exception {
        if (json == null || json.isEmpty()) {
            return new JSONObject();
        }
        return new JSONObject(json);
    }

    private static JSONArray array(String json) throws Exception {
        if (json == null || json.isEmpty()) {
            return new JSONArray();
        }
        return new JSONArray(json);
    }

    private static Map<String, Object> toMap(JSONObject object) throws Exception {
        Map<String, Object> map = new HashMap<>();
        Iterator<String> keys = object.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            map.put(key, toObject(object.get(key)));
        }
        return map;
    }

    private static List<Object> toList(JSONArray array) throws Exception {
        List<Object> list = new ArrayList<>(array.length());
        for (int i = 0; i < array.length(); ++i) {
            list.add(toObject(array.get(i)));
        }
        return list;
    }

    private static Object toObject(Object value) throws Exception {
        if (value == null || value == JSONObject.NULL) {
            return null;
        }
        if (value instanceof JSONObject) {
            return toMap((JSONObject) value);
        }
        if (value instanceof JSONArray) {
            return toList((JSONArray) value);
        }
        return value;
    }

    private static String optString(JSONObject json, String key) {
        if (!json.has(key) || json.isNull(key)) {
            return null;
        }
        return json.optString(key, null);
    }

    private static String firstString(JSONObject json, String first, String second, String third) {
        String value = optString(json, first);
        if (value != null) {
            return value;
        }
        value = optString(json, second);
        if (value != null) {
            return value;
        }
        return optString(json, third);
    }

    private static String emptyToNull(String value) {
        return value == null || value.isEmpty() ? null : value;
    }
}
