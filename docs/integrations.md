# Integration plugins

Sentry QML integrations are optional Qt plugins selected by the application.
They execute in process with the application's full privileges. Metadata checks
compatibility; it does not authenticate a publisher or make plugin code safe.
Production applications should build or audit integrations, ship them with the
application, and include them and their dependencies in code-signing and update
integrity checks. Runtime downloads and network URLs are not supported.

The C++ plugin ABI is public but experimental while Sentry QML itself has no
stable C++ ABI. A plugin and the SDK should be built with compatible Qt and
Sentry QML versions.

## Selecting integrations

QML applications add explicit descriptors to `SentryOptions`:

```qml
SentryOptions {
    integrationPaths: ["/opt/my-app/lib/sentry-integrations"]
    integrations: [
        SentryIntegration {
            name: "minimal"
            required: false
            configuration: { "label": "example" }
        }
    ]
}
```

C++ applications use the same descriptor objects:

```cpp
SentryOptions options;
SentryIntegration integration;
integration.setName(QStringLiteral("minimal"));
integration.setConfiguration({{QStringLiteral("label"), QStringLiteral("example")}});
options.addIntegration(&integration);
Sentry::instance()->init(&options);
```

An ID must match `[a-z0-9][a-z0-9.-]*`. Set `path` to select one absolute local
plugin file directly. Otherwise resolution checks imported static plugins and
then, in order, each absolute local `integrationPaths` directory and the
application deployment directory:

- Windows and Linux: `<applicationDir>/sentry-integrations`
- macOS bundles: `<applicationDir>/../PlugIns/sentry-integrations`

Directories are not searched recursively. The working directory, Qt plugin
paths, QML import paths, and Sentry data directories are never searched.
Multiple enabled descriptors or multiple candidates for one ID are errors.
Disabled descriptors are ignored. A missing or incompatible optional
integration emits a warning; a required integration prevents initialization.

The SDK snapshots configuration during `Sentry.init()`. Changing a descriptor
does not reconfigure a running integration. Call `Sentry.close()` and then
`Sentry.init()` to start a new cycle. Plugin root objects and dynamic libraries
stay loaded for the entire process, but `stop()` releases their runtime state.

## Implementing a plugin

The plugin root inherits `QObject` and `SentryIntegrationPlugin`, declares
`Q_INTERFACES`, and supplies JSON metadata with `Q_PLUGIN_METADATA`. The
[minimal example](../example/integration/minimalintegration.cpp) is a complete
implementation without feature dependencies.

Metadata must contain `Id`, `Name`, `Version`, `IntegrationApi`,
`MinimumSentryQmlVersion`, `Platforms`, `Backends`, and `RequiredServices`.
Sentry QML validates it before constructing the plugin root. Service IDs are
versioned interface IDs; `prepare()` can obtain an advertised service with
`SentryIntegrationContext::service()`.

Lifecycle calls are ordered as follows:

1. `prepare()` runs before backend initialization and receives a copied
   configuration and the SDK-owned context.
2. `start()` runs after backend initialization but before `initialized` becomes
   true. Scope operations on the context are available here.
3. `flush()` runs before backend flush and receives the caller's remaining
   monotonic deadline.
4. `stop()` runs before backend close and in reverse descriptor order.

Methods execute on the SDK lifecycle caller's thread, normally the GUI thread.
They must be bounded and must not throw. Move expensive work to bounded workers,
marshal GUI work to the GUI thread, and make `stop()` idempotent and
non-throwing. Detach every observer, worker, and callback during `stop()`.
Plugin code is never called from a crash signal or exception handler.

## Building and deploying

After `find_package(SentryQml CONFIG REQUIRED)`, use the installed helper:

```cmake
sentry_qml_add_integration(
    TARGET MyIntegration
    ID minimal
    CLASS_NAME MinimalIntegration
    METADATA "${CMAKE_CURRENT_SOURCE_DIR}/minimalintegration.json"
    SOURCES minimalintegration.cpp minimalintegration.json
    LIBRARIES MyFeatureDependency
)

sentry_qml_deploy_integrations(
    TARGET my_application
    INTEGRATIONS MyIntegration
)
```

With a shared SDK the helper builds a dynamic plugin in
`sentry-integrations` and installs it under an SDK-specific integration
directory. The deployment helper copies selected plugins next to the
application; on Windows it also copies their runtime DLLs. Use Qt's application
deployment tooling to collect Qt and platform runtime dependencies on other
platforms.

With a static SDK, the helper builds a static Qt plugin and the deployment
helper links only integrations named by the application. Import each selected
plugin using `Q_IMPORT_PLUGIN(MinimalIntegration)` or the corresponding
`qt_import_plugins()` configuration. Optional feature dependencies therefore
enter a static application only when their integration target is selected.

Platform-specific integration features may still restrict their `Platforms`,
`Backends`, or `RequiredServices` metadata. Dynamic loading is intended for
desktop deployments; static import provides the common plugin mechanism on
iOS, WebAssembly, Android, and static desktop builds.

Qt references: [creating plugins](https://doc.qt.io/qt-6/plugins-howto.html),
[`QPluginLoader`](https://doc.qt.io/qt-6/qpluginloader.html),
[`qt_add_plugin`](https://doc.qt.io/qt-6/qt-add-plugin.html), and
[deploying plugins](https://doc.qt.io/qt-6/deployment-plugins.html).
