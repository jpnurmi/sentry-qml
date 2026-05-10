# Sentry QML

Experimental Sentry SDK for QML, backed by
[sentry-native](https://github.com/getsentry/sentry-native),
[Sentry Cocoa](https://github.com/getsentry/sentry-cocoa), or
[Sentry Android](https://github.com/getsentry/sentry-java), or the Sentry
JavaScript SDK when built for WebAssembly.

## Build

```sh
git submodule update --init --recursive
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Example

```sh
./build/example/sentry_qml_example
```

The example runs from the build tree without installing the QML module.

## Android

Configure Android builds with a Qt for Android toolchain and `-DSENTRY_QML_SDK=android`.
Android app targets that link `SentryQml` must also call:

```cmake
sentry_qml_configure_android_target(your_app_target)
```

This adds the Java bridge and the Gradle dependency on `io.sentry:sentry-android`.
The default dependency version is `8.41.0`; override it with
`-DSENTRY_ANDROID_VERSION=<version>` or replace the full Gradle coordinate with
`-DSENTRY_ANDROID_GRADLE_COORDINATE=<coordinate>`.

With Qt versions before 6.10, the helper owns `QT_ANDROID_PACKAGE_SOURCE_DIR`.
If your app already has a custom package source directory, copy
`src/android/java` into it and add the Sentry Android dependency to your
`build.gradle` manually, or build with Qt 6.10 or newer.

## WebAssembly

Configure WebAssembly builds with a Qt for WebAssembly toolchain. The
WebAssembly backend is selected automatically for Emscripten builds. The
generated page must load the Sentry JavaScript SDK before calling
`Sentry.init(...)` from QML and expose it as `globalThis.Sentry`. If
`globalThis.Sentry.wasmIntegration` is available, it is added during
initialization.

The wasm backend initializes the JavaScript SDK, applies QML event hooks for
events captured through the QML API, and forwards scope data, breadcrumbs, logs,
metrics, feedback, and attachments through the browser SDK. Native crash capture
is not supported by the browser JavaScript backend.
