#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "snes/snes.h"
#include "variables.h"
#include "funcs.h"
#include "assets/smw_assets.h"


static const uint32 kPatchedCarrys_SMW[] = {
  0xFE1F,
  0xFE26,
  0xFE35,
  0x1807a,
  0x18081,
  0x1A2CC,
  0x1B066,
  0x0fe79,
  0x0fe80,
  0x0fe88,

  0x1DDFB,
  0x1E0DD,
  0x2AAFB,
  0x2B05B,
  0x2B0A2,
  0x2B0A4,
  0x2B1DD,
  0x2B29B,
  0x2B2F6,
  0x3AD9B,
  0x498A2,
  0x2FBF5,
  0x2FBF7,
  0x2FC11,
  0x2FC13,
  0x2FC34,
  0x2FBFA,
  0x1D021,
  0x1D028,
  0x1B182,
  0x1FDD6,
  0x2B368,
  0x2BB3E,

  0x2C061,
  0x2C06C,
  0x2AD15,
  0x02DDA1,

  0x0399DB,

  0x1BC75,
  0x1BC78,
  0x1BC7A,
  0x2B228,

  0x2f231,
  0x2f23d,
  0x2f245,

  0x3C073,
};

static uint32 get_24(uint32 a) {
  return *(uint32*)SnesRomPtr(a) & 0xffffff;
}

void SmwCpuInitialize(void) {
  if (g_rom) {
    *SnesRomPtr(0x843B) = 0x60; // remove WaitForHBlank_Entry2
    *SnesRomPtr(0x2DDA2) = 5;
    *SnesRomPtr(0xCA5AC) = 7;

    uint8 *music = SnesRomPtr(0x8052);
    bool custom_music = music[1] != 0xE8;
    if (custom_music) {
      music[0] = 0xea;
      music[1] = 0xea;
      music[2] = 0xea;

      *SnesRomPtr(0x8079) = 0x60;  // HandleSPCUploads_SPC700UploadLoop ret 

      uint8* p = SnesRomPtr(0x8075);
      p[0] = 0x64;
      p[1] = 0x10;
      p[2] = 0x80;
      p[3] = 0xF2;

      printf("Custom music not supported!\n");

      static const uint8 kRevertProcessNormalSprites[] = { 0xda, 0x8a, 0xae, 0x92, 0x16, 0x18, 0x7f, 0xb4, 0xf0, 0x07, 0xaa, 0xbf, 0x00, 0xf0, 0x07, 0xfa, 0x9d, 0xea, 0x15 };
      memcpy(SnesRomPtr(0x180d2), kRevertProcessNormalSprites, sizeof(kRevertProcessNormalSprites));
      static const uint8 kRevertStatusBar[] = { 0xad, 0x22, 0x14, 0xc9 };
      memcpy(SnesRomPtr(0x8FD8), kRevertStatusBar, sizeof(kRevertStatusBar));
      
      if (HAS_HACK(kHack_Walljump)) {
        uint8 *wallhack = SnesRomPtr(0xa2a1);
        wallhack[3] &= 0x7f;
        wallhack = SnesRomPtr(get_24(0xa2a2));
        wallhack[3] &= 0x7f;
      }

      // Reznor platform fix
      static const uint8 kRevert_0x39890[] = { 0xee, 0x0f, 0x14 };
      memcpy(SnesRomPtr(0x39890), kRevert_0x39890, sizeof(kRevert_0x39890));

      static const uint8 kRevert_0x2907a[] = { 0xbd, 0x9d, 0x16, 0xd0 };
      memcpy(SnesRomPtr(0x2907a), kRevert_0x2907a, sizeof(kRevert_0x2907a));
      static const uint8 kRevert_0xf5f3[] = { 0xa0, 0x04, 0x8c, 0xf9, 0x1d };
      memcpy(SnesRomPtr(0xf5f3), kRevert_0xf5f3, sizeof(kRevert_0xf5f3));
      static const uint8 kRevert_0x1bb33[] = { 0xa9, 0x30, 0x9d, 0xea, 0x15 };
      memcpy(SnesRomPtr(0x1bb33), kRevert_0x1bb33, sizeof(kRevert_0x1bb33));
      static const uint8 kRevert_0x2a129[] = { 0xa9, 0x21, 0x95, 0x9e, 0xa9, 0x08, 0x9d, 0xc8, 0x14, 0x22, 0xd2, 0xf7, 0x07 };
      memcpy(SnesRomPtr(0x2a129), kRevert_0x2a129, sizeof(kRevert_0x2a129));
      static const uint8 kRevert_0x2db82[] = { 0xbd, 0xe0, 0x14, 0x99, 0xe0, 0x14 };
      memcpy(SnesRomPtr(0x2db82), kRevert_0x2db82, sizeof(kRevert_0x2db82));
      static const uint8 kRevert_0x2e6ec[] = { 0xa9, 0x38, 0x9d, 0xea, 0x15 };
      memcpy(SnesRomPtr(0x2e6ec), kRevert_0x2e6ec, sizeof(kRevert_0x2e6ec));
    }

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
  .game_id = kGameID_SMW,
  .patch_carrys = kPatchedCarrys_SMW,
  .patch_carrys_count = arraysize(kPatchedCarrys_SMW),
  .patch_bugs = NULL,
  .initialize = &SmwCpuInitialize,
  .run_frame = &SmwRunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
  .on_frame_inputs = &SmwOnFrameInputs,
  .on_finish_level = &SmwOnFinishLevel,
  .special_save_load = &SmwSpecialSaveLoad,
};
