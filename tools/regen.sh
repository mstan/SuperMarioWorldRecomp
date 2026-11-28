#!/usr/bin/env bash
# Shared generation from the original image; co-op is an in-memory input overlay.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${SNESRECOMP_ROOT:-$ROOT/snesrecomp}"
variant=stock; fuzz=0; options=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --stock) variant=stock;; --coop) variant=coop;;
    --quick|--v2) ;; --full) fuzz=1;;
    *) options+=("$1");;
  esac
  shift
done
[ -z "${SMW_ROM:-}" ] || options+=(--rom "$SMW_ROM")
[ -z "${SMW_GEN_DIR:-}" ] || options+=(--out-dir "$SMW_GEN_DIR")
if [ "$variant" = coop ]; then
  [ "$fuzz" = 0 ] || { echo "--full fuzz is supported only for stock" >&2; exit 2; }
  options+=(--cfg-dir recomp/coop)
fi
sh "$ENGINE/tools/regen_project.sh" --project-root "$ROOT" "${options[@]}"
if [ "$fuzz" = 1 ]; then
  PYTHON="${PYTHON:-python3}"
  "$PYTHON" "$ENGINE/fuzz/generate_snippets.py"
  "$PYTHON" "$ENGINE/fuzz/run_recomp.py"
  "$PYTHON" "$ENGINE/fuzz/run_oracle.py"
  "$PYTHON" "$ENGINE/fuzz/diff.py"
fi
