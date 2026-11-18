#!/usr/bin/env bash
# Build & verify Explorer2 on Linux (64-bit x86_64, 32-bit i686 -m32, Clang, or ASan+UBSan).
# Usage:
#   ./linux/build_linux.sh [--32] [--clang] [--asan] [--build-dir DIR] [--test]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_32="OFF"
SANITIZE="OFF"
CXX_ARG=()
SUFFIX="gcc64"
RUN_TEST=0
BUILD_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --32|-m32)
            BUILD_32="ON"
            SUFFIX="gcc32"
            shift
            ;;
        --clang)
            CXX_ARG=(-DCMAKE_CXX_COMPILER=clang++)
            [[ "${BUILD_32}" == "ON" ]] && SUFFIX="clang32" || SUFFIX="clang64"
            shift
            ;;
        --asan|--sanitize)
            SANITIZE="ON"
            SUFFIX="${SUFFIX}-asan"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --test)
            RUN_TEST=1
            shift
            ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [--32] [--clang] [--asan] [--build-dir DIR] [--test]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${BUILD_DIR}" ]]; then
    BUILD_DIR="${REPO_ROOT}/build-linux-${SUFFIX}"
fi

GEN_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GEN_ARGS=(-GNinja)
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    "${GEN_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DE2_BUILD_32BIT="${BUILD_32}" \
    -DEXPLORER2_SANITIZE="${SANITIZE}" \
    "${CXX_ARG[@]}"

cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo "Built targets in ${BUILD_DIR}:"
file "${BUILD_DIR}/explorer2" "${BUILD_DIR}/explorer2-stage15" "${BUILD_DIR}/explorer2-readers"

if [[ "${RUN_TEST}" -eq 1 ]]; then
    (
        cd "${REPO_ROOT}"
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}" \
        UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
        "${BUILD_DIR}/explorer2" --self-test all "${BUILD_DIR}/selftest_all.txt"
        tail -n 3 "${BUILD_DIR}/selftest_all.txt"
    )
fi
