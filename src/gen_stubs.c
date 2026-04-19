// gen_stubs.c — Residual functions that are not generated from ROM.
//
// Two categories:
//
//   (A) HandleSPCUploads_Inner — intentional HLE bypass while
//       g_use_my_apu_code=true. The recompiler can generate a
//       body for this function, but turning the real-SPC path on
//       is currently blocked by a separate emit-order gap: the
//       NextByte loop's natural fall-through from $809f (INC A)
//       into StartTransfer at $80a0 is lost because the emitter
//       places $80a0 earlier in C than $809f, so `v13++;` falls
//       off the end of the function instead of wrapping. The
//       M-flag width bug (SEP #$20 not narrowing self.A) was
//       fixed in snesrecomp@7dc2cdc and is a prerequisite for
//       re-attempting the transition. To re-enable: revert to
//       'func HandleSPCUploads_Inner 8079 end:80e8 sig:void(*p)'
//       in bank00.cfg, shrink exclude_range 8000 80E8 -> 8000
//       8079, remove this stub, fix the emit-order gap, regen
//       bank 00.
//
//   (B) SmwRunDecompressFromWRAM / _Entry2 — two WRAM-executed
//       functions. Cartridge ROM contains no instructions at bank
//       $7F; the game decompresses code into WRAM at boot and
//       executes it from there. By definition, a recompiler that
//       reads from ROM cannot generate these. They are modelled
//       as HLE and will remain so permanently.

#include "common_rtl.h"
#include "funcs.h"
#include "variables.h"

// (A) HandleSPCUploads_Inner — HLE bypass (see header).
void HandleSPCUploads_Inner(const uint8 *p) { (void)p; }

// (B) SmwRunDecompressFromWRAM ($7F:8000) — clears 128 OAM Y to $F0.
void SmwRunDecompressFromWRAM(void) { ResetSpritesFunc(0); }

// (B) SmwRunDecompressFromWRAM_Entry2 ($7F:812E) — clears sprites 100-127.
void SmwRunDecompressFromWRAM_Entry2(void) { ResetSpritesFunc(100); }
