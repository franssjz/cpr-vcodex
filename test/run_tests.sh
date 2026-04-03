#!/usr/bin/env bash
# Native unit test runner for cpr-vcodex
# Compiles and runs all test suites using g++ and the Unity test framework.
#
# Usage: ./test/run_tests.sh
# Exit code: 0 if all tests pass, 1 if any test fails

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="/tmp/cpr-vcodex-tests"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Find or install Unity test framework
UNITY_DIR=""
if [ -d "$BUILD_DIR/Unity" ]; then
  UNITY_DIR="$BUILD_DIR/Unity"
elif [ -d "/tmp/Unity" ]; then
  UNITY_DIR="/tmp/Unity"
else
  echo -e "${YELLOW}Downloading Unity test framework...${NC}"
  mkdir -p "$BUILD_DIR"
  git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git "$BUILD_DIR/Unity" 2>/dev/null
  UNITY_DIR="$BUILD_DIR/Unity"
fi

UNITY_SRC="$UNITY_DIR/src"
if [ ! -f "$UNITY_SRC/unity.h" ]; then
  echo -e "${RED}ERROR: Unity test framework not found at $UNITY_SRC${NC}"
  exit 1
fi

mkdir -p "$BUILD_DIR"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-function"

TOTAL_TESTS=0
TOTAL_FAILURES=0
TOTAL_IGNORED=0
FAILED_SUITES=()

run_test_suite() {
  local name="$1"
  local sources="$2"
  local includes="$3"

  echo ""
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo -e " Running: ${YELLOW}${name}${NC}"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

  local output_binary="$BUILD_DIR/$name"

  # Compile
  if ! $CXX $CXXFLAGS $includes -o "$output_binary" $sources "$UNITY_SRC/unity.c" 2>&1; then
    echo -e "${RED}COMPILATION FAILED: $name${NC}"
    FAILED_SUITES+=("$name (compilation)")
    return
  fi

  # Run and capture output
  local test_output
  local exit_code=0
  test_output=$("$output_binary" 2>&1) || exit_code=$?

  echo "$test_output"

  # Parse results from Unity output (last lines contain summary)
  local results_line
  results_line=$(echo "$test_output" | grep -E "^-+$" -A1 | tail -1)
  if [[ "$results_line" =~ ([0-9]+)\ Tests\ ([0-9]+)\ Failures\ ([0-9]+)\ Ignored ]]; then
    TOTAL_TESTS=$((TOTAL_TESTS + ${BASH_REMATCH[1]}))
    TOTAL_FAILURES=$((TOTAL_FAILURES + ${BASH_REMATCH[2]}))
    TOTAL_IGNORED=$((TOTAL_IGNORED + ${BASH_REMATCH[3]}))
  fi

  if [ $exit_code -ne 0 ]; then
    FAILED_SUITES+=("$name")
  fi
}

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           cpr-vcodex Native Unit Test Runner                ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Project root: $PROJECT_ROOT"
echo "Compiler: $($CXX --version | head -1)"
echo "Unity: $UNITY_DIR"

cd "$PROJECT_ROOT"

# Test 1: UTF-8 library
run_test_suite "test_utf8" \
  "test/test_utf8/test_utf8.cpp lib/Utf8/Utf8.cpp" \
  "-Ilib/Utf8 -I$UNITY_SRC"

# Test 2: TimeZone registry
run_test_suite "test_timezone" \
  "test/test_timezone/test_timezone.cpp" \
  "-Isrc/util -I$UNITY_SRC"

# Test 3: Date math (TimeUtils)
run_test_suite "test_date_math" \
  "test/test_date_math/test_date_math.cpp" \
  "-Itest/mocks -Isrc -Isrc/util -I$UNITY_SRC"

# Test 4: Duration formatting
run_test_suite "test_format_duration" \
  "test/test_format_duration/test_format_duration.cpp" \
  "-I$UNITY_SRC"

# Test 5: Settings helpers
run_test_suite "test_settings_helpers" \
  "test/test_settings_helpers/test_settings_helpers.cpp" \
  "-Itest/mocks -Isrc -Isrc/util -I$UNITY_SRC"

# Print summary
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                      TEST SUMMARY                          ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Total tests:    $TOTAL_TESTS"
echo "  Failures:       $TOTAL_FAILURES"
echo "  Ignored:        $TOTAL_IGNORED"
echo "  Test suites:    5"

if [ ${#FAILED_SUITES[@]} -gt 0 ]; then
  echo ""
  echo -e "  ${RED}FAILED SUITES:${NC}"
  for suite in "${FAILED_SUITES[@]}"; do
    echo -e "    ${RED}✗ $suite${NC}"
  done
  echo ""
  echo -e "${RED}RESULT: FAIL${NC}"
  exit 1
else
  echo ""
  echo -e "  ${GREEN}All test suites passed!${NC}"
  echo ""
  echo -e "${GREEN}RESULT: OK${NC}"
  exit 0
fi
