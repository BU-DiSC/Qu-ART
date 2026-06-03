#!/usr/bin/env bash
# graphs/run_and_plot.sh — produce the two test_kfp bar charts (x = 4 workloads).
#
#   1. Configure+build test_kfp with QUART_KFP_STATS=OFF, run it, capture timings.
#   2. Configure+build test_kfp with QUART_KFP_STATS=ON,  run it, capture stats.
#   3. Plot graphs/timings.png and graphs/stats.png from the two captures.
#
# The stat counters add a per-insert increment to the QuART_kfp hot path, so the
# timings MUST come from the stats-OFF build (clean) and the counts from the
# stats-ON build — hence two separate configure/build/run passes.
#
# Usage: graphs/run_and_plot.sh [max_keys_per_stream]
#   default 0 = use the full stream size (all 200M keys per stream).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build"
KEY_LIMIT="${1:-0}"

cd "$ROOT"

echo "==> [1/2] Building test_kfp with stats OFF (clean timings)"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DQUART_KFP_STATS=OFF >/dev/null
cmake --build "$BUILD" --target test_kfp -j"$(nproc)" >/dev/null
echo "==> Running (max_keys_per_stream=$KEY_LIMIT) ..."
"$BUILD/test_kfp" both "$KEY_LIMIT" | tee "$HERE/times_raw.txt"

echo
echo "==> [2/2] Building test_kfp with stats ON (classification counts)"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DQUART_KFP_STATS=ON >/dev/null
cmake --build "$BUILD" --target test_kfp -j"$(nproc)" >/dev/null
echo "==> Running (max_keys_per_stream=$KEY_LIMIT) ..."
"$BUILD/test_kfp" ff "$KEY_LIMIT" | tee "$HERE/stats_raw.txt"

echo
echo "==> Plotting"
python3 "$HERE/plot.py" "$HERE/times_raw.txt" "$HERE/stats_raw.txt" "$HERE"

echo "==> Done. Wrote:"
echo "    $HERE/timings.png"
echo "    $HERE/stats.png"
