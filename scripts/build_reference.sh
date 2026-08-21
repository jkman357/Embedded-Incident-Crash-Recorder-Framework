#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
VALIDATION="$ROOT/validation"
CC="${CC:-gcc}"

COMMON=(
  -std=c11
  -Wall -Wextra -Wpedantic -Werror
  -O2
  -ffunction-sections -fdata-sections
  -I"$ROOT/include"
  -I"$ROOT/src"
  -I"$ROOT/reference"
)

SOURCES=(
  "$ROOT/src/ir_core.c"
  "$ROOT/src/ir_service.c"
  "$ROOT/reference/ir_reference_project.c"
  "$ROOT/reference/main.c"
)

rm -rf "$BUILD"
mkdir -p "$BUILD/release" "$BUILD/development" "$BUILD/off" "$VALIDATION"
: > "$VALIDATION/build.log"

run() {
  echo "+ $*" | tee -a "$VALIDATION/build.log"
  "$@" 2>&1 | tee -a "$VALIDATION/build.log"
}

{
  echo "Embedded Incident & Crash Recorder Framework v1.0.0rc02"
  echo "Reference build compiler: $($CC --version | head -1)"
  echo
} | tee -a "$VALIDATION/build.log"

run "$CC" "${COMMON[@]}" \
  "${SOURCES[@]}" \
  -Wl,-Map,"$VALIDATION/reference_release.map" \
  -o "$BUILD/release/ir_reference"
run "$BUILD/release/ir_reference"

run "$CC" "${COMMON[@]}" \
  -DIR_BUILD_PROFILE=IR_BUILD_PROFILE_DEVELOPMENT \
  "${SOURCES[@]}" \
  -Wl,-Map,"$VALIDATION/reference_development.map" \
  -o "$BUILD/development/ir_reference"
run "$BUILD/development/ir_reference"

run "$CC" "${COMMON[@]}" \
  -DIR_ENABLE=0 \
  "$ROOT/reference/probe_off_check.c" \
  -o "$BUILD/off/probe_off_check"
run "$BUILD/off/probe_off_check"

run "$CC" -std=c99 -Wall -Wextra -Wpedantic -Werror -O2 \
  -I"$ROOT/include" -I"$ROOT/src" -I"$ROOT/reference" \
  "${SOURCES[@]}" \
  -o "$BUILD/release/ir_reference_c99"
run "$BUILD/release/ir_reference_c99"

run "$CC" "${COMMON[@]}" \
  "$ROOT/reference/size_report.c" \
  -o "$BUILD/release/size_report"
run "$BUILD/release/size_report" | tee "$VALIDATION/size_report.txt"

run objdump -h "$BUILD/release/ir_reference"
objdump -h "$BUILD/release/ir_reference" > "$VALIDATION/section_report.txt"

if ! grep -q '\.incident_ram' "$VALIDATION/reference_release.map"; then
  echo "FAIL: .incident_ram not found in release MAP" | tee -a "$VALIDATION/build.log"
  exit 1
fi

if ! grep -q '\.incident_ram' "$VALIDATION/reference_development.map"; then
  echo "FAIL: .incident_ram not found in development MAP" | tee -a "$VALIDATION/build.log"
  exit 1
fi

if ! grep -q '\.incident_ram' "$VALIDATION/section_report.txt"; then
  echo "FAIL: .incident_ram not found in linked section table" | tee -a "$VALIDATION/build.log"
  exit 1
fi

if nm "$BUILD/release/ir_reference" | grep -q 'g_ir_task_trace_queue'; then
  echo "FAIL: Release build still contains Development continuous-trace queue" | tee -a "$VALIDATION/build.log"
  exit 1
fi

if ! nm "$BUILD/development/ir_reference" | grep -q 'g_ir_task_trace_queue'; then
  echo "FAIL: Development build is missing continuous-trace queue" | tee -a "$VALIDATION/build.log"
  exit 1
fi

echo "PASS: Release continuous-trace queue compiled out; Development queue present" | tee -a "$VALIDATION/build.log"
echo "PASS: release/development/off builds, C99 compatibility, runtime checks, retained size guard, and MAP section check" | tee -a "$VALIDATION/build.log"

# Keep published validation artifacts independent of the local checkout path.
for file in "$VALIDATION/build.log" "$VALIDATION/reference_release.map" \
            "$VALIDATION/reference_development.map" "$VALIDATION/section_report.txt" \
            "$VALIDATION/size_report.txt"; do
  sed -i "s|$ROOT|.|g" "$file"
done
