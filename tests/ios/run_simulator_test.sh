#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "usage: run_simulator_test.sh <app-bundle> [args...]" >&2
    exit 64
fi

app_bundle="$1"
shift

if [ ! -d "$app_bundle" ] && [[ "$app_bundle" == *'${EFFECTIVE_PLATFORM_NAME}'* ]]; then
    simulator_bundle="${app_bundle//\$\{EFFECTIVE_PLATFORM_NAME\}/-iphonesimulator}"
    device_bundle="${app_bundle//\$\{EFFECTIVE_PLATFORM_NAME\}/}"
    if [ -d "$simulator_bundle" ]; then
        app_bundle="$simulator_bundle"
    elif [ -d "$device_bundle" ]; then
        app_bundle="$device_bundle"
    fi
fi

if [ ! -d "$app_bundle" ]; then
    echo "iOS app bundle not found: $app_bundle" >&2
    exit 66
fi

bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_bundle/Info.plist")"
device="${SENTRY_QML_IOS_SIMULATOR_UDID:-}"

if [ -z "$device" ]; then
    device="$(xcrun simctl list devices booted | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)) (Booted).*/\1/p' | head -n 1)"
fi

if [ -z "$device" ]; then
    device="$(xcrun simctl list devices available | sed -n 's/.*(\([0-9A-Fa-f-]\{36\}\)) (Shutdown).*/\1/p' | head -n 1)"
    if [ -z "$device" ]; then
        echo "No available iOS simulator found." >&2
        exit 69
    fi
    xcrun simctl boot "$device"
fi

xcrun simctl bootstatus "$device" -b
xcrun simctl install "$device" "$app_bundle"
xcrun simctl terminate "$device" "$bundle_id" >/dev/null 2>&1 || true
xcrun simctl launch --console-pty "$device" "$bundle_id" "$@"
