# CFG override audit spike — tracking

**Goal:** validate every override in `recomp/bank*.cfg` against the current
recompiler. Classify each as redundant / load-bearing / wrong. Fix the
wrong ones. Strip the redundant ones. Flag framework gaps for the
load-bearing ones.

**Why:** these overrides accumulated over many iterations of buggier
discover.py and recomp.py. None have been systematically audited. The
koopa-spawn bug (#27) was one wrong `end:` directive among many; others
plausibly hide in the remaining ~1,400 overrides.

**Plan:** see `plans/floating-sauteeing-floyd.md` (via Claude session tooling).
Multi-session spike, ~8 sessions / 25 hours estimated.

## Methodology

Per override, the validator (`snesrecomp/tools/cfg_override_validator.py`):

1. Strips the override token from its cfg line.
2. Regens the bank.
3. Diffs gen-C vs baseline.

If diff is empty → **redundant**, safe to strip.
If diff is non-empty → **load-bearing**, needs human review (SMWDisX
cross-check) to decide if override is correct or wrong.

Triage via `snesrecomp/tools/cfg_override_triage.py` (reads latest
results in `snesrecomp/tools/cfg_audit_results/*.json`).

## Session log

- **2026-04-22 session 1**: Phase A complete.
  - Built `cfg_override_validator.py` (supports end/sig/rep/repx/sep/
    init_y/init_carry/carry_ret/ret_y/restores_x/y_after/x_after).
  - Built `cfg_override_triage.py` (summary + list by diff-class).
  - Ran full `end:` audit across 9 banks (~14 min). Results below.
  - Next: Phase B (strip redundant-validated end: directives; SMWDisX
    cross-check load-bearing ones).

## Per-override-type status

### `end:` (513 overrides) — Phase A done 2026-04-22

| Bank | Total | Redundant | Load-bearing | Regen-failed |
|---|---:|---:|---:|---:|
| 00 | 309 |   0 | 309 | 0 |
| 01 |   7 |   0 |   7 | 0 |
| 02 |  13 |   0 |  13 | 0 |
| 03 |   4 |   0 |   4 | 0 |
| 04 | 118 |   0 | 118 | 0 |
| 05 |  16 |   0 |  16 | 0 |
| 07 |   3 |   0 |   3 | 0 |
| 0c |  18 |   0 |  18 | 0 |
| 0d |  25 |  22 |   3 | 0 |
| **Total** | **513** | **22** | **491** | **0** |

**Observation**: banks 00 + 04 in particular show 100% load-bearing with
uniform 1745-line diffs across every override — meaning stripping ANY
one cascades through auto-promote / sub-entry-promotion for the whole
bank. This is the "cfg end: directives are load-bearing for four passes
that iterate" coupling documented in `cfg_strip_redundant.py`. Redundant-
strip is locally safe but globally coupled; strip one at a time and
re-audit iteratively.

Bank 0d has 22 genuinely-redundant `end:` directives (first batch to
strip). The 3 load-bearing on 0d are small-diff candidates that need
SMWDisX review.

### `sig:` (837 overrides) — not started
### `rep:` / `repx:` / `sep:` (27 overrides) — not started
### `init_y:` / `carry_ret` / `ret_y` / etc (17 overrides) — not started
### `exclude_range` — not started
### `dispatch` / `jsl_dispatch*` — not started
### `skip` — not started
### `no_autodiscover` — not started
### standalone `name` lines with sig — not started

## Open items — load-bearing + wrong (suspected)

Populated as SMWDisX cross-checks identify wrong overrides. Empty for now.

| Bank:Addr | Override | Why wrong | Proposed fix | Status |
|---|---|---|---|---|
| — | — | — | — | — |

## Open items — framework gaps

Load-bearing + correct overrides surface framework gaps the recompiler
should close. Populated after Phase B cross-check.

| Pattern | Overrides affected | Framework change needed |
|---|---|---|
| — | — | — |

## Recent fixes

| Date | Commit | Overrides affected | Description |
|---|---|---|---|
| 2026-04-22 | `c637d20` (parent) | 1 | auto_02_BCF8 stripped (`cfg_strip_redundant` Pass 2 with full safety) |
| 2026-04-22 | `eacf04c` (snesrecomp) | — | `cfg_strip_redundant` tool hardened with organic-discovery / standalone-name / auto-name-shape checks |
| 2026-04-21 | `791dc5e` (snesrecomp) | 13 | koopa-class `end:` directives added by cfg_apply_audit_fixes (Phase 1) |
| 2026-04-21 | `38ff1c1` (snesrecomp) | — | Phase 2 framework: discover.py per-function d_end computation, auto-promote plumbs into cfg |

## Tooling

- `snesrecomp/tools/cfg_override_validator.py` — run strip-and-diff audit.
  - `--type end --all` — full audit, ~14 min.
  - `--type end --bank 0d` — single bank, ~20s.
- `snesrecomp/tools/cfg_override_triage.py` — summarize + list results.
  - Default: print summary across all available results.
  - `--list redundant --type end` — show strippable overrides.
  - `--list load-bearing --type end --bank 00 --limit 20` — show
    cross-check candidates.
- `snesrecomp/tools/cfg_audit_results/*.json` — per-session raw results
  (gitignored, re-runnable).

## Phase progress

- [x] Phase A: tooling + `end:` audit
- [ ] Phase B: strip redundant end: (22 candidates, bank 0d), SMWDisX
      cross-check load-bearing end:
- [ ] Phase C: `sig:` audit + fix
- [ ] Phase D: `rep:`/`repx:`/`sep:` + behavioral hints
- [ ] Phase E: `exclude_range` / `dispatch` / `skip` / `no_autodiscover`
- [ ] Phase F: wrap-up + bug #8 regression check
