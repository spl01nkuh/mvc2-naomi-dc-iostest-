#!/usr/bin/env bash
set -euo pipefail
# Xcode exports the resolved bundle location. CMake's multi-config bundle
# expression contains EFFECTIVE_PLATFORM_NAME, escaped in generated scripts.
BUNDLE_DIR="${TARGET_BUILD_DIR:?}/${FULL_PRODUCT_NAME:?}"
test -d "$BUNDLE_DIR"
xcrun --sdk "$1" metal -std=ios-metal2.3 -target "$2" -c "$3" -o "$4"
xcrun --sdk "$1" metallib "$4" -o "$BUNDLE_DIR/default.metallib"
