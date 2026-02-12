#!/usr/bin/env bash
# Build script for Linux/macOS
# Usage: ./build.sh [--release] [--test]

set -e

BUILD_DIR="build"
BUILD_TYPE="Debug"
RUN_TESTS=false

for arg in "$@"; do
    case "$arg" in
        --release) BUILD_TYPE="Release" ;;
        --test) RUN_TESTS=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

echo "=== Configuring (${BUILD_TYPE}) ==="
cmake -G Ninja -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "=== Building ==="
cmake --build "$BUILD_DIR" --parallel

if [ "$RUN_TESTS" = true ]; then
    echo "=== Running Tests ==="
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

echo "=== Done ==="
