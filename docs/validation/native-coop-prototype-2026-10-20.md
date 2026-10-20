# Experimental native co-op prototype presentation

The owner accepted the playable prototype on 2026-10-20 and authorized merging
game PR #19 to `main`. This accepts the documented alpha scope; it does not
claim completion of the full product contract or campaign qualification.

## Player-facing changes

- Package: **SMW Native Co-op (Experimental)**.
- Toggle: **Simultaneous co-op (Experimental)**, still disabled by default.
- Descriptions identify the prototype, separate saves and known rough edges.
- README explains player assignment, the mod toggle and original **2 Player**
  menus, and links the implementation, evidence and deferral list.
- Historical IPS executable/netplay instructions move to
  [legacy documentation](../legacy-ips-coop.md); they do not describe this mod.

The pinned framework's strict manifest parser has no `channel` field. The
experimental designation therefore uses its supported package/feature names
and descriptions, visible in the Mods page, without introducing an unsupported
manifest key or changing dependency revisions.

## Fresh gameplay captures

Both README images were captured from the tested Windows SDL3 executable,
SHA256 `22d47a4951281435e538e61b714233f2143767592ecdb7bdb62ecb92a79aa93b`,
using the stock USA ROM and the updated manifest. These changes affect metadata
and documentation only; no runtime rebuild is needed.

| README image | Capture source | View |
|---|---|---|
| `docs/images/native-coop-widescreen.png` | Adaptive renderer's actual composed `frame-000120.bmp` | 448 × 224 |
| `docs/images/native-coop-yoshi.png` | Native framebuffer `frames/frame_000120.bmp` | 256 × 224 |

Images are lossless PNGs enlarged 3× with nearest-neighbor sampling for readable
pixels. No gameplay elements were composited or removed. The widescreen sample
uses copied YI2 entrance state with actor positions/equipment staged for framing;
the Yoshi sample resumes an existing two-rider fixture. These are presentation
illustrations, not additional campaign/mechanic acceptance claims.

Each isolated muted run executes 155 frames, loads its copied state after
60 frames, captures frame 120 and exits normally. Both have zero dispatch
misses. Original snapshots, commands, executable/ROM hashes, logs, results and
unmodified captures remain under
`F:/Temp/Codex/smw/experimental-release/{widescreen-final,yoshi}`.
The first framing attempt is retained separately as `widescreen`.
Temporary/cache/data overrides resolve to F:; owner saves/settings are not used
as test output. No runtime pause is used.

Portable roster/state/platform-clearance, hook and storage checks pass locally;
the required Linux checks run on the final PR head and again on merged main.
Existing gameplay/build/audio evidence remains in the
[focused follow-up report](native-coop-common-followup-2026-10-20.md).
Known defects and unverified combinations remain in
[NATIVE_COOP_DEFERRED.md](../NATIVE_COOP_DEFERRED.md).
