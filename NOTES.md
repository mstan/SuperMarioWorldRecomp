# SSA-from-scratch — paused 2026-04-29

This branch (`ssa-from-scratch`) holds work-in-progress regen artifacts
for the SSA rollout in the sibling `snesrecomp` repo (symlinked at
`./snesrecomp/`, real path `F:\Projects\snesrecomp`). It is **not in a
working state at HEAD** — Mario is stuck in the attract demo.

See `snesrecomp/NOTES.md` for the recompiler-side detail (Steps 1-4
committed cleanly, Step 5 v2 fails visual).

## What's in this commit

`src/gen/smw_*_gen.c` (9 banks) — regen output of `bash tools/regen.sh`
against `snesrecomp` HEAD's Step 5 v2 `recomp.py` with `ssa_mode=True`.
`build/bin-x64-Release/smw.exe` built from this regen boots but does
not let Mario move in the attract demo.

## Branch layout (parent + snesrecomp, both have matching branch names)

| Branch                              | State                                                                                                              |
|-------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| `ssa-from-scratch` (this one)       | SSA Steps 1-4 committed in snesrecomp; Step 5 v2 with parent regen artifacts at HEAD. **Mario stuck in attract.** |
| `pre-phi-prealloc-baseline-backup`  | Pre-SSA + pre-phi-prealloc. Confirmed working visual baseline (proper berries, yoshi spawns, mario-1-block-under-? open). |
| `pre-phi-prealloc-baseline`         | Same as backup (active branch checkpoint).                                                                          |
| `fix-berries-yoshi-cascade`         | Pre-SSA + phi-prealloc applied. Known visual regressions: berries-as-?-blocks + yoshi-no-spawn.                     |

## How to resume SSA work

1. `git checkout ssa-from-scratch` on both parent + snesrecomp.
2. Read `snesrecomp/NOTES.md` for the recompiler-side step list and
   diagnosis notes.
3. Either continue Step 5 diagnosis (per-function bisection of which
   function breaks Mario when SSA-adopted) or roll back to snesrecomp
   commit `dae8291` (Step 4) which is the last verified-working state.

## Build & run

```bash
# Regen all 9 banks + sync funcs.h + regen registry.
cd /f/Projects/SuperMarioWorldRecomp
bash tools/regen.sh --no-tests       # add --full to also run Phase B fuzz

# Rebuild Release.
"/c/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" \
    smw.sln //p:Configuration=Release //p:Platform=x64 //m //nologo //v:m

# Boot survival test.
taskkill //F //IM smw.exe 2>/dev/null
./build/bin-x64-Release/smw.exe & SMWPID=$!
sleep 12
if kill -0 $SMWPID 2>/dev/null; then echo "ALIVE"; taskkill //F //IM smw.exe; \
else wait $SMWPID; echo "EXIT=$?"; fi
```
