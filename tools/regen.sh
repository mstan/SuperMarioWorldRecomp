#!/usr/bin/env bash
# Full regen pipeline driver.
#
# Default behavior: regen all 9 banks, sync funcs.h, regen
# func registry, then run the framework test suite. Each step gates
# on success — if any step fails, the script exits non-zero with a
# clear message about which step broke and what to do about it.
#
# Why this exists: forgetting any one of these (especially
# gen_func_registry.py) produces a broken build (LNK2001) that's
# painful to debug after the fact. Codifying the order in one
# command means future contributors don't trip over it, and
# everything that needs to run in lockstep stays in lockstep.
#
# Flags:
#   --quick                 default. Skip Phase B fuzz.
#   --full                  also run Phase B fuzz (recomp + oracle + diff).
#   --no-tests              skip the framework test suite.
#   --strict-idempotent     after the regen, run it again into a temp
#                           dir and assert byte-identical output. Catches
#                           generator nondeterminism. Slower (full regen
#                           runs twice).
#   --v2                    run the v2 pipeline (gen_v2/ + funcs_v2.h)
#                           instead of the v1 pipeline. Skips the v1
#                           recomp_func_registry step (v2 doesn't need it).
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
USE_V2=0

for arg in "$@"; do
  case "$arg" in
    --quick)            RUN_FUZZ=0 ;;
    --full)             RUN_FUZZ=1 ;;
    --no-tests)         RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
    --v2)               USE_V2=1 ;;
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
RECOMP="snesrecomp/recompiler/recomp.py"
SYNC_FUNCS="tools/sync_funcs_h.py"
GEN_REGISTRY="tools/gen_func_registry.py"
TESTS="snesrecomp/tests/run_tests.py"
ROM="smw.sfc"

step() { echo; echo "=== $* ==="; }

regen_all_banks() {
  local out_dir="${1:-src/gen}"
  mkdir -p "$out_dir"
  for b in "${BANKS[@]}"; do
    python "$RECOMP" "$ROM" "recomp/bank${b}.cfg" \
      --reverse-debug -o "$out_dir/smw_${b}_gen.c" \
      > /dev/null 2>&1 \
        || { echo "regen: bank $b failed" >&2; return 1; }
  done
}

if [ "$USE_V2" -eq 1 ]; then
  step "Regenerating 9 banks (v2 pipeline)"
  python snesrecomp/tools/v2_regen.py --rom "$ROM" \
      --cfg-dir recomp --out-dir src/gen_v2

  step "Syncing funcs_v2.h"
  python snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
      --out recomp/funcs_v2.h
else
  step "Regenerating 9 banks"
  regen_all_banks src/gen
  echo "  ok"

  step "Syncing funcs.h"
  python "$SYNC_FUNCS"

  step "Regenerating recomp_func_registry.c"
  python "$GEN_REGISTRY"
fi

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-diff"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  regen_all_banks "$TMP_GEN"
  drift_count=0
  for b in "${BANKS[@]}"; do
    if ! diff -q "src/gen/smw_${b}_gen.c" "$TMP_GEN/smw_${b}_gen.c" \
            > /dev/null 2>&1; then
      echo "  DRIFT: bank $b" >&2
      drift_count=$((drift_count + 1))
    fi
  done
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
