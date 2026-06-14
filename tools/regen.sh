#!/usr/bin/env bash
# Full regen pipeline driver.
#
# Default behavior: regen all 9 banks into src/gen, sync funcs.h,
# then run the framework test suite. Each step gates
# on success — if any step fails, the script exits non-zero with a
# clear message about which step broke and what to do about it.
#
# Why this exists: generated bank C and funcs.h must move together.
# Codifying the order in one command means future contributors don't
# trip over it, and everything that needs to run in lockstep stays in
# lockstep.
#
# Flags:
#   --quick                 default. Skip Phase B fuzz.
#   --full                  also run Phase B fuzz (recomp + oracle + diff).
#   --no-tests              skip the framework test suite.
#   --strict-idempotent     after the regen, run it again into a temp
#                           dir and assert byte-identical output. Catches
#                           generator nondeterminism. Slower (full regen
#                           runs twice).
#   --v2                    accepted for compatibility; the active
#                           pipeline is always the v2 emitter now.
#   -h | --help             this message.
#
# Run from the repo root (or anywhere — paths are resolved relative
# to this script's location).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_FUZZ=0
RUN_TESTS=1
STRICT_IDEMPOTENT=0
for arg in "$@"; do
  case "$arg" in
    --quick)            RUN_FUZZ=0 ;;
    --full)             RUN_FUZZ=1 ;;
    --no-tests)         RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
    --v2)               ;;
    -h|--help)
      sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'
      exit 0
      ;;
    *)
      echo "regen.sh: unknown flag: $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

cd "$ROOT"

BANKS=(00 01 02 03 04 05 07 0c 0d)
TESTS="snesrecomp/tests/run_tests.py"
ROM="smw.sfc"

step() { echo; echo "=== $* ==="; }

# MSU-1: the build is recompiled from an MSU-1-patched ROM (Conn's audio-only
# SMW MSU-1 patch injects the driver into bank $04 freespace; recomp/bank04.cfg
# emits it). We apply the bundled IPS to the user's STOCK rom in a throwaway
# file — the user never patches anything, and at runtime still uses their stock
# ROM (the launcher applies the same patch beside it). See recomp/msu1/.
MSU_IPS="recomp/msu1/smw_msu.ips"
GEN_ROM="$ROM"
if [ -f "$MSU_IPS" ]; then
  PATCHED_ROM=".build/smw_msu1.sfc"
  mkdir -p "$(dirname "$PATCHED_ROM")"
  step "Applying MSU-1 patch (Conn, audio-only — recomp/msu1/)"
  python tools/apply_msu_patch.py --rom "$ROM" --ips "$MSU_IPS" --out "$PATCHED_ROM"
  GEN_ROM="$PATCHED_ROM"
fi

step "Regenerating 9 banks"
python snesrecomp/tools/v2_regen.py --rom "$GEN_ROM" \
    --cfg-dir recomp --out-dir src/gen

step "Syncing funcs.h"
python snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-diff"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  python snesrecomp/tools/v2_regen.py --rom "$GEN_ROM" \
      --cfg-dir recomp --out-dir "$TMP_GEN"
  drift_count=0
  for b in "${BANKS[@]}"; do
    if ! diff -q "src/gen/bank${b}_v2.c" "$TMP_GEN/bank${b}_v2.c" \
            > /dev/null 2>&1; then
      echo "  DRIFT: bank $b" >&2
      drift_count=$((drift_count + 1))
    fi
  done
  if ! diff -q "src/gen/unresolved_stubs_v2.c" \
          "$TMP_GEN/unresolved_stubs_v2.c" > /dev/null 2>&1; then
    echo "  DRIFT: unresolved stubs" >&2
    drift_count=$((drift_count + 1))
  fi
  if [ "$drift_count" -gt 0 ]; then
    echo "regen: $drift_count bank(s) non-deterministic" >&2
    exit 1
  fi
  echo "  all 9 banks byte-identical across two regens"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  python "$TESTS"
fi

if [ "$RUN_FUZZ" -eq 1 ]; then
  step "Phase B fuzz"
  python snesrecomp/fuzz/generate_snippets.py > /dev/null
  python snesrecomp/fuzz/run_recomp.py
  taskkill //F //IM smw.exe > /dev/null 2>&1 || true
  python snesrecomp/fuzz/run_oracle.py
  python snesrecomp/fuzz/diff.py
fi

step "Done"
