#!/usr/bin/env bash
# Build PhiPhysiceEditorNeo on Linux or macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

CMAKE_ARGS=(-S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
if [[ -n "${RAYLIB_ROOT:-}" ]]; then
    CMAKE_ARGS+=("-DRAYLIB_ROOT=$RAYLIB_ROOT")
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j "$JOBS"

if [[ -x "$BUILD_DIR/PhiPhysiceEditorNeo" ]]; then
    echo "Running self test..."
    "$BUILD_DIR/PhiPhysiceEditorNeo" --selftest
    echo
    echo "Build complete: $BUILD_DIR/PhiPhysiceEditorNeo"
    echo "Run: $BUILD_DIR/PhiPhysiceEditorNeo"
else
    echo "Build finished but the executable was not found in $BUILD_DIR"
    exit 1
fi
