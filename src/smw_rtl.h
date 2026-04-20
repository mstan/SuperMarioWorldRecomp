#ifndef SMW_SMW_RTL_H_
#define SMW_SMW_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

extern int g_dbg_ctr_mine;

enum {
  kSmwRam_APUI02 = 0x18c5,
  kSmwRam_my_flags = 0x19C7C,
};

extern bool g_smw_playback_mode;

// RtlGameInfo hooks (see snesrecomp/runner/src/common_cpu_infra.h).
void SmwOnFrameInputs(uint32 inputs);
void SmwOnFinishLevel(void);
bool SmwSpecialSaveLoad(int cmd, int slot);

void SmwRunOneFrameOfGame_Internal();
void SmwSavePlaythroughSnapshot();

void SmwDrawPpuFrame(void);
void SmwRunOneFrameOfGame(void);

#pragma pack (push, 1)
typedef struct OwExits {
  uint16 field_0;
  uint16 field_2;
  uint8 field_4;
} OwExits;

typedef struct SpriteSlotData {
  uint8 field_0;
  uint16 field_1;
  uint16 field_3;
} SpriteSlotData;

typedef struct LevelTileAnimations {
  uint16 field_0;
  uint16 field_2;
  uint16 field_4;
} LevelTileAnimations;

#pragma pack (pop)

typedef struct GenTileArgs {
  uint8 r6, r7;
  uint16 r8;
  uint16 r12, r14;
  uint16 r10;
  uint8 *ptr_lo_map16_data;
} GenTileArgs;

#endif  // SMW_SMW_RTL_H_