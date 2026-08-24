# Session Replay

Session Replay is an experimental, opt-in integration for native Qt Quick
applications on Windows, Linux, and macOS. It records a short, redacted frame
buffer before a native crash, encodes it as H.264/MPEG-4 after the next launch,
and submits it with the matching crash event.

This is crash replay, not full-session replay. It does not record healthy
sessions, keyboard input, text values, or operating-system windows.

## Requirements

- The Sentry QML native backend.
- Qt 6.8 or newer with Qt Multimedia and its FFmpeg backend.
- Runtime H.264/MPEG-4 encoding support.

The integration runs a short encoder check during preparation. An unavailable
backend or codec disables an optional integration with a diagnostic; it does
not prevent the core SDK from initializing. A required integration makes the
same failure fatal to SDK initialization.

Build the plugin explicitly:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_64" \
  -DSENTRY_QML_BUILD_SESSION_REPLAY_INTEGRATION=ON
cmake --build build --target SentryQmlSessionReplayIntegration
```

Qt Multimedia is linked only into the integration target. A shared core build
without the plugin does not acquire a Qt Multimedia dependency. Use
`sentry_qml_deploy_integrations()` to copy the integration beside the
application, and use the normal Qt deployment tooling to include Qt
Multimedia's FFmpeg plugin and runtime libraries.

For a static build, explicitly link `SentryQmlSessionReplayIntegration` and
import `SentryQmlSessionReplayIntegration` with `Q_IMPORT_PLUGIN` or
`qt_import_plugins()`. Applications that do not link/import it acquire no
replay or media dependency.

## Configuration

Sampling is disabled by default. Enable the integration through
`SentryOptions`:

```qml
SentryOptions {
    integrations: [
        SentryIntegration {
            name: "session-replay"
            configuration: {
                "crashSampleRate": 0.1,
                "durationMs": 5000,
                "frameRate": 1,
                "maxWidth": 1280,
                "maxHeight": 720,
                "maskAllText": true,
                "maskAllImages": true,
                "imageQuality": 70,
                "maxSpoolBytes": 33554432,
                "maxPendingReplays": 2
            }
        }
    ]
}
```

`crashSampleRate` accepts `0.0` through `1.0`. `durationMs` accepts 1–20
seconds, and `frameRate` accepts 1 or 2 FPS. Capture dimensions are normalized
to an even-sized fixed canvas. The byte, frame-count, duration, and pending-job
limits are all enforced independently.

## Privacy

Text, images, icons, and password inputs are masked by default. Redaction is
applied on the GUI thread before a frame can reach storage or encoder workers.
Password fields remain masked under every override.

Add explicit subtree markers when the default classification is insufficient:

```qml
Item {
    property bool sentryReplayMask: true
}

Text {
    property bool sentryReplayUnmask: true
    text: "Non-sensitive status"
}
```

An explicit mask takes precedence over an unmask. Custom Canvas,
ShaderEffect, and VideoOutput content cannot be classified automatically; mask
the containing item when it can display sensitive pixels. Set
`debugArtifacts: true` only during development to enable extra classification
diagnostics; it never writes unredacted frames.

## Storage and delivery

Redacted JPEG frames and an atomic manifest are stored below the configured
native database path in `replay-frames/<replay-id>/`. Capture keeps only the
configured rolling window and drops a tick whenever capture or storage is
busy.

After a crash, sentry-native supplies the previous crash envelope during the
next healthy initialization. The integration requires an exact replay-ID
match, serializes encode jobs, produces a finalized H.264 MP4, and submits the
structured `replay_video` envelope. Source frames remain available across
transient encoding or submission failures and are removed after SDK acceptance
or a bounded permanent-failure decision. Encoding retries are capped at three.

Normal shutdown deletes the current, uncrashed spool. If the application never
restarts after a crash, the replay remains local until a later launch performs
retention cleanup.
