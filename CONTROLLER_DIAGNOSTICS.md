# Controller detection reports

Connect the controller, then run `Collect-Controller-Diagnostics.cmd` from the
extracted download. Press and release its buttons, move both sticks and use the
D-pad during the first capture. The launcher then opens for a second capture:
check its controller list and bindings, then close it. You do not need to select
a ROM or save file, or start gameplay.

The helper prints the path to a ZIP containing the device and launcher reports.
Attach it to your issue and say whether Windows lists the controller as a game
controller. Reports include raw USB joystick identities, SDL mapping status,
button/axis/hat events, selected player devices, bindings and build hashes.
They do not include your ROM or save files and do not upload automatically.
Review the reports before sharing them.

Reports normally live in `diagnostics` beside the game. If that folder cannot be
written, the helper uses `%LOCALAPPDATA%\SuperMarioWorldRecomp\diagnostics`.
