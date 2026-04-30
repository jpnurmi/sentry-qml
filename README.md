# Sentry QML

Experimental Sentry SDK for QML, backed by
[sentry-native](https://github.com/getsentry/sentry-native).

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
