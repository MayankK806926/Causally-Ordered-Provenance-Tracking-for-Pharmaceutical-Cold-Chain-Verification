#!/usr/bin/env bash
# run_all_sweeps.sh — Week 5: Run the full parameter sweep suite.
#
# Runs two sweep passes:
#   1. Primary sweep: vary blackout duration (0.1h–48h) at default sampling rate
#   2. Secondary sweep: vary sampling interval (30s, 60s, 120s, 300s) at a
#      fixed blackout of 4h — characterizes pruning storage savings vs throughput
#
# Usage:
#   chmod +x experiments/run_all_sweeps.sh
#   ./experiments/run_all_sweeps.sh [BUILD_DIR]
#
# BUILD_DIR defaults to ./build

set -euo pipefail

BUILD_DIR="${1:-build}"
SWEEP_BIN="${BUILD_DIR}/sweep_runner"
DATA_DIR="data/sweeps"
PLOT_SCRIPT="experiments/plot_results.py"

echo "========================================================================"
echo " Cold Chain Vector Clock — Full Sweep Suite"
echo "========================================================================"
echo ""

# Check executable exists
if [ ! -x "$SWEEP_BIN" ]; then
    echo "ERROR: sweep_runner not found at ${SWEEP_BIN}"
    echo "       Run 'cmake --build ${BUILD_DIR}' first."
    exit 1
fi

mkdir -p "$DATA_DIR"

# ── Sweep 1: Blackout duration ────────────────────────────────
echo "━━━ Sweep 1: Blackout Duration (0.1h–48h) ━━━"
"$SWEEP_BIN" \
    --blackout-hours 0.1,0.5,1,2,4,8,16,24,48 \
    --runs-per-point 20 \
    --shipments 5 \
    --duration-hours 72 \
    --out "${DATA_DIR}/results.csv"

echo ""

# ── Sweep 2: Sampling interval (throughput characterization) ──
echo "━━━ Sweep 2: Sampling Interval vs. Pruning (blackout fixed at 4h) ━━━"
"$SWEEP_BIN" \
    --blackout-hours 4 \
    --sampling-intervals 30,60,120,300 \
    --runs-per-point 10 \
    --shipments 5 \
    --duration-hours 24 \
    --out "${DATA_DIR}/sampling_sweep.csv"

echo ""

# ── Generate plots ────────────────────────────────────────────
echo "━━━ Generating plots ━━━"
if command -v python3 &>/dev/null; then
    python3 "$PLOT_SCRIPT" --input "${DATA_DIR}/results.csv" --output-dir "$DATA_DIR"
    echo ""
    echo "Sampling sweep plots:"
    python3 "$PLOT_SCRIPT" --input "${DATA_DIR}/sampling_sweep.csv" --output-dir "${DATA_DIR}/sampling"
elif command -v python &>/dev/null; then
    python "$PLOT_SCRIPT" --input "${DATA_DIR}/results.csv" --output-dir "$DATA_DIR"
    python "$PLOT_SCRIPT" --input "${DATA_DIR}/sampling_sweep.csv" --output-dir "${DATA_DIR}/sampling"
else
    echo "WARNING: python not found — skipping plot generation."
    echo "         Run manually: python ${PLOT_SCRIPT} --input ${DATA_DIR}/results.csv"
fi

echo ""
echo "========================================================================"
echo " All sweeps complete."
echo " Results:  ${DATA_DIR}/results.csv"
echo "           ${DATA_DIR}/sampling_sweep.csv"
echo " Plots:    ${DATA_DIR}/*.png"
echo "========================================================================"
