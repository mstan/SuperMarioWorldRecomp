# Controller Support

SMW-recomp supports any SDL_GameController-compatible controller —
Xbox One / Series, PlayStation 4 / 5, Switch Pro, and most third-party
controllers using XInput or DirectInput on Windows.

Plug the controller in before launching. Hot-plug works too, but
launching with the controller already attached is the most reliable
path.

## Default Xbox mapping

The defaults match the physical button positions on an Xbox-style
controller (A is south, B is east, X is west, Y is north). Names
here are Xbox-side; the SNES side is what each one sends to the
game.

| Xbox button | SNES button |
| --- | --- |
| A (south) | B |
| B (east) | A |
| X (west) | Y |
| Y (north) | X |
| Left bumper (LB) | L |
| Right bumper (RB) | R |
| Back / View | Select |
| Start / Menu | Start |
| D-pad | Up / Down / Left / Right |
| Left thumbstick | Up / Down / Left / Right (8-segment, with deadzone) |

This routes the bottom face button (A on Xbox) to SNES B and the
right face button (B on Xbox) to SNES A — i.e. the **physical
position** matches a SNES controller, where A is on the right and B
is on the bottom. Most SNES emulators do this. If you'd prefer
label-true mapping (Xbox A → SNES A), edit `smw.ini` and swap the
two letters in the `Controls = ...` line.

Player 2 uses the same default layout on a second controller.

## How to rebind

1. Launch the game once. On first run, two files are auto-generated
   next to `smw.exe`:
   - `smw.ini` — system settings + gamepad mapping
   - `keybinds.ini` — keyboard mapping
2. Open `smw.ini` in any text editor.
3. Find the `[GamepadMap]` section. Edit the `Controls = ...` line
   to remap the 12 SNES buttons. Each comma-separated entry is a
   gamepad button name (see below). The 12 positions correspond to
   SNES `Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R` in
   that order.
4. Save the file and restart the game.

To disable gamepad support entirely, set `EnableGamepad1 = false`
(and `EnableGamepad2 = false` for player 2) in `[GamepadMap]`.

### Recognized gamepad button names

| Name in `smw.ini` | SDL_GameController button |
| --- | --- |
| `A`, `B`, `X`, `Y` | Face buttons |
| `L1` or `Lb` | Left shoulder / bumper |
| `R1` or `Rb` | Right shoulder / bumper |
| `L2` | Left trigger (analog, hysteresis ~12000) |
| `R2` | Right trigger (analog) |
| `L3` | Left thumbstick click |
| `R3` | Right thumbstick click |
| `Back` | Back / View / Select |
| `Start` | Start / Menu |
| `Guide` | Xbox / PS button |
| `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` | D-pad directions |

Combos (modifier buttons) work too: write `Back+Start` to bind an
action to "hold Back, then press Start" — useful for compact
controllers where Select is hard to reach.

## Two-player setup

Plug in two controllers. The game assigns the first detected one
to player 1, the second to player 2. SDL's player-index property
is honored if your controller reports one, so swapping controllers
mid-session usually does the right thing.

## Troubleshooting

- **Controller doesn't respond at all** — check that the smw.ini
  has `EnableGamepad1 = true`. If you're not sure where the file
  lives, look next to `smw.exe`. Delete it and relaunch to
  regenerate defaults.
- **Some buttons work, others don't** — your controller likely
  isn't in SDL's GameController DB. Either set the
  `SDL_GAMECONTROLLERCONFIG` environment variable with a mapping,
  or open an issue with your controller model.
- **Wrong buttons fire** — check the `Controls = ...` line in
  `smw.ini [GamepadMap]`. The 12 positions are
  `Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R`; a
  swapped pair will surface as transposed actions.
- **Need to use the keyboard alongside the controller** — both work
  simultaneously. Keyboard inputs are OR'd with the controller's,
  so either can drive any player.

## System shortcuts (keyboard only, for now)

State save/load (F1–F10 / Shift+F1–F10), fullscreen toggle, pause,
turbo, window resize, and volume are bound to the keyboard via
`smw.ini [KeyMap]`. The controller currently only drives the 12 SNES
gameplay buttons; if you'd like controller bindings for these system
actions, open an issue.
