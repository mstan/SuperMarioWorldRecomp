#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "snes/snes.h"
#include "variables.h"
#include "funcs.h"
#include "assets/smw_assets.h"

void SmwCpuInitialize(void) {
  if (g_rom) {
    *SnesRomPtr(0x843B) = 0x60; // remove WaitForHBlank_Entry2
    *SnesRomPtr(0x2DDA2) = 5;
    *SnesRomPtr(0xCA5AC) = 7;

    // fast rom
    static const uint8 kRevert_0xfffc[] = { 0x00, 0x80 };
    memcpy(SnesRomPtr(0xfffc), kRevert_0xfffc, sizeof(kRevert_0xfffc));
    static const uint8 kRevert_0xffea[] = { 0x6a, 0x81 };
    memcpy(SnesRomPtr(0xffea), kRevert_0xffea, sizeof(kRevert_0xffea));
    static const uint8 kRevert_0x801c[] = { 0xfb };
    memcpy(SnesRomPtr(0x801c), kRevert_0x801c, sizeof(kRevert_0x801c));
    static const uint8 kRevert_0x8713[] = { 0xb7, 0x02, 0x85, 0x01 };
    memcpy(SnesRomPtr(0x8713), kRevert_0x8713, sizeof(kRevert_0x8713));
  }
}

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .patch_bugs = NULL,
  .initialize = &SmwCpuInitialize,
  .run_frame = &SmwRunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
};
