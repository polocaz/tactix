#!/usr/bin/env bash
# Configure + build Tactix, optionally run something afterwards.
#
#   ./scripts/build.sh                          build Release
#   ./scripts/build.sh -r                       build, then run the GUI
#   ./scripts/build.sh -t                       build, then run the tests
#   ./scripts/build.sh -b --agents 10000 --json build, then run the benchmark
#   ./scripts/build.sh -d -r                    Debug build (build-debug/), then run
#
# Anything after the flags is forwarded to the binary being run.
set -euo pipefail

cd "$(dirname "$0")/.."

config=Release
dir=build
run=

usage() {
    echo "Usage: scripts/build.sh [-d] [-r|-b|-t] [args forwarded to the binary]"
    echo "  -d  Debug build (build-debug/)   -r  run the GUI"
    echo "  -t  run the tests                -b  run the benchmark"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -d|--debug) config=Debug; dir=build-debug ;;
        -r|--run)   run=tactix ;;
        -b|--bench) run=tactix_bench ;;
        -t|--test)  run=test ;;
        -h|--help)  usage; exit 0 ;;
        *) break ;;
    esac
    shift
done

cmake -B "$dir" -DCMAKE_BUILD_TYPE="$config"
cmake --build "$dir" --config "$config" --parallel

case "$run" in
    "")   ;;
    test) ctest --test-dir "$dir" -C "$config" --output-on-failure ;;
    *)    # Multi-config generators (Visual Studio, Xcode) nest by config; single-config ones don't.
          bin="$dir/$run"
          [ -x "$bin" ] || bin="$dir/$config/$run"
          "$bin" "$@" ;;
esac
