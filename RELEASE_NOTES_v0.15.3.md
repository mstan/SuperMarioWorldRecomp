SMW v0.15.3 delivers the already owner-qualified shared renderer memory package through the normal engine dependency.

- Pin merged snesrecomp 31d72e1a, containing compact captured PPU registers and on-demand scene-export scratch.
- The measured SMW package reserves 1,572,864 fewer bytes of BSS (1.5 MiB). Matched stock/widescreen CPU samples were neutral/inconclusive; no release frame-rate gain is claimed.
- Keep the adaptive-widescreen mod with Fit to screen and Screen-based spawning choices, Lua examples/listener opt-in and Falcon owner-ROM helper. The widescreen feature stays disabled by default; its existing enabled-mode default remains 16:9. No new gameplay or renderer default is introduced.
- Native saves and user settings are preserved. Host save states belong to their exact build; old-state compatibility is not promised.

The owner already cleared the representative SMW gameplay check for this shared package. That evidence remains attached to its original source/binary identities. This release adds normal Windows/Linux packaging and muted runtime/current-build save-load checks without repeating the owner playtest. No ROM, owner Falcon cache, generated game sources or QA profile is distributed.
