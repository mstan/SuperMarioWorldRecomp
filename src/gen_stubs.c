// gen_stubs.c — (intentionally empty)
//
// SmwRunDecompressFromWRAM / _Entry2 ($7F:8000 / $7F:812E) used to be modelled
// here as HLE forwarders to ResetSpritesFunc(). They are now recompiled
// LITERALLY (LLE) from the captured WRAM snapshot as guarded AOT bodies: the
// `ram_routine 7F8000 ...` directive in recomp/bank00.cfg appends the snapshot
// to the ROM image via a synthetic reloc region, the recompiler emits the real
// bodies into src/gen/bank7f_v2.c, and runtime dispatch is gated on a live
// WRAM byte-match (g_ram_routine_guards). Defining them here as well would be a
// duplicate-symbol (LNK2005) conflict with the generated bodies, so this file
// no longer defines anything. ResetSpritesFunc itself remains a normal ROM-side
// body in src/smw_00.c and is unaffected.
//
// This TU is retained (referenced by CMakeLists.txt) as a home for any future
// genuinely-non-generated residual bodies.
