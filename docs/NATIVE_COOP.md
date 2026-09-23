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
| `00:D273` room request | Collect entrant IDs, resolve primary/stable priority once, publish the selected actor's coordinates to the original room loader. |
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

### Shared pipe and door destination

`00:D273` normally increments `SublevelCount` and starts the level fade
immediately. The adapter records a request from the bound actor, leaving both
actor updates able to submit requests. After resolving gameplay events, it
commits at most one request using the roster's primary/stable-ID priority.
`05:D796` chooses the exit table entry from `$95` or `$97` (horizontal/vertical
screen). Only the selected entrant's `$94..$97` coordinates are published into
guest loader state. Canonical actor images retain separate equipment and
reserves; the original fade and loader still run once.

The request exists only within the current host frame. Snapshots during the
fade already contain the committed guest loader inputs and all canonical
actors. The machine's `room` field and CSV `level_data` record `SpriteDataPtr`
at `$CE..$D0`. This is diagnostic only: empty rooms can share a sprite list.
It is not a unique room identity. Transitions are detected through native mode
changes; `LoadingLevelNumber` is reused by the ROM after loading. The ownership
adapter now captures the complete nine-bit level number from `$0E..$0F` at
`05:D8B7`, before the loader multiplies it into a table index. Machine `level`
and CSV `level` retain that identity (unknown for an imported older state until
its next entrance). The CSV includes this identity, the sprite pointer, selected
entrant, sublevel count, mode, and each reserve for validation.

This boundary covers the shared pipe/door request routine. Door, vertical-room,
mount transport, additional carried-object entrance types, waiting-bubble,
conflicting room/clear, and other exit
paths still need their own live acceptance evidence and remaining adapters.

### Shared checkpoint

The `00:F2CD` hook receives only a valid midway-tape contact, before the original
tile removal, sparkle, sound, and collector upgrade. It records a core
checkpoint event; frame-end resolution upgrades every small actor, preserves
stronger equipment and reserves, and remembers the upgrade for waiting actors.
An actor still in its death animation keeps the small death pose and receives
the big form on recovery. The event applies once per frame regardless of roster
size. The native checkpoint key is `TranslevelNo + 1`, with zero for no checkpoint.

Immediate retries bypass stock `04:8F35` overworld checkpoint publication. Before
retrying, the adapter transfers a live `MidwayFlag` into bit `$40` of that
translevel's `OWLevelTileSettings`. The original `05:D796` loader then selects
the midpoint entrance. A new primary level entry resynchronizes the core key
from the selected level's own flag, avoiding a stale checkpoint from another
level. The restart policy still makes every actor small and retains reserves.

The regular launcher exposes both input seats. `[GamepadMap] KeyboardPlayers`
persists its keyboard assignments (bits 0 and 1 are the current two host seats);
reloading hotkeys preserves those assignments. Recorded input accepts `p1.` and
`p2.` button prefixes, for example `press p2.right+p2.b 45`. Unprefixed buttons
retain their existing player-1 meaning. These are host input limits, separate
from the dynamic simulation roster.

### Goal tape and shared victory sequence

The goal tape moves and draws once. At `01:C0C2`, after motion and before its
player crossing tests, the adapter runs the original post-X/bottom-Y checks
against each eligible actor image. `01:C0E7` records accepted crossings before
music, freeze, sprite conversion, or reward effects. The tape's custom-contact
flag makes `01:A7E4` a contact query; the original clipping/fence/animation
rules determine whether the actor also touched the moving tape for stars.

Frame-end core resolution selects secret before normal, then primary/stable
player ID. The highest valid tape result is retained separately from the exit
owner. The ROM table at `07:F1AA` stores packed BCD (including `$50` for fifty
stars); that representation is monotonic and is the adapter's event value.
The winning tape's original `01:C0E7` effects run once. At `01:C107`, its contact
branch uses the collected team result; `07:F252` receives the highest valid
tape height and executes the original star and exceptional three-life reward.
The original sprite-to-coin conversion also runs once.

Only the primary actor executes the shared `00:C915` victory director: palette
fade, score conversion, music, peace-sign phase, spotlight, and game-mode change.
Other actors use the original actor motion and peace-pose routines, keeping
their own equipment and positions. Primary executes first even if serialized
actor records are reordered. Dead/waiting actors return small with their own
reserves; catch-up actors retain their equipment. A valid clear overrides
same-frame death/timeout without charging a life. The next gameplay entry
returns the core outcome to normal and preserves the surviving equipment.

Goal spheres use the same actor collection at `01:8773`, after their single
graphics/sparkle update. Each actor runs the native contact query, including its
alternate-frame contact rule. `01:8778` records accepted contacts before the
one-time sphere removal/music/end-timer effects. If a sphere wins an exit tie
while another actor validly touches a tape, that tape's accepted-bonus tail at
`01:C109` still runs once. It preserves the chosen exit and supplies the team's
star reward and original coin conversion. Equal star rewards select the
primary/stable actor. The stock sprite-slot bound is separate from roster size.

Goal collection state is temporary within one host frame; committed scene
state is already in the guest and native actor snapshot. No snapshot schema
change or custom UI is needed. Mounted keyhole activation, mount
rewards, bonus-room participation, and full campaign coverage still require
their own adapters and validation.

### Native keyhole completion

At `01:E1C8`, query every active actor's explicitly owned, carried key. The
stock global scan chooses only the highest occupied key slot, which can hide
another player's valid contact. Each candidate instead enters `01:E1F3` with
its own key slot. The original carried-status, clipping, overlap and keyhole
cooldown checks remain authoritative. `01:E210` records an accepted secret exit
before shared effects; `01:E23A` stops queries before graphics. The hole is
drawn once by its ordinary sprite update. This iterates the dynamic roster;
normal-sprite slot capacity is independent of player count.

The existing broker chooses secret before normal, then primary/stable actor.
Only the chosen hole executes the original `01:E210` timer, music, freeze and
window-center effects. `00:C47E` continues to own the whole keyhole sequence
once per world frame. Core gameplay clocks stop, and pause input cannot
interrupt a committed exit. `00:C9FE` retains the original secret-exit value.
Other actors keep their equipment; dead/waiting actors return small with their
own reserves. A same-frame valid clear prevents timeout from charging a life.

A simultaneous tape reward needs an extra handoff: a keyhole skips the normal
score-tally scene and freezes score-sprite credit timers. After the accepted
tape tail, its BCD stars are credited by the original `05:CF05` counter writer,
stopping at `05:CF36` before tally graphics/phase changes. The bounded credit
loop supplies the routine's eligible frame phase and restores the true-frame
byte afterwards; it advances no gameplay ticks. For fifty stars, `02:AD22`
records the exact native 3-up popup slot. The original `02:AE03` life writer
queues its reward once, stopping at `02:AE38` before popup motion. That popup's
timer moves past its consumed credit threshold, preventing duplicate lives
after restoration/unfreeze. Ordinary stars, life queues, timers and pictures
already belong to the complete snapshot; no new save format is required.

The stock status-bar handler still applies the hundred-star threshold. If that
earns a bonus room, keyhole completion at `00:C9FE` uses the normal bonus-room
entrance settings while preserving the secret route. The next gameplay entry
restores the whole roster in the original bonus room. Completing every bonus
interaction and mounted/Yoshi-mouth key activation remain separate work.

### Native scripted completion

A zero-to-nonzero transition of the original `EndLevelTimer` registers the
completion already produced by a native boss or switch script. Actor updates
record the actual initiating player for switch activation; a world-only boss
script uses the primary party lead. The original script and its shared effects
are not replayed. Donut Secret House's native boss-secret rule comes from
`00:C9FE` (`CutsceneID` with translevel `$13`). A pending valid tape contact can
still supply its reward independently of a scripted exit's ownership.

The policy accepts an explicit exit while gameplay clocks are frozen, provided
the session is connected and has no committed outcome. Other gameplay events
remain rejected in that state. Resolving the exit advances no recovery,
protection, or frame counters and emits one terminal action. If a dead actor
owns the winning exit, returning actors use a surviving primary/stable actor's
pose instead of the dead owner's position. Full all-dead/scripted-scene placement
and per-boss acceptance remain outstanding.

The Yellow Switch Palace was validated in the original room `$0CA`, using
native falling contact and the original switch tiles. Core scripted-exit tests
cover completion while frozen; this does not establish every boss as validated.

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
NMI. The native `CNR1` container introduced schema version 2 to serialize both,
including coordinates, piece attributes and pixels. Preflight bounds-checks
counts and attributes before replacing the session. Development snapshots from
schema 1 are rejected; separate campaign SRAM remains compatible. Schema 3 now
adds explicit entity ownership, described below. Mount and projectile behavior
remain part of the unfinished integration work.

### World contacts, camera and recovery

The normal-sprite scheduler (`01:8127`) runs each entity once, binding its owner
for a carried/attached object, otherwise the nearest active actor. Exact distance ties use stable
player IDs with primary-player priority. This supplies the actor context for the
original AI and contact code; simultaneous contact collection, committed attack
targets and special sprite policies remain unfinished.
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

Shared controls are gathered before the original level-mode handler. Start is
available to an actor awaiting recovery; either player can pause/resume, using
the original debounce and sounds. Select for the stock early-exit command stays
on the primary player's input. Message dismissal combines fresh action presses
and uses the original message-box handler and delay. No additional menu is drawn.
Gameplay countdowns begin at the actual post-pause branch, so both the pause and
resume frames have the correct timing. Frozen entrance/scene phases also leave
co-op gameplay countdowns unchanged. Returning from game over resets the session
lifecycle and equipment before the next level; full continue/campaign coverage
remains part of acceptance testing.

The hook manifest can declare an interior boundary's audited containing entry.
The verifier accepts interpreter coverage only while every variant of that entry
is explicitly interpreter-only. If code generation later compiles any variant,
the interior hook must appear in the generated blocks or the build fails.

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
- Shared-control hooks add two interior sites in interpreter-only `GM14Level`:
  pause handling (`00:A21B`) and the gameplay branch (`00:A28A`). The build now
  verifies 11 compiled sites and three interpreter sites. Hook tests also reject
  absent or partly compiled containing entries for interior-hook coverage.
- `native-coop-shared-pause` loads a recovery-delay state. P2 presses Start while
  awaiting recovery; P1 resumes. Across 92 paused host frames, the world frame
  stays at 20 and recovery stays at 46. Recovery still occurs at simulation
  frame 1043 with 120 protection ticks. The input recorder adds a release frame
  between commands; the script's one press plus 90-frame wait spans 92 frames.
  This exercises the requested in-game pause feature, without debugger pausing
  or stepping.
- In `native-coop-message-dismiss-check`, P2 jumps into Yoshi's House's original
  message block and dismisses it with a later fresh B press. Frames 2270/2310
  show the native message and a stationary world counter; frame 2440 shows the
  message closed and gameplay resumed. The fresh menu run also verifies the
  unchanged original count/file screens. No dispatch misses were recorded.
- `tools/make_coop_pipe_fixture.py` makes an explicitly staged **copy** of a
  local schema-2 YI2 entrance state (slot 9). It places one actor on the open
  pipe in screen F and the other on screen E, or places both on the pipe. It
  assigns distinct big/fire power-ups and mushroom/flower reserves. This is an
  offline test fixture, not evidence of naturally reaching that location.
  The source state is unchanged; the live run uses ordinary Down input and the
  original collision, pipe animation, fade and loader.
- `native-coop-pipe-mario.csv` and `native-coop-pipe-luigi-fixed.csv` each verify
  either actor can take the team from sprite data `$07C532` to `$07C57F` (YI2's
  underground room), with exactly one sublevel increment, five lives, balanced
  stack 511, and independent equipment/reserves retained. The Luigi run has
  582 destination actor records and is byte-identical with scheduler bounce
  disabled/enabled (`native-coop-pipe-luigi-floor.csv` versus `-fixed.csv`).
- `native-coop-pipe-both-fixed` verifies simultaneous entry selects primary ID
  0 and increments the sublevel count once. Slot 15 is saved during the outgoing
  fade, then restored; all 398 destination actor records from the first pass
  match the replay in order, including the frozen entrance animation and
  later independent movement. Frame 610 shows both actors in the underground
  room. `tools/check_coop_pipe_trace.py` checks these invariants. Counters freeze
  during pipe animations, so replay validation compares ordered records rather
  than treating gameplay/world counters as a unique frame key.
- Those pipe runs use the dispatch-boundary correction below. The earlier
  `native-coop-pipe-both.csv` run exposed a skipped compiled sprite handler and
  is not acceptance evidence. Corrected runs recorded no dispatch misses or
  unresolved-abandon messages. Hook coverage is now 12 compiled sites and
  three interpreter sites.
- The shared offline `tools/coop_fixture.py` parser checks guard/native/core
  checksums and field-layout identity before creating copied fixtures. Both
  pipe and `make_coop_checkpoint_fixture.py` use it. No live state is patched.
- `native-coop-checkpoint-waiting` stages small Mario in a recovery wait while
  small Luigi approaches YI2's tape through normal Right input. At simulation
  frame 436 both upgrade; Mario retains the upgrade flag while waiting and
  returns big at frame 582 with 120 protection ticks. Reserves remain mushroom
  and flower, lives remain five. In `native-coop-checkpoint-mario`, Mario's
  tape contact upgrades himself and preserves fire Luigi throughout.
- `native-coop-checkpoint-state` saves after the upgrade while Mario still
  waits. All 484 repeated actor records match after restore, including return
  size/protection and reserve ownership. `native-coop-checkpoint-retry` starts
  from the recovered checkpoint state with the timer staged to expire. It
  charges exactly one shared life (five to four), publishes the midpoint flag,
  and restarts both small at the original YI2 midpoint entrance (center X 2328),
  with separate reserves intact. Frame 880 shows the restarted team. The timer
  was changed only in an offline copied state, not during execution.
- `tools/check_coop_checkpoint_trace.py` checks checkpoint upgrades, preserved
  stronger power, pending recovery upgrades, exact state replay, and midpoint
  retry invariants. No dispatch misses or unresolved-abandon messages occurred
  in these runs. Hook coverage is now 13 compiled and three interpreter sites.

### Goal tape validation (2026-09-23)

- `native-coop-goal` stages positions near YI2's original goal and uses real
  controller input. Luigi owns the single exit while Mario remains behind;
  powers big/fire, reserves mushroom/flower, five lives and stack 511 persist.
  Frame 550 shows both actors in the original course-clear walk. The complete
  victory trace reaches mode `$0C` with one shared timer decrement every two
  frames, one peace phase, and one spotlight close.
- `native-coop-goal-contacts` uses an explicit offline fixture with a second
  copy of the naturally loaded goal sprite. Simultaneous normal exits choose
  Mario, while Luigi's fifty-star contact wins the reward over Mario's six.
  The original fifty-star graphic is visible at frame 140; exactly three lives
  are awarded (five to eight). This synthetic conflict is not campaign evidence.
- `native-coop-goal-secret-timeout` makes Luigi's tape secret and expires TIME
  on the contact frame. The result is Luigi's secret exit and fifty stars;
  Mario returns small, Luigi stays fire, both retain reserves, and no life is
  charged. Captured WRAM confirms TIME 000 and `SecretGoalTape=1`. Both scheduler
  settings produce identical 1,126 actor records. The final life total is nine:
  native `05:CC77` independently grants one life when the existing bonus-star
  tens digit matches both final time digits (zero/00), in addition to the tape's
  three lives. The checker explicitly accounts for this original rule.
- `native-coop-goal-waiting` starts Mario in a recovery bubble. Luigi's clear
  brings Mario back small immediately for the scene, preserving his mushroom
  reserve and awarding the same single fifty-star reward.
- `native-coop-goal-state` restores the same mid-victory snapshot twice;
  all 936 actor records through the scene transition match exactly.
  `native-coop-goal-reenter` finishes the clear, returns to the overworld, and
  enters YI3 using the normal selection button. Its 1,246 subsequent actor
  records resume normal gameplay with big Mario/fire Luigi and separate
  reserves; a fresh Luigi input moves only Luigi. Frame 1410 shows both actors
  in that next level.
- `tools/make_coop_goal_fixture.py` stages an entrance copy near the goal or,
  with `--contacts normal|secret`, copies a naturally loaded goal into an empty
  stock sprite slot for conflict tests. Capture that source with a normal
  scripted save before crossing; script state slots are 0..15. `--timeout`
  sets the divider to 0, because `UpdateStatusBar` decrements it before its BPL
  test. These tools never modify or pause a running guest.
  `tools/check_coop_goal_trace.py` validates owner/reward priority, single scene
  cadence, equipment, stack, life totals, replay, and execution-mode equality.
  No dispatch misses or unresolved-abandon messages occurred in accepted runs.
  Hook coverage is 18 compiled and three interpreter sites.

Goal sphere extension:

- `native-coop-sphere` uses an offline fixture replacing the loaded goal with
  the stock sphere type and its ROM tweaker data. Real Luigi movement triggers
  the single team clear; all 1,196 actor records pass the same victory,
  equipment, stack, and life invariants. Frame 70 shows the sphere before touch.
- `native-coop-goal-mixed` stages Mario on a sphere and Luigi on a fifty-star
  tape in one contact frame. Mario wins the normal-exit tie, and Luigi's tape
  still awards fifty stars and exactly three lives. Frame 140 shows the original
  fifty-star graphic with both actors. All 1,126 actor records are identical
  under the default and compiled-bounce scheduler settings.
- The mixed fixture aligns the true-frame parity with the sphere's slot:
  stock sphere contacts run on alternate frames, whereas tape contacts run
  every frame. This preserves the original contact query. These fixtures
  validate the adapter; Sunken Ghost Ship water/campaign coverage remains open.
  `--sphere` and `--contacts normal --mixed-sphere` reproduce the two cases.
  Build and hook checks pass with 20 compiled and three interpreter sites;
  accepted runs have no dispatch misses or unresolved-abandon messages.

Native switch scene extension:

- `tools/make_coop_palace_fixture.py --entrance` stages a copied state for the
  original room loader, selecting screen exit `$CA` with the overworld submap
  selector zero. `05:D796` derives the level's high byte from that selector;
  a nonzero selector would load `$1CA`, the YI2 underground room. Save the loaded
  room normally, then use the tool without `--entrance` to stage players above
  the existing yellow switch. The tool verifies its actual Map16 tile and
  unpressed switch flags; it adds no switch tiles or replacement scene.
- `native-coop-palace` starts Mario waiting and lets Luigi fall onto the switch.
  One exit belongs to Luigi, with native end timer 8. Mario returns small with
  mushroom reserve; Luigi remains fire with flower reserve. The original yellow
  flag becomes 1, the stock message is visible at frame 230, and native mode `$0B`
  is reached with five lives. After the clear, core frame/recovery clocks remain
  frozen while the original scene progresses. All 1,378 actor records match in
  `native-coop-palace-compiled`. No dispatch misses occurred.
- `tools/check_coop_palace_trace.py` checks the one-time activation, complete
  8-to-0 scene countdown, unchanged shared lives, preserved equipment, waiting
  actor return, frozen core clock, and scheduler equality. The C core tests also
  reject ordinary gameplay events while frozen and verify an explicit scripted
  exit resolves once without advancing recovery/protection clocks.
- `native-coop-goal-dead-winner` adds same-frame timeout to the normal-goal tie.
  Mario still owns the winning exit, returns small beside surviving Luigi, and
  retains his reserve. Luigi's fifty-star reward is preserved. All 1,126 actor
  records pass the exit/cadence/life checks, including the native TIME 000
  matching-digit life; no death life is charged.

### Carried-object ownership and transport

`CoopMachine` owns a dynamic entity registry. Stable entity IDs are distinct
from reusable native sprite slots, with explicit owner, target, kind, type and
ownership flags. Reinitializing a slot at `07:F722` retires its old identity and
clears old carrier/mount references. The normal and extended slot bounds are
stock engine capacities (12 and 10), independent of the session roster count.
The registry is also the foundation for later mount/projectile adapters; those
behaviors are not implied by the existence of their record flags.

Every normal sprite runs once through the scoped call, including sprites bound
to the primary actor. A native pickup changing status to `$0B` records that
actor as owner. An object already being carried always binds its owner, even
when another actor is nearer. Native release, throw, death, removal and
conversion retire carried ownership. Throw-block allocation uses the bound
actor at native sprite initialization. A balloon uses attached ownership,
without consuming the actor's exclusive hand-held object reference; complete
balloon interaction rules still need validation.

Occupancy bookkeeping mirrors `01:808C`: `$1470` (`CarryingFlag`) receives
`$148F` (`IsCarryingItem`), then `$148F` is cleared for the coming sprite pass.
The initial secondary adapter had these two bytes reversed; the source/symbol
audit corrected that ordering. The native pickup and carried routines then
publish each actor's own flags.

Catch-up executes the native `01:A015` Down-release branch before recovery
changes the actor's coordinates, leaving the object at departure. A failed
attempt discards all owned entities before entering the loader, including an
object held by a survivor when the shared timer expires. Clear arbitration
still takes priority over retry cleanup.

The native cleanup at `02:ABF2` assumes one carried object and moves it into
slot zero. Its co-op adapter retains the owned carried records, runs the shared
cleanup once, then recreates every transported object with the same native
`07:F7D2` initializer and preserved type, position and palette. New slots are
assigned deterministically; logical IDs and owners survive. Room initialization
seeds each actor's carrying flags from its own object. No unowned carried sprite
is assigned to an arbitrary nearby player.

`CNR1` schema 3 appends an `ENT1` section after the actor pictures: record count,
next identity, persistent level number, then 28-byte little-endian records
(`id`, `kind`, `slot`, `type`, `owner`, `target`, `flags`). Preflight checks ID and
slot uniqueness, bounds, valid player references, mutually exclusive ownership
flags and both directions of held/mount references. The entire native container
remains CRC protected and load remains transactional. Schema 2 imports only
when its actors have no held, mounted or attached state; ownership cannot be
reconstructed safely from an old single-player slot. Ordinary campaign SRAM
and its separation from co-op SRAM are unchanged.

Focused evidence (2026-09-23, copied/staged fixtures, original pickup routines):

- `native-coop-carry-near.csv`: 560 actor records. Luigi retains the same key
  during nine frames when Mario is nearer and also holds grab. After release,
  Mario picks up that same ID. Slot 4 captures actual held ownership and both
  native carry flags in schema 3.
- `native-coop-carry-pipe.csv` and `native-coop-carry-pipe-compiled.csv`:
  **1,444 identical records** with bounce disabled/enabled. Big Mario and fire
  Luigi independently pick up keys in slots 7 and 4; Luigi enters the YI2 pipe.
  Both keys arrive in level `$1CA`, relocated to slots 1 and 0, with unchanged
  logical IDs/owners, equipment and reserves. Slot 6 preserves both held keys.
- `native-coop-carry-state.csv`: load that two-key snapshot twice and replay
  the same input; **306 consecutive actor records match exactly**. The initial
  neutral load input releases the objects through native logic; the saved
  registry and guest flags are separately verified as held at the boundary.
- `native-coop-carry-catchup.csv`: 580 records. Luigi holds the key through
  separation grace 59, drops it at x=99 on frame 463, then safely returns at
  x=4748 on frame 486 with his power/reserve and no new protection.
- `native-coop-carry-timeout.csv`: 1,008 records. Timeout charges one shared
  life (5 to 4), clears the surviving carrier's ownership before retry, and
  returns both players small to level `$106`, keeping reserves 1 and 2.
- The simultaneous goal/death/TIME-000 regression still passes all 1,126
  arbitration/victory records after this scheduler change. All accepted runs
  retain stack `$01FF` and produce no dispatch misses.
- Portable entity/snapshot tests pass at rosters 2/3/4/17, including slot reuse,
  forged duplicate IDs, wrong owners, every-byte corruption and truncation.

Reproduction tools: `make_coop_carry_fixture.py`, `check_coop_carry_trace.py`, and
the shared schema-2/3 `coop_fixture.py` parser. `--pipe` deliberately stages two
stock keys; `--catchup` stages excessive separation; `--timeout` stages TIME 001
with sixty ticks left on its divider. These are focused cases, not campaign
coverage. An immediate script `savestate` after a `press` now captures the last
held frame before the script's neutral release frame; delayed snapshots retain
their explicit delay. This makes held-object snapshots reproducible.

Remaining work includes all loose-object contact candidates when players
overlap, every carried-object/entrance type,
balloon conflicts, mounted keyholes, mounts, projectiles and full campaign validation.

### Carried goal rewards

The original `00:FB00` gift table reads the currently bound player's power,
reserve and riding state. At a co-op clear, each invocation now binds the
carried object's explicit owner, cancels that actor's star as the original
clear does, and executes the unchanged native conversion once. The completed
registers and scratch return to the original world conversion loop; the
previous actor context is restored. This applies to the roster, with no
Mario/Luigi selection built into reward conversion.

The gift's `07:F722` initialization retires the carried object's identity. Its
native status is briefly still `$0B` during initialization, before `00:FB00`
sets `$0C`; that interval must not claim the replacement as another carried
object. A scoped conversion guard prevents this false claim. The replacement
is a new, unowned world reward that either eligible actor can collect, and the
former carrier's hand reference and both carrying flags are cleared. Snapshot
schema 3 already represents all of this state.

The first two-shell test exposed the old failure: converting Mario's object
while Luigi was the goal winner attempted to assign both objects to Luigi and
hit the exclusive-ownership check. The corrected adapter completes the scene.
Focused validation (2026-09-23):

- `native-coop-goal-carry.csv` / `native-coop-goal-carry-floor.csv`: **1,194
  identical actor records**. Both original pickup routines acquire a shell;
  Luigi crosses the naturally loaded tape. Big Mario has a flower reserve and
  gets the native mushroom gift. Big Luigi has a mushroom reserve, so the
  native duplicate-item rule makes his gift a 1-up. The viewed running frame
  capture shows the two different rewards above their former carriers.
- `native-coop-goal-carry-swapped.csv`: swapping reserves swaps those gifts,
  with Luigi still owning the exit. The saved native sprite slots are `$0C`,
  contain the expected distinct types, have new unowned IDs, and retain no
  carried references or flags. The original 1,194-record victory completes.
- `native-coop-goal-carry-replay.csv`: restoring the actual converted-gift
  snapshot repeats **1,100 identical actor records** through native victory,
  without resurrecting either shell. All successful runs have stack `$01FF`
  and no dispatch misses. Release build passes; hook coverage is now 31
  compiled boundaries and three interpreted boundaries.

`tools/make_coop_goal_carry_fixture.py` stages loose shells and reserves in an
offline copy of a naturally loaded YI2 goal. Other nearby enemies are removed
from that fixture; a four-frame native sprite contact cooldown lets the
scripted grab arrive before neutral input would kick the shells. This isolates
the conversion case. Hold `y+p2.y+p2.right` for 45 frames, save, and allow the
native victory to finish. `--swap` exchanges reserves.
`tools/check_coop_goal_carry.py` checks both the running trace and actual saved
registry; `--replay` checks two loads of that converted state. Mounted reward
selection still requires the independent mount adapter and its own evidence.

### Keyhole validation

Focused evidence (2026-09-23). The keys are picked up by original routines;
holes, conflicting tapes and exceptional starting counters are explicit offline
fixture inputs, not claims about original level layouts. All runs use the
original ROM, Screen-based spawning, an uninterrupted runtime and stack `$01FF`,
with no dispatch misses. Release build, hook coverage (30 compiled boundaries
and three interpreted boundaries), and core tests for 2/3/4/17 actors pass.

- `native-coop-keyhole.csv` / `native-coop-keyhole-compiled.csv`: 320 identical
  actor records. Luigi's slot-4 key opens the hole while Mario retains his
  separate slot-7 key. The accepted clear leads to exactly 122 world frames of
  native window animation, game mode `$0B`, secret exit `$02`, five lives,
  unchanged equipment/reserves and no accepted pause during the scene. Running
  frame captures show the native keyhole window and its center.
- `native-coop-keyhole-both.csv`: two accepted keyholes on the same frame,
  one commit, Mario wins, 320 actor records. `native-coop-keyhole-waiting.csv`
  has Luigi finish while Mario awaits recovery; Mario returns small, keeping
  reserve 1, and Luigi retains fire/reserve 4.
- `native-coop-keyhole-conflict.csv`: simultaneous normal tape and keyhole at
  TIME 000. Luigi's secret exit wins; Mario's native timeout death still makes
  him small, but no shared life is charged. A dead actor's invalid physical
  tape touch awards no stars. The two valid exit requests resolve once.
- `native-coop-keyhole-tape.csv`: simultaneous live Mario tape contact earns
  fifty stars while Luigi wins the secret exit. The actual shared star counter
  receives 50, and the native life queue grants exactly three lives (5 to 8).
  The keyhole cadence stays 122 frames, with no time-score tally substituted.
- `native-coop-keyhole-secret-tie.csv`: marking that tape as another secret exit
  instead chooses Mario. The tape's original victory sequence runs, no keyhole
  window starts, and 1,126 actor records pass the existing goal validator.
- `native-coop-keyhole-bonus.csv` / `native-coop-keyhole-bonus-floor.csv`:
  **1,174 identical actor records** through the same mixed exit starting at
  eighty stars. The result is eight lives, thirty remaining stars, secret route
  `$02` and original bonus-room `$100` entry with both actor states intact.
  A running capture confirms the original bonus-room presentation.
- `native-coop-keyhole-replay.csv`: an actual mid-keyhole state repeats 166
  identical actor records. `native-coop-keyhole-reward-replay.csv` repeats 200
  records from a snapshot with stars already credited and two lives still in
  the native queue; final totals remain fifty stars and eight lives.

Reproduction tools: `tools/make_coop_keyhole_fixture.py` and
`tools/check_coop_keyhole_trace.py`. From an unowned YI2 entrance copy, the
default fixture has two loose keys and Luigi's hole; `--both` supplies both
holes; `--waiting` makes Mario await recovery. Hold `y+p2.y+p2.right` for Luigi's
case or `y+right+p2.y+p2.right` for both. Save during the 122-frame animation
for the replay check. For conflicts, first capture an actual two-key state
while holding `y+p2.y`; use that copy with `--contacts`, optionally `--timeout`,
`--tape-secret`, or `--bonus-start 80`. The added hole uses a higher sprite slot
so its native query observes the held keys before the script's neutral load
frame releases them. The trace's `exit_candidates` proves simultaneous contact;
star/life counters distinguish a recorded award from a credited one.

### Disabled-mod input correction

Tracked as `beads-8wg.3.27`. The two-key compiled/interpreted comparison exposed
an existing inactive Falcon hook at `01:80D2`: `smw_falcon_clear_carry_bridge`
unconditionally removed Y/Down from the native controller bytes, preventing
Mario from grabbing a key. A running pre-opcode trace localized the change
between `01:80AC` and `01:80AF`, before the co-op sprite adapter. The temporary
probe was removed after diagnosis.

The helper now clears its private latch and returns without editing input when
Falcon is inactive. Its four cleanup call sites (handoff, after-physics fallback,
sprite pass, and state-load paths) share that guard; active Falcon cleanup keeps
its existing behavior. `falcon_kick_guard_test` now exercises all 256 native
controller-byte combinations across inactive physics, sprite and load hooks.
The focused test passes alongside its existing active Punch/Kick/Dive checks.
Its sprite-only fixture supplies empty-terrain stubs for the newer block-query
dependencies. The repaired two-key runtime comparison above proves the original
compiled failure no longer occurs. No framework/compiler change was required.
The standalone Windows `test/falcon_kick_guard/build.bat` points to the current
game-owned `src/foreign_controller.c` so this regression remains reproducible.

### Compiler table-boundary correction found by co-op validation

The compiled simultaneous-entry test exposed an existing dispatch decoder bug
at `01:85C8`: the sprite-main table was inferred as 54 entries. Unused sprite
`$36` points to declared data at `01:E41F`, but valid sprites `$37..$C8` follow
it. The emitted out-of-range path skipped their handlers. The authoritative
inline table occupies `01:85CC..875E`, already declared in `recomp/bank01.cfg`.

The framework decoder now uses an aligned data region beginning exactly at an
inline dispatch table as its byte boundary. Within that boundary, an unused
data-target slot retains its index and target; data targets remain ineligible
for compilation and use the interpreter if reached. Without an exact table
boundary, the existing conservative inference remains in effect. This also
prevents reading adjacent data as additional table entries. No ROM bytes or
generated C were hand-edited.

An audit of 86 stock-ROM inline `ExecutePtr`/`ExecutePtrLong` sites found these
17 changed entry counts. The other 69 are unchanged. The byte boundaries come
from existing game metadata, not new per-sprite exceptions.

| Dispatch site | Containing label | Previous entries | Corrected entries |
|---|---|---:|---:|
| `01:8133` | HandleSprite | 14 | 13 |
| `01:8179` | CallSpriteInit | 203 | 201 |
| `01:85C8` | CallSpriteMain | 54 | 201 |
| `01:BDE6` | Magikoopa | 7 | 4 |
| `01:C550` | TouchedPowerUp | 10 | 6 |
| `01:D119` | CODE_01D116 | 3 | 2 |
| `01:D75E` | CODE_01D75C | 4 | 3 |
| `02:B008` | CallGenerator | 16 | 15 |
| `02:B3AC` | CODE_02B3AB | 7 | 3 |
| `02:D40B` | Layer3SmashMain | 7 | 5 |
| `02:DCDD` | CODE_02DCB7 | 6 | 4 |
| `02:DFBE` | CODE_02DF93 | 4 | 3 |
| `02:E132` | CODE_02E0CD | 4 | 3 |
| `03:8A48` | BowserStatue | 5 | 4 |
| `03:9244` | FallingSpike | 3 | 2 |
| `0C:C9BC` | CODE_0CC9B3 | 7 | 6 |
| `0C:CA45` | CODE_0CCA2F | 5 | 4 |

The 17 focused decoder/padding/PHK-PER-dispatch tests pass, including new short
and long table regressions with an interior unused data target and a plausible
pointer immediately beyond the table. Regeneration emits 3,237 exact AOT and
622 interpreter variants; the Windows release build passes. Full campaign
coverage remains outstanding. The owner approved the post-class-fix review
required by `NES/PRINCIPLES.md` section 8b on 2026-09-23. The framework fix is
committed as `84177df` on the isolated `feat/native-coop-runtime` branch.

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
