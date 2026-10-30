SMW v0.15.4 delivers the owner-qualified shared SNES native/HLE optimization bundle.

- Pin merged snesrecomp `cc095ad4`, whose runtime matches the approved combined build. The Windows release enables the qualified shared execution, audio, rendering and coprocessor implementations while retaining their portable reference choices in source.
- Preserve the existing adaptive widescreen mod, its 16:9 enabled-mode default, Fit to screen choice and Screen-based spawning. Widescreen remains opt-in; no gameplay, controller or mod default changes.
- Include the normal Lua-enabled executable, examples, client and Lua license. The listener remains opt-in through `SNESRECOMP_LUA_PORT`.
- Include the Falcon owner-ROM helper and licenses. Owner ROMs and generated Falcon caches are not distributed.
- Preserve the separate legacy IPS co-op executable in the same Windows ZIP. That edition remains opt-in, uses its existing 4:3 presentation and has Lua disabled. It accepts an untouched SMW (USA) ROM and creates its patched execution image locally.
- Keep legacy-only adapter roots out of stock regeneration, preserving the approved stock dispatch table. Co-op regeneration continues to include its adapter.
- Native saves and user settings remain separate from the release archive. Host save states belong to their exact build; old-state compatibility is not promised.

The owner cleared MMX, Super Metroid and SMW built from this shared runtime bundle. Release packaging checks use muted isolated profiles and do not repeat the cleared human gameplay check. Performance remains workload- and machine-dependent; this release makes no original Xbox or universal frame-rate claim.

Legacy hack credits: Noobish Noobsicle, Bloony and NesDraug. See `docs/legacy-ips-coop.md` for instructions and attribution.
