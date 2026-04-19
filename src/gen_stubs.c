// gen_stubs.c — Residual functions that are not generated from ROM.
//
// Two categories:
//
//   (A) HandleSPCUploads_Inner — intentional HLE bypass while
//       g_use_my_apu_code=true. The recompiler can generate the
//       function body correctly, but running it requires the real
//       APU emulator path to fully work first (current attempts
//       hang because the SPC IPL handshake doesn't complete —
//       outPorts[0] never receives $AA). Until that's fixed, this
//       stub keeps the HLE SPC path live. To re-enable: revert to
//       'func HandleSPCUploads_Inner 8079 end:80e8 sig:void(*p)' in
//       bank00.cfg, shrink exclude_range 8000 80E8 -> 8000 8079,
//       remove this stub, regen bank 00.
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
