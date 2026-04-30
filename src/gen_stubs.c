// gen_stubs.c — Residual functions that are not generated from ROM.
//
// SmwRunDecompressFromWRAM / _Entry2 — two WRAM-executed functions.
// Cartridge ROM contains no instructions at bank $7F; the game
// decompresses code into WRAM at boot and executes it from there. By
// definition, a recompiler that reads from ROM cannot generate these.
// They are modelled as HLE and will remain so permanently.

#include "common_rtl.h"
#include "cpu_state.h"
#include "funcs.h"
#include "variables.h"

// SmwRunDecompressFromWRAM ($7F:8000) — clears 128 OAM Y to $F0.
void SmwRunDecompressFromWRAM(CpuState *cpu) { (void)cpu; ResetSpritesFunc(0); }

// SmwRunDecompressFromWRAM_Entry2 ($7F:812E) — clears sprites 100-127.
void SmwRunDecompressFromWRAM_Entry2(CpuState *cpu) { (void)cpu; ResetSpritesFunc(100); }
