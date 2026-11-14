#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SDK="${1:-device}"
case "$SDK" in
    device) SYSROOT=iphoneos; ARCH=arm64 ;;
    simulator) SYSROOT=iphonesimulator; ARCH="$(uname -m)" ;;
    *) echo "Usage: $0 [device|simulator]" >&2; exit 1 ;;
esac
if [[ "$(uname -s)" != Darwin ]] || ! command -v xcodebuild >/dev/null; then
    echo "This build requires macOS and the full Xcode app." >&2
    exit 1
fi
BUILD_DIR="$ROOT_DIR/build/ios-development-$SDK"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
    -DMVC2_DEV_BUILD_NUMBER="${MVC2_DEV_BUILD_NUMBER:-1}"
cmake --build "$BUILD_DIR" --config Release --target MVC2Development --parallel 4
APP="$BUILD_DIR/Release-$SYSROOT/MVC2Development.app"
test -f "$APP/MVC2Development"
test -f "$APP/default.metallib"
if [[ "$SDK" == device ]]; then
    PACKAGE_DIR="$(mktemp -d)"
    trap 'rm -rf "$PACKAGE_DIR"' EXIT
    mkdir -p "$PACKAGE_DIR/Payload"
    ditto "$APP" "$PACKAGE_DIR/Payload/MVC2Development.app"
    ditto -c -k --keepParent "$PACKAGE_DIR/Payload" "$BUILD_DIR/MVC2-Development-unsigned.ipa"
    shasum -a 256 "$BUILD_DIR/MVC2-Development-unsigned.ipa" > "$BUILD_DIR/SHA256.txt"
    echo "Unsigned development IPA: $BUILD_DIR/MVC2-Development-unsigned.ipa"
fi
