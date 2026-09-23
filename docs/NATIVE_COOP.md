# Native simultaneous co-op

Owner-approved specification, 2026-09-22. Implementation tracking:
`beads-8wg.3.26`, branch `feat/native-simultaneous-coop`.

This document is a permanent part of the game repository. It describes the
approved behavior, architecture, implementation progress, and validation. The
specification is not a claim that every feature below already works. The status
and evidence sections distinguish implemented behavior from remaining work.

## Product contract

An optional, default-off native mod in the normal SMW executable enables
simultaneous control when the user selects **2 Player**. Player 1 is Mario and
Player 2 is Luigi. **1 Player** remains normal solo gameplay; disabling the mod
restores stock alternating two-player gameplay. Setting changes apply at the
next session launch. The original ROM is the only gameplay source. The abandoned
IPS co-op build supplies neither code nor gameplay rules to this implementation.

The first release supports local co-op throughout stock SMW, native width and
widescreen, and the supported compiled/interpreted execution modes. Other games,
ROM hacks, online play, and character replacements are outside this release.
Incompatible character mods must be rejected explicitly rather than partly
activated. Widescreen's selected enemy-activation policy remains authoritative.

### Future player counts (owner addition, 2026-09-22)

The architecture must support a session-sized roster of N players. Two is the
initial product/input/UI limit, not a simulation or serialization invariant.
Do not encode "the other player", fixed two-element actor arrays, 1-minus-player
indexing, two-bit participation masks, or one Mario/one Luigi state fields in
the core. Use stable player IDs, explicit actor ownership, and roster iteration.
Connection/controller identities are distinct from player IDs and character IDs.

For future larger rosters, the same rules generalize as follows:

- P1 is the primary player for overworld authority and deterministic final ties;
  remaining exact ties use stable ascending player IDs.
- Team failure means no active player remains. Any eligible survivor can anchor
  recovery. Selection uses safe placement, then proximity, then stable ID.
- Camera fitting considers all active actors; exclude bubbles and dead actors.
  Resolve separation without bubbling the final surviving active actor.
- Each player owns equipment, reserve, projectile allowance, and effect timers.
  Pickups and enemy targeting compare every eligible participant.
- The mount cap and maximum grants per Yoshi source equal the session roster
  count (two in this release). Extra players do not increase ordinary item supply.
- Counters, room events, checkpoint, timer, and lives remain team state. World
  updates and rewards run once, independent of roster size.
- States store explicit record counts and IDs, with checked allocation and
  length validation. Presentation and event buffers grow with the roster.
- Future counts need input/UI/character support and performance validation;
  this work does not advertise unsupported player counts as playable.

## Agreed gameplay

### Players, camera, and recovery

| Area | Rule |
|---|---|
| Characters | Mario and Luigi have identical SMW movement physics and distinct appearances. |
| Contact | Players pass through; no player bouncing, body blocking, or carrying one another. |
| Camera | Frame active players equally within room bounds. Follow established travel direction during excessive separation; the primary player breaks ambiguous opposite-direction ties. |
| Catch-up | One-second edge warning, then bubble the trailing player. No voluntary bubble. |
| View changes | Reframe and restart separation grace after window/aspect changes. View changes never damage players. |
| Manual camera | Disable L/R camera look during co-op. |
| Autoscroll | Preserve level scrolling, pits, crushing, and hazards. Catch-up is not a rescue from failing the level's scrolling requirements. |
| Lethal/catch-up tie | Resolve lethal contact first. |
| Individual death | World and survivors continue during the death animation. After the animation, wait three gameplay seconds before safe recovery. |
| Death recovery | Return small, preserve a stored reserve, give two seconds of damage protection; pits and crushing remain lethal. |
| Catch-up recovery | Preserve equipment and mount, drop held objects at departure, and grant no new protection. |
| Placement | Prefer safe footing. Permit safe water/supported-air recovery only when the returning actor can survive there. Wait otherwise. |
| Mounted bubble | Wait for clearance for rider and mount together. |
| Team failure | Bubbles are out of play. No active players means one shared life lost, once, followed by entrance/checkpoint restart. |
| Retry | All restart small with stored reserves intact; clear mounts and carried objects from the failed attempt. |
| Input on return | Held movement applies immediately; jump, fire, grab, and reserve release require fresh presses. |
| Timers in bubbles | Temporary benefits keep expiring with gameplay time; preserved Yoshi mouth timers are the exception. |

All durations use simulation time. Pause, message boxes, shared transformations,
and frozen entrance/cutscene phases do not consume gameplay countdowns.

### Resources, mounts, and interactions

- Share lives, coins, score, Dragon Coins, and bonus stars. Use normal SMW
  starting lives, maximum counters, game over, and continue behavior.
- Power-up state and reserve slots are individual. Stars benefit their collector
  only. Give every fire-powered player the normal independent fireball allowance.
- Keep original ordinary block supply. Exclusive contested pickups go to the
  nearest eligible player; exact ties use primary/stable player order.
- Nonlethal damage drops the damaged player's reserve above them; either player
  can collect it. Lethal damage preserves a reserve still in storage. Previously
  released items remain world objects and are not also preserved in storage.
- Mounts belong to the shared world. Either player can ride any unoccupied Yoshi.
  At most two mounted/unmounted Yoshis exist in a room for this release; bubbles
  retain ownership of their transported mount and count toward the cap.
- Each Yoshi source can grant two mounts. Keep the block visibly usable for its
  remaining reward; a second hit releases the second egg when the cap permits.
  At the cap, use the normal alternate reward. Do not duplicate grant/egg state.
- Each mount has its own mouth contents, abilities, and swallow timer. Catch-up
  preserves contents and the remaining swallow timer. A surviving mount stays
  in the world when its rider dies. Objects held by another player can only be
  swallowed after release.
- Kicked shells and other normal world hazards can hurt any player.
- Ordinary enemies select the nearest eligible active player and keep a target
  through committed attacks. Audit player-facing behavior, including Boos.
- Same-frame attacks cause one damage event per enemy, respecting existing
  invulnerability. Every valid stomping player gets a bounce; defeat rewards
  occur only once.
- Reactive platforms combine riders; equal opposing influences cancel. Simple
  occupied triggers activate once. Same-frame ON/OFF activations switch once.
- Fence sides are individual. Player-relative darkness illuminates each active
  actor, combining areas; preserve scripted lighting.

### Transitions, progression, and special scenes

- Any player can initiate a valid pipe, door, or exit for the team. Apply normal
  entrance rules individually to each held object and mount. Waiting bubbles
  travel to the destination and return after the entrance sequence when both
  recovery delay and safe-placement requirements are met.
- Either player activates the shared checkpoint and upgrades every small player
  to big, including actors awaiting recovery. Preserve stronger power-ups.
- Share the level timer. Timeout costs one life and restarts the team.
- Conflicting same-frame exits favor a secret exit, then primary/stable player
  order. A valid level clear wins over same-frame death; a dead player returns
  small without charging a team life.
- Goal tape awards bonus stars once per clear; simultaneous touches use the
  highest valid result. Both players participate in bonus games; rewards are
  shared.
- Bosses retain normal health and timing. Aimed attacks choose the nearest active
  actor at attack start; scripted attacks remain scripted. Repeated recovery
  beside a survivor remains available in bosses.
- Mario controls one overworld party position and level choice; Luigi visibly
  follows. Preserve the normal lead in castle destruction and ending scenes;
  retain all participants' state when gameplay resumes.
- Power-up/damage transformations briefly freeze the world. Messages freeze it
  and either player can dismiss them after the normal delay with fresh input.
- Either player can pause/resume. Only Mario can invoke the normal early-exit
  command in an eligible already-completed level.

## Menus, input, and persistence

With the mod enabled, select player count **before** save files. Reuse SMW's
original player-count and save-file screens, including their art, text, cursor,
input, and sound. One Player opens normal files; Two Player opens the separate
co-op files through the same regular file menu. When disabled, keep the original
order. Select the persistence namespace before loading file lists or writing
campaign/automatic-resume data.

Owner correction, 2026-09-22: no custom co-op menu overlays or extra co-op file
labels. This supersedes the initial custom count selector and file banner. Keep
the separate save storage. The custom assignment overlay was also removed.

Co-op saves are unique and separate from normal SRAM, state slots, and resume
files. New co-op campaigns start fresh; no import/copy feature in this release.
Use normal SMW save opportunities and preserve the normal risk of losing unsaved
progress on game over. Never write co-op extra state into normal SRAM.

Both players need usable controller/keyboard assignments before gameplay. Shared
keyboard bindings qualify. An assigned controller disconnect freezes the session
for reconnection/reassignment. Inputs must not remain latched through reassignment.

Save states support the full session, including all actors, mounts, bubbles,
ownership, source grants, pending outcomes, camera state, and timers. Validate
ROM/mode/schema and complete payloads before committing a load. Reject normal
states in co-op, co-op states in normal play, unsupported/corrupt states, and
incomplete actor records without partly modifying the running session.

## Architecture and implementation

### Boundaries

SMW's original player RAM, graphics upload, camera, sprite interactions, and
death/transition state are shared single-player machinery. Calling the complete
level update once per player would duplicate enemies, timers, rewards, and
transitions. Co-op instead owns explicit actor state and brokers shared events.

1. An activation plugin enables a session controller only for selected co-op.
2. A dynamic roster contains stable player IDs and independently owned player
   state. Team state owns counters, checkpoint, timer, and outcomes.
3. Scoped adapters expose audited original player logic and terrain collision;
   preserve registers/scratch memory and classify every shared side effect.
4. Run world logic once per frame; run player logic once per active actor.
   Collect competing interactions before deterministic resolution.
5. Capture independent graphics/palette/animation data before guest state is
   reused. Render actors independently with native-width/widescreen depth,
   fence, spotlight, and Mode 7 handling.
6. Versioned state records and preflight validation make loads transactional.

Hooks must cover compiled and interpreted paths, compose with widescreen hooks,
and be generated reproducibly. Required sites are verified. Never hand-edit
`src/gen/` or build on the abandoned patched-ROM implementation. General engine
capabilities belong in the shared framework; SMW rules remain in the game.

HUD: two labeled reserve slots in this release, shared counters once, readable
player/bubble identity and separation warnings. Actor draw storage and HUD models
must accept a roster rather than assume two.

### Sequence

1. Package/session foundation, permanent specification, player ownership audit,
   isolated saves, roster/policy tests, and stock baseline.
2. Native dual-actor movement/collision, rendering, shared camera, deterministic
   contacts, and input routing.
3. Complete recovery, resources, mounts, all room transitions, enemies/bosses,
   overworld, bonus games, and special scenes.
4. Full save-state support, execution-path parity, compatibility, packaging.
5. Campaign-level acceptance evidence before closing the issue.

### Initial player stage adapter

The first live adapter is deliberately a development milestone, with the
following audited boundaries. The world update is not replayed for each actor.

| Stage | Current treatment |
|---|---|
| NMI, input polling and overworld | Original once-per-frame path. |
| Level entry | Seed each actor from the original entrance state; retain its own equipment on later room entries. |
| `00:C47E` before `00:C569` | Original world/keyhole/effective-frame/shared timer work runs once. |
| `00:C569` through `00:C592` | Scoped original player animation/motion/terrain, reserve-release and note-block state run for each actor. |
| Secondary timer fields | Advance only the audited per-player timer bytes, with the original cadence. |
| Normal sprites | Each original update runs once with a selected actor context; complete contact and attack-target arbitration remains in progress. |
| Extended sprites | Original world update runs once; ownership and multi-actor contact integration remain in progress. |
| Player graphics | Primary draw/upload runs normally. Secondary body/cape draws run in a scoped scratch/OAM context; independent pictures join the OBJ renderer. |

Each scoped call preserves the caller's registers, scratch bytes and stack
balance, while retaining elapsed machine clocks and bus state. Original player
routines can produce shared effects: room changes, transformations, projectiles
and terrain rewards still require their event/ownership adapters. Individual
death and recovery adapters are under live validation as described below.
The initial motion result does not establish those behaviors as correct.

The regular launcher exposes both input seats. `[GamepadMap] KeyboardPlayers`
persists its keyboard assignments (bits 0 and 1 are the current two host seats);
reloading hotkeys preserves those assignments. Recorded input accepts `p1.` and
`p2.` button prefixes, for example `press p2.right+p2.b 45`. Unprefixed buttons
retain their existing player-1 meaning. These are host input limits, separate
from the dynamic simulation roster.

### Independent graphics

`coop_presentation.c` captures the ROM's body/cape pieces after selecting each
actor's original pose and palette. The wrapper enters after Yoshi/shared music
work, retains only audited player fields, and restores scratch/OAM before the
primary draws. It resolves the original `MarioGFXDMA` sources into private
picture memory. NMI and world sprite graphics still run once. Seven body/cape
pieces is the original draw routine's maximum, checked at runtime; the actor
list remains dynamic.

The framework's opt-in `PpuExtraObject` capability adds private RGB15 pictures
at the OBJ stage. It preserves native sprite ordering, background priority,
windows, brightness and color math. The legacy, fast and Mode 7 renderers use
the same private colors. The SMW widescreen renderer uses the same compositor.
With no extra objects, the existing native path and snapshot layout stay intact.
This is actor rendering; it does not add menus or a co-op UI overlay.

Each actor stores both its pending guest draw and its visible draw latched at
NMI. The native `CNR1` container now uses schema version 2 to serialize both,
including coordinates, piece attributes and pixels. Preflight bounds-checks
counts and attributes before replacing the session. Development snapshots from
schema 1 are rejected; separate campaign SRAM remains compatible. Full mount,
ownership and event state remains part of the unfinished integration work.

### World contacts, camera and recovery

The normal-sprite scheduler (`01:8127`) runs each entity once, binding the nearest
active actor while the original routine executes. Exact distance ties use stable
player IDs with primary-player priority. This supplies the actor context for the
original AI and contact code; simultaneous contact collection, committed attack
targets, carried-object ownership and special sprite policies remain unfinished.
The per-actor carry/platform occupancy fields are cleared at `01:808C` with the
original cadence. That entry is explicitly interpreter-only in the dispatch
table, and the hook verifier checks that fact instead of accepting absent code.

The character selector at `$0DB3` also selects score/progression slots in stock
SMW. Gameplay always uses the shared party slot; character selection is confined
to drawing. Collision centers now use the original `03:B664` clipping table:
small actors have a 12-pixel box starting 20 pixels below `$96`, large actors a
26-pixel box starting six pixels below it. `$96` is the pose-frame origin.

Camera focus uses the active roster's bounds and the established travel leader
when players cannot fit. The original camera code retains smoothing, scrolling
settings and layer parallax. Edge checks use the actual rendered viewport;
viewport changes restart separation grace. The normal 256-pixel screen clamp
at `00:E9A1` is replaced by room boundaries during free scrolling. The terrain
crush check at `00:E9FB` and original autoscroll path remain. Autoscroll, vertical
rooms and resize transitions still require live acceptance evidence.

Lethal damage has two audited entrances: `00:F606` supplies the initial enemy/
crush velocity, while `00:F60A` preserves pit fall velocity. Both are hooked:
the generated F606 block includes the F60A instructions without a second block
boundary. Intercepting F60A alone missed compiled enemy deaths; a live contact
trace exposed the shared freeze, and the adapter now covers both entrances.
The death animation retains the original falling motion and four-frame timer
cadence while omitting the global music/freeze and individual life charge.
The primary death timer is excluded from the otherwise once-per-world timer
stage, matching the cadence of secondary actors.

After event resolution, the adapter checks separation and safe recovery. The
read-only Map16 query follows the original collision pointers and P-switch
remapping. It checks actual footing and body clearance, including collidable
Layer 2, and waits when live sprites occupy the recovery area. Foot height is
preserved when a small actor returns beside a large survivor. The current query
is conservative: slope, moving-platform, mounted and supported-air placement
need their remaining adapters. These cases are not yet acceptance-complete.
Team failure enters the original level loader with the shared checkpoint flag
and one life charged. Game-over presentation uses the original game modes.

### Status and evidence

- Worktree created from `main` at `f36d99f73d775febcf396e874b86a264c25d91da`.
- Beads issue claimed. This specification includes the later N-player requirement.
- Implemented the standalone dynamic-roster policy core in `src/mods/coop`,
  including camera/separation, recovery, team failure, shared event arbitration,
  fresh-action input handling, and explicit actor field capture/bind.
- `tools/test_native_coop.py` passes with `-Wall -Wextra -Werror` at roster sizes
  2, 3, 4 and 17. Tests cover deterministic contested contacts, one-time shared
  outcomes, death/clear priority, safe recovery, checkpoint upgrades, disconnects,
  and scoped guest WRAM ownership. CRC-protected little-endian state records
  round-trip and reject every single-byte corruption and truncated prefix without
  replacing the destination session.
- Fresh stock generation and the unmodified native game baseline build passed
  using the pinned framework and SDL3/MinGW. No IPS image was used.
- Added a default-off development package to the normal executable, with the
  same menu boundaries instrumented in generated C and the interpreter. Live
  scripted runs confirm player-count selection precedes the native file list,
  separate co-op files load, and missing input assignments stop entry to gameplay.
  The first UI used custom overlays; the owner's later correction replaces these
  with the ROM's existing menu routines and removes all co-op menu overlays.
- Persistence selects `saves/native-coop-v1` for co-op and `saves` for solo.
  The small `last-mode.txt` in the co-op directory selects the correct namespace
  before automatic resume on a subsequent launch. With no previous selection,
  SRAM and snapshots remain unavailable until the player chooses a count.
  Changing namespaces clears the SRAM buffer before reading the selected file.
- Native actor-image records store player ID and input seat separately. The
  codec includes the stock ROM identity and an ownership-layout signature;
  tests cover reordered/non-contiguous IDs and 2/3/4/17 players. Invalid,
  truncated, corrupt, and duplicate-owner records leave the destination intact.
- Shared framework work lives on `feat/native-coop-runtime`, issue
  `beads-8wg.2.68`: optional integrity/mode envelopes, preflight/session gates,
  save-stream error propagation, and composable interpreter hooks. A redirected
  hook restarts opcode classification at its destination. The bridge contract
  harness passes 119 checks, including composition and redirected calls/returns.
- Corrected the playtest script's mod-state location to
  `mods/preloaded/state.toml`; the prior `mods/state.toml` was ignored. Added
  optional game arguments for recorded input and finite live runs. Existing
  settings/saves remain in `build-adaptive/playtest`.
- Current menu evidence: `build-adaptive/playtest/native-coop-stock-menu-check`
  (ignored local artifacts), frames 390 and 480, shows the ROM's original count
  screen and regular file menu with no co-op overlay/banner. The earlier
  `native-coop-menu-check` captures are superseded. Live slot 3 to slot 4 restore
  also passed through the guarded loader, restoring two native actor records.
  Native movement/rendering, mount adapters, complete state payloads, and the
  campaign acceptance matrix remain under implementation.
- Implementation is in progress. No playable-completion or campaign-validation
  claim has been made. Update this section as code and evidence land.
- The initial native movement adapter builds and runs through the intro,
  overworld, and Yoshi's Island 2. The recorded `native-coop-seats` run contains
  422 level frames per actor: Mario moves from x=24 to x=16 under P1 input,
  Luigi moves from x=24 to x=93 under P2 input, and each jumps independently.
  Both finish on the terrain at y=360 in that trace's old center convention
  (the corrected collision-box center is y=378); the guest stack remains 511. The trace
  is an ignored local artifact at `build-adaptive/playtest/native-coop-seats.csv`.
  Its frame-680 screenshot predates secondary rendering and shows only Mario.
  This is movement evidence,
  not full co-op acceptance.
- `tools/test_coop_input.py` passes against the actual config implementation:
  shared keyboard assignment, launcher assignment changes, hotkey reload, and
  write/read persistence. The existing roster/state tests also continue to pass.
- Independent graphics now run in a fresh 3,400-frame menu-to-level recording.
  `native-coop-draw-check` frames 3130, 3190 and 3300 show both actors, separate
  jumps and original palettes. The shared timer follows its original cadence.
  The earlier concern about a zero timer was a misreading of the small capture:
  magnification and the recorded RAM show 394, 393, 391 and 390 as expected.
- Live schema-2 state restore preserves both actors and their graphics.
  `native-coop-256-check/frame-000090.bmp` verifies the 256-pixel PPU path;
  `native-coop-compiled-check` verifies compiled guest dispatch. The 200-frame
  restore/input run produces byte-identical actor traces with default interpreter
  dispatch and `SNESRECOMP_LLE_BOUNCE=1`. These bounded checks do not validate
  the remaining rooms, abilities or interaction rules.
- Framework `tests/ppu/test_extra_objects.py` passes private-color, opaque-black,
  transparency, native/Mode7, legacy/fast, background depth, windows, color math,
  clipping, rotated OAM and stable 17-object ordering checks. The existing PPU
  composition regression matches its previous digest `436319d369c4a1e3`.
- The 2026-09-23 normal-sprite/camera/recovery build verifies 11 compiled hook
  sites and one explicitly interpreter-only entry. `tools/test_coop_hooks.py`
  checks idempotence and rejects missing or partly compiled coverage claims.
  Terrain tests cover small recovery beside a large anchor, hazards despite
  protection, world sprites, collidable Layer 2, P-switch remapping, bounds,
  truncated ROM data and a query leaving all WRAM unchanged.
- Live `native-coop-recover-mario.csv` and `native-coop-recover-luigi.csv` runs
  each contain 1,040 level frames per actor. The dying actor enters death at
  simulation frame 672, finishes the original animation at 863, waits 180
  gameplay frames and recovers at 1043. The survivor and world keep advancing;
  lives stay at five and the stack stays at 511. The Luigi trace also records
  exactly 120 protection ticks on return. Screenshots in the matching `-check`
  directories show the survivor continuing and the recovered actor at safe
  footing. Characters can occupy the same position after recovery.
- `tools/check_coop_recovery_trace.py` validates those live lifecycle, world
  cadence, life, protection and stack invariants. The 1,100-frame Luigi recovery
  run produces byte-identical actor traces with default dispatch and
  `SNESRECOMP_LLE_BOUNCE=1`. The latter runs the scheduler through compiled
  dispatch; scoped interpreter calls can call compiled guest functions in both
  modes, which is why both lethal entry points require instrumentation.
- Native slots 12 (during death) and 13 (during the recovery delay) were saved
  and restored in `native-coop-recover-state`. Its 1,248 repeated actor records
  match the uninterrupted baseline with zero divergences. No dispatch misses
  were recorded. This validates those state phases, not the unfinished mount,
  transition, enemy-contact and campaign coverage.
- In `native-coop-individual` both actors die before recovery; the shared life
  count changes once from five to four and the native loader returns the team
  to Yoshi's Island 2. The earlier `native-coop-enemy-death` trace exposed the
  missed compiled kill entrance and is superseded by the validated builds above.

## Acceptance matrix

| Area | Required evidence |
|---|---|
| Inert mode | Disabled mod and enabled-mod solo match stock behavior; normal saves unchanged. |
| Roster | Core tests at 2, 3, 4, and a larger count; no partner-only assumptions or fixed participation masks. |
| Input | Independent controllers/shared keyboard, connection loss, reassignment, fresh actions on release. |
| Simulation | Trace world tick once per frame; deterministic event outcomes independent of actor update order. |
| Rooms | Horizontal, vertical, water, autoscroll, Layer 2, moving platforms, fences, darkness, Mode 7 bosses. |
| Abilities | Cape/flight, balloon, swimming, spin jump, independent fireballs, stars, reserve behavior. |
| Recovery | Single/simultaneous death, catch-up, hazards on boundary, mounts, unsafe placement, timeout, game over. |
| Progress | Checkpoint upgrade/retry, doors/pipes, conflicting exits, completion/death tie, secret exits. |
| Contacts | Pickup arbitration, double stomp, ON/OFF ties, projectile ownership, two mouth states and egg grants. |
| Special | Every boss, bonus games, overworld party, castle destruction, ending. |
| Persistence | Normal/co-op namespace isolation; state round trips at transitions, bubbles, transformations, bosses; invalid load is atomic. |
| Presentation | Native width and supported wide aspects, viewport resize, depth/palettes, darkness, both spawn settings. |
| Execution | Compiled/interpreted paths and no-mod stock oracle comparisons. |
| Delivery | Build/package checks; sustainable original frame cadence; campaign verification with recorded evidence. |

Use live trace rings, recorded inputs, and screenshots; never pause/step a running
game for investigation. Use `tools/run_adaptive_renderer.ps1` in this worktree,
preserve `build-adaptive/playtest` settings/saves, and keep Screen-based spawning
for owner playtests. Normal in-game pause/disconnect/message behavior requested
by the owner is a product feature, not permission to pause for instrumentation.

All stock gameplay remains in scope. An exceptional room needs an implementation,
not silent fallback to alternating turns or removal of a player. Original SMW
behavior governs aspects not deliberately changed above.
