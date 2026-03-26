#!/usr/bin/env bash
# benchmark-check.sh — Run benchmarks and compare against baseline
# Usage: ./scripts/benchmark-check.sh <test-dir> [baseline-file]
#
# Outputs JSON with timing results. If baseline provided, reports regressions.
# Exit code: 0 = pass, 1 = regression detected (>25% slower)

set -euo pipefail

TEST_DIR="${1:?Usage: benchmark-check.sh <test-dir> [baseline-file]}"
BASELINE="${2:-}"
THRESHOLD="${BENCH_THRESHOLD:-25}"  # percent regression threshold
OUTPUT_FILE="${TEST_DIR}/benchmark-results.json"

echo "=== Liva Benchmark Regression Check ==="
echo "Test dir: ${TEST_DIR}"
echo "Threshold: ${THRESHOLD}%"

# Run benchmark tests with verbose output, capture timing
RESULTS=$(ctest --test-dir "${TEST_DIR}" \
    -R "BenchmarkTest\.|IncrementalBenchmarkTest\." \
    --output-on-failure --verbose 2>&1 || true)

# Extract test names and durations from ctest output
# Format: "N/M Test #N: TestSuite.TestName .... Passed  X.XX sec"
declare -A TIMINGS
while IFS= read -r line; do
    if [[ "$line" =~ ([A-Za-z]+Test\.[A-Za-z0-9_]+).*Passed[[:space:]]+([0-9]+\.[0-9]+)[[:space:]]*sec ]]; then
        TEST_NAME="${BASH_REMATCH[1]}"
        DURATION="${BASH_REMATCH[2]}"
        TIMINGS["$TEST_NAME"]="$DURATION"
    fi
done <<< "$RESULTS"

# Generate JSON output
echo "{" > "$OUTPUT_FILE"
echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"," >> "$OUTPUT_FILE"
echo "  \"tests\": {" >> "$OUTPUT_FILE"

FIRST=true
for TEST_NAME in "${!TIMINGS[@]}"; do
    if [ "$FIRST" = true ]; then FIRST=false; else echo "," >> "$OUTPUT_FILE"; fi
    printf "    \"%s\": %s" "$TEST_NAME" "${TIMINGS[$TEST_NAME]}" >> "$OUTPUT_FILE"
done

echo "" >> "$OUTPUT_FILE"
echo "  }" >> "$OUTPUT_FILE"
echo "}" >> "$OUTPUT_FILE"

echo ""
echo "Results saved to: ${OUTPUT_FILE}"
echo "Tests measured: ${#TIMINGS[@]}"

# Compare against baseline if provided
if [ -n "$BASELINE" ] && [ -f "$BASELINE" ]; then
    echo ""
    echo "=== Comparing against baseline: ${BASELINE} ==="

    REGRESSIONS=0
    IMPROVEMENTS=0

    for TEST_NAME in "${!TIMINGS[@]}"; do
        CURRENT="${TIMINGS[$TEST_NAME]}"
        # Extract baseline value (simple grep from JSON)
        PREV=$(grep "\"${TEST_NAME}\"" "$BASELINE" 2>/dev/null | grep -oE '[0-9]+\.[0-9]+' | head -1 || echo "")

        if [ -z "$PREV" ] || [ "$PREV" = "0" ] || [ "$PREV" = "0.00" ]; then
            echo "  NEW  ${TEST_NAME}: ${CURRENT}s (no baseline)"
            continue
        fi

        # Calculate percent change: (current - prev) / prev * 100
        PCT=$(awk "BEGIN { printf \"%.1f\", (${CURRENT} - ${PREV}) / ${PREV} * 100 }")
        ABS_PCT=$(awk "BEGIN { v = ${PCT}; if (v < 0) v = -v; printf \"%.1f\", v }")

        if (( $(awk "BEGIN { print (${PCT} > ${THRESHOLD}) }") )); then
            echo "  WARN ${TEST_NAME}: ${PREV}s -> ${CURRENT}s (+${PCT}%)"
            REGRESSIONS=$((REGRESSIONS + 1))
        elif (( $(awk "BEGIN { print (${PCT} < -${THRESHOLD}) }") )); then
            echo "  GOOD ${TEST_NAME}: ${PREV}s -> ${CURRENT}s (${PCT}%)"
            IMPROVEMENTS=$((IMPROVEMENTS + 1))
        else
            echo "  OK   ${TEST_NAME}: ${PREV}s -> ${CURRENT}s (${PCT}%)"
        fi
    done

    echo ""
    echo "Summary: ${REGRESSIONS} regression(s), ${IMPROVEMENTS} improvement(s)"

    if [ "$REGRESSIONS" -gt 0 ]; then
        echo "WARNING: ${REGRESSIONS} benchmark(s) regressed by >${THRESHOLD}%"
        exit 1
    fi

    echo "All benchmarks within threshold."
fi

echo "=== Benchmark check complete ==="
