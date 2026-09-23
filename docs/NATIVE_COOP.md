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

With the mod enabled, select player count **before** save files. One Player shows
normal files; Two Player shows clearly labeled co-op files. When disabled, keep
the original order. Select the persistence namespace before loading file lists
or writing campaign/automatic-resume data.

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
- Implementation is in progress. No playable-completion or campaign-validation
  claim has been made. Update this section as code and evidence land.

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
