#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${GTEST_SOURCE_DIR:-}" ]]; then
    gtest_source_dir="$GTEST_SOURCE_DIR"
elif [[ -f "$root_dir/../googletest/googletest/src/gtest-all.cc" ]]; then
    gtest_source_dir="$root_dir/../googletest"
else
    gtest_source_dir=/usr/src/googletest
fi
build_dir="${BUILD_DIR:-${root_dir}/build/chapter02-arm64-test}"
aarch64_cxx="${AARCH64_CXX:-aarch64-linux-gnu-g++}"
qemu_aarch64="${QEMU_AARCH64:-qemu-aarch64}"
aarch64_sysroot="${AARCH64_SYSROOT:-/usr/aarch64-linux-gnu}"

for tool in "$aarch64_cxx" "$qemu_aarch64" wast2json; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

if [[ ! -f "$gtest_source_dir/googletest/src/gtest-all.cc" || ! -f "$gtest_source_dir/googletest/src/gtest_main.cc" ]]; then
    printf 'GoogleTest sources not found under %s\n' "$gtest_source_dir" >&2
    exit 1
fi

mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
wast2json "$root_dir/Chapter02/local.wast" -o "$build_dir/local.json"

"$aarch64_cxx" \
    -std=c++17 -Wall -Wextra -O0 -g -pthread \
    -I"$gtest_source_dir/googletest/include" \
    -I"$gtest_source_dir/googletest" \
    "$root_dir/Chapter02/wasm_chapter02_test.cpp" \
    "$gtest_source_dir/googletest/src/gtest-all.cc" \
    "$gtest_source_dir/googletest/src/gtest_main.cc" \
    -o "$build_dir/wasm_chapter02_test"

(
    cd "$build_dir"
    "$qemu_aarch64" -L "$aarch64_sysroot" "$build_dir/wasm_chapter02_test"
)
