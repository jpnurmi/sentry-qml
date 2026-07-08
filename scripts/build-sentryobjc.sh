#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"

sdks="macosx"
output_dir="$repo_dir/build/sentry-cocoa"
build_dir="${RUNNER_TEMP:-$repo_dir/build}/sentry-cocoa-build"
source_dir="$repo_dir/modules/sentry-cocoa"
ios_deployment_target="15.0"
build_recipe="7"

usage() {
    cat <<EOF
Usage: $0 [options]

Options:
  --sdks <list>                 Comma-separated SDKs to build (default: macosx)
  --output-dir <path>           Directory for SentryObjC-Dynamic.xcframework
  --build-dir <path>            Temporary build directory
  --source-dir <path>           sentry-cocoa checkout
  --ios-deployment-target <ver> iOS deployment target (default: 15.0)
  -h, --help                    Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sdks)
            sdks="$2"
            shift 2
            ;;
        --output-dir)
            output_dir="$2"
            shift 2
            ;;
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        --source-dir)
            source_dir="$2"
            shift 2
            ;;
        --ios-deployment-target)
            ios_deployment_target="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

mkdir -p "$output_dir"

cmake \
    -DSENTRY_COCOA_SOURCE_DIR="$source_dir" \
    -DSENTRY_COCOA_BUILD_DIR="$build_dir" \
    -DSENTRY_COCOA_XCFRAMEWORK="$output_dir/SentryObjC-Dynamic.xcframework" \
    -DSENTRY_COCOA_STAMP_FILE="$output_dir/SentryObjC-Dynamic.xcframework.stamp" \
    -DSENTRY_COCOA_XCODEBUILD="$(xcrun -find xcodebuild)" \
    -DSENTRY_COCOA_LIPO="$(xcrun -find lipo)" \
    -DSENTRY_COCOA_SDKS="$sdks" \
    -DSENTRY_COCOA_IOS_DEPLOYMENT_TARGET="$ios_deployment_target" \
    -DSENTRY_COCOA_BUILD_RECIPE="$build_recipe" \
    -P "$repo_dir/cmake/sentry-cocoa.cmake"
