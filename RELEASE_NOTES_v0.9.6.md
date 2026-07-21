# Super Mario World Recompiled v0.9.6 — shared ImGui launcher

This release replaces the old launcher path with the current shared Dear ImGui
`recomp-ui` launcher. The pre-boot UI is now a self-contained SDL/OpenGL ImGui
window with DPI-independent controls, addressing the missing full launcher
reported under Steam Deck/Proton in issue #6.

- Shared SNES launcher profile with ROM selection and verification.
- Accessible controller, hotkey, audio, display, widescreen, and MSU-1 options.
- Current launcher fonts, controller art, and game box art bundled in the
  Windows package.

Gameplay behavior is otherwise unchanged from v0.9.5 plus the current main
branch fixes.
