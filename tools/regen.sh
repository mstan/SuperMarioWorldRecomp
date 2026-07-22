#!/usr/bin/env bash
# Full regeneration pipeline for the stock and simultaneous co-op variants.
#
# Generated bank C is intentionally untracked. The stock build continues to
# use recomp/ + src/gen; the opt-in co-op build layers recomp/coop/*.cfg onto
# the stock CFGs and emits into src/gen-coop. The variants never share a ROM
# analysis image or generated source directory.
#
# Flags:
#   --stock                 regenerate the normal 1P/MSU build (default).
#   --coop                  regenerate the simultaneous co-op build.
#   --quick                 default. Skip Phase B fuzz.
#   --full                  also run Phase B fuzz (stock only).
#   --no-tests              skip the framework test suite.
#   --strict-idempotent     regenerate into a temp dir and byte-compare.
#   --v2                    accepted for compatibility; v2 is always active.
#   -h | --help             this message.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_FUZZ=0
RUN_TESTS=1
STRICT_IDEMPOTENT=0
VARIANT=stock
for arg in "$@"; do
  case "$arg" in
    --stock)             VARIANT=stock ;;
    --coop)              VARIANT=coop ;;
    --quick)             RUN_FUZZ=0 ;;
    --full)              RUN_FUZZ=1 ;;
    --no-tests)          RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
    --v2)                ;;
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

if [ "$VARIANT" = coop ] && [ "$RUN_FUZZ" -eq 1 ]; then
  echo "regen.sh: --full fuzz is currently supported only for --stock" >&2
  exit 2
fi

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

GEN_ROM="$ROM"
CFG_DIR="recomp"
OUT_DIR="src/gen"
FUNCS_HEADER="recomp/funcs.h"

if [ "$VARIANT" = coop ]; then
  # The co-op and MSU patches are alternative analysis inputs. Generate from
  # stock+co-op only, matching the image prepared by the runtime patcher.
  COOP_IPS="recomp/coop/smw_coop.ips"
  PATCHED_ROM=".build/smw_coop.sfc"
  step "Applying simultaneous co-op patch (recomp/coop/)"
  "$PYTHON" tools/apply_msu_patch.py --rom "$ROM" --ips "$COOP_IPS" \
    --out "$PATCHED_ROM" \
    --expect-sha256 0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b
  GEN_ROM="$PATCHED_ROM"
  CFG_DIR=".build/recomp-coop"
  OUT_DIR="src/gen-coop"
  FUNCS_HEADER="recomp/coop/funcs.h"

  step "Assembling stock CFG plus co-op overlays"
  CFG_ABS="$ROOT/$CFG_DIR"
  EXPECTED_CFG_ABS="$ROOT/.build/recomp-coop"
  if [ "$CFG_ABS" != "$EXPECTED_CFG_ABS" ]; then
    echo "regen.sh: refusing to replace unexpected CFG directory: $CFG_ABS" >&2
    exit 1
  fi
  rm -rf -- "$CFG_ABS"
  mkdir -p "$CFG_ABS"
  cp recomp/bank*.cfg "$CFG_ABS/"
  # The patched ROM replaces stock $00:86DF with Lunar Magic's pointer-call
  # trampoline. Remove stock cross-bank aliases before the co-op overlay names
  # the same address; emitting two public aliases for one exact variant would
  # leave the non-canonical wrapper with no matching variant definition.
  sed -i '/^name 0086df GameMode14_InLevel_0086DF$/d' "$CFG_ABS"/bank*.cfg
  for overlay in recomp/coop/bank*.cfg; do
    dest="$CFG_ABS/$(basename "$overlay")"
    if [ -f "$dest" ]; then
      printf '\n' >> "$dest"
      sed '/^bank[[:space:]]*=/d; /^includes[[:space:]]*=/d; /^comment[[:space:]]*=/d' "$overlay" >> "$dest"
    else
      cp "$overlay" "$dest"
    fi
  done
else
  # The normal build remains the existing MSU-capable 1P image.
  MSU_IPS="recomp/msu1/smw_msu.ips"
  if [ -f "$MSU_IPS" ]; then
    PATCHED_ROM=".build/smw_msu1.sfc"
    mkdir -p "$(dirname "$PATCHED_ROM")"
    step "Applying MSU-1 patch (Conn, audio-only - recomp/msu1/)"
    "$PYTHON" tools/apply_msu_patch.py --rom "$ROM" --ips "$MSU_IPS" --out "$PATCHED_ROM"
    GEN_ROM="$PATCHED_ROM"
  fi
fi

step "Syncing $VARIANT funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir "$CFG_DIR" \
  --out "$FUNCS_HEADER"
if [ "$VARIANT" = coop ]; then
  cp "$FUNCS_HEADER" "$CFG_DIR/funcs.h"
fi

# Feed only handwritten runtime sources into host-root discovery. Scanning the
# whole src/ tree would let src/gen and src/gen-coop discover each other's
# generated symbols, coupling the variants and overflowing Windows argv limits.
SOURCE_ROOT_ARGS=(--source-root recomp/widescreen_aot_roots.c)
while IFS= read -r source; do
  SOURCE_ROOT_ARGS+=(--source-root "$source")
done < <(find src -type f -name '*.c' \
  ! -path 'src/gen/*' ! -path 'src/gen-coop/*' | sort)

step "Regenerating configured banks ($VARIANT)"
"$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
  --cfg-dir "$CFG_DIR" --out-dir "$OUT_DIR" --cfg-roots \
  "${SOURCE_ROOT_ARGS[@]}" \
  --analysis-backend "$ANALYSIS_BACKEND"

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-compare"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
    --cfg-dir "$CFG_DIR" --out-dir "$TMP_GEN" --cfg-roots \
    "${SOURCE_ROOT_ARGS[@]}" \
    --analysis-backend "$ANALYSIS_BACKEND"
  "$PYTHON" snesrecomp/tools/v2_compare_output.py \
    --expected "$OUT_DIR" --actual "$TMP_GEN"
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

step "Done ($VARIANT)"
