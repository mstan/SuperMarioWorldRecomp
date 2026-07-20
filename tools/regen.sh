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

TESTS="snesrecomp/tests/run_tests.py"
ROM="smw.sfc"

PYTHON="${PYTHON:-$(command -v python3 || command -v python || true)}"
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3/python interpreter found on PATH" >&2
  exit 1
fi

step() { echo; echo "=== $* ==="; }

ANALYSIS_BACKEND="${SNESRECOMP_ANALYSIS_BACKEND:-native}"
case "$ANALYSIS_BACKEND" in
  native|python|auto) ;;
  *) echo "regen.sh: invalid SNESRECOMP_ANALYSIS_BACKEND: $ANALYSIS_BACKEND" >&2; exit 2 ;;
esac

if [ "$ANALYSIS_BACKEND" = native ]; then
  step "Building native analyzer"
  "$PYTHON" snesrecomp/tools/build_native_analyzer.py
fi

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
  "$PYTHON" tools/apply_msu_patch.py --rom "$ROM" --ips "$MSU_IPS" --out "$PATCHED_ROM"
  GEN_ROM="$PATCHED_ROM"
fi

step "Regenerating 9 banks"
# --cfg-roots is the static-coverage policy (mirrors MegamanXRecomp): every
# declared `func` seeds the analysis closure so the proven surface is
# materialized as AOT; the interpreter is a failsafe for the unprovable
# remainder (e.g. the $7F8000 RAM-resident routine), never the plan of record
# for known code. Verified 2026-07-20: raises SMW to 2246 AOT variants and cuts
# runtime interp gap sites 333 -> 16 with a clean attract cycle and zero tier2
# bails / dispatch misses. The --cfg-roots mode-handler regression noted in the
# 2026-07-17 handoff was already fixed by PR #6's decoder rewrite.
"$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
    --cfg-dir recomp --out-dir src/gen --cfg-roots \
    --source-root src --source-root recomp/widescreen_aot_roots.c \
    --analysis-backend "$ANALYSIS_BACKEND"

step "Syncing funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-compare"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
      --cfg-dir recomp --out-dir "$TMP_GEN" --cfg-roots \
      --source-root src --source-root recomp/widescreen_aot_roots.c \
      --analysis-backend "$ANALYSIS_BACKEND"
  "$PYTHON" snesrecomp/tools/v2_compare_output.py \
      --expected src/gen --actual "$TMP_GEN"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

if [ "$RUN_FUZZ" -eq 1 ]; then
  step "Phase B fuzz"
  "$PYTHON" snesrecomp/fuzz/generate_snippets.py > /dev/null
  "$PYTHON" snesrecomp/fuzz/run_recomp.py
  taskkill //F //IM smw.exe > /dev/null 2>&1 || true
  "$PYTHON" snesrecomp/fuzz/run_oracle.py
  "$PYTHON" snesrecomp/fuzz/diff.py
fi

step "Done"
