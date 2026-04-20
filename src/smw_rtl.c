#include "smw_rtl.h"
#include "recomp_state.h"
#include "variables.h"
#include "config.h"
#include <time.h>
#ifdef SMW_ORACLE
#include "../../../tools/oracle/oracle.h"
#endif
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "funcs.h"
#include "debug_server.h"

bool g_smw_playback_mode;
static int g_smw_playback_ctr = (1) * 2 - 1; // 36

static const char *const kSmwBugSaves[] = {
  "playthrough/1_1",
};

static void SmwLoadNextPlaybackSnapshot(void) {
  char name[128];
  for (int i = 0; i < 100; i++) {
    g_smw_playback_ctr++;
    sprintf(name, "saves/playthrough/%d_%d.sav", g_smw_playback_ctr >> 1, (g_smw_playback_ctr & 1) + 1);
    if (RtlLoadSnapshot(name, true)) {
      printf("Playthrough %s\n", name);
      return;
    }
  }
}

void SmwOnFrameInputs(uint32 inputs) {
  // APUI02 reflection — the ancilla code tests this at WRAM $18c5.
  uint8 apui02 = RtlApuReadReg(2);
  if (apui02 != g_ram[kSmwRam_APUI02]) {
    g_ram[kSmwRam_APUI02] = apui02;
    RtlRecordPatchByte(&g_ram[kSmwRam_APUI02], 1);
  }
  // Whether controllers are plugged in (top two bits of input word).
  uint32 new_my_flags = inputs >> 30;
  if (new_my_flags != g_ram[kSmwRam_my_flags]) {
    assert(new_my_flags <= 255);
    g_ram[kSmwRam_my_flags] = (uint8)new_my_flags;
    RtlRecordPatchByte(&g_ram[kSmwRam_my_flags], 1);
  }
}

void SmwOnFinishLevel(void) {
  if (!RtlIsReplayMode() && g_config.save_playthrough) {
    SmwSavePlaythroughSnapshot();
    RtlClearKeyLog();
  }
  if (g_smw_playback_mode)
    SmwLoadNextPlaybackSnapshot();
}

bool SmwSpecialSaveLoad(int cmd, int slot) {
  if (cmd == kSaveLoad_Replay && slot == 256) {
    g_smw_playback_mode = true;
    SmwLoadNextPlaybackSnapshot();
    return true;
  }
  if (slot >= 256) {
    int i = slot - 256;
    if (cmd == kSaveLoad_Save || i >= (int)(sizeof(kSmwBugSaves) / sizeof(kSmwBugSaves[0])))
      return true;
    char name[128];
    sprintf(name, "saves/%s.sav", kSmwBugSaves[i]);
    printf("*** %s slot %d: %s\n",
      cmd == kSaveLoad_Save ? "Saving" : cmd == kSaveLoad_Load ? "Loading" : "Replaying", slot, name);
    RtlLoadSnapshot(name, cmd == kSaveLoad_Replay);
    return true;
  }
  return false;
}

const uint8 *ptr_layer1_data;
const uint8 *ptr_layer2_data;
uint8 ptr_layer2_is_bg;



void AddSprXPos(uint8 k, uint16 x) {
  AddHiLo(&spr_xpos_hi[k], &spr_xpos_lo[k], x);
}

void AddSprYPos(uint8 k, uint16 y) {
  AddHiLo(&spr_ypos_hi[k], &spr_ypos_lo[k], y);
}

void AddSprXYPos(uint8 k, uint16 x, uint16 y) {
  AddHiLo(&spr_xpos_hi[k], &spr_xpos_lo[k], x);
  AddHiLo(&spr_ypos_hi[k], &spr_ypos_lo[k], y);
}

uint16 GetSprXPos(uint8 k) {
  return PAIR16(spr_xpos_hi[k], spr_xpos_lo[k]);
}

uint16 GetSprYPos(uint8 k) {
  return PAIR16(spr_ypos_hi[k], spr_ypos_lo[k]);
}

void SetSprXPos(uint8 k, uint16 x) {
  spr_xpos_hi[k] = x >> 8;
  spr_xpos_lo[k] = x;
}

void SetSprYPos(uint8 k, uint16 y) {
  spr_ypos_hi[k] = y >> 8;
  spr_ypos_lo[k] = y;
}

void SetSprXYPos(uint8 k, uint16 x, uint16 y) {
  SetHiLo(&spr_xpos_hi[k], &spr_xpos_lo[k], x);
  SetHiLo(&spr_ypos_hi[k], &spr_ypos_lo[k], y);
}

void SmwSavePlaythroughSnapshot() {
  char buf[128];
  snprintf(buf, sizeof(buf), "playthrough/%d_%d_%d.sav", ow_level_number_lo, misc_exit_level_action, (int)time(NULL));
  RtlSaveSnapshot(buf, false);
}

void UploadOAMBuffer() {  // 008449
  memcpy(g_ppu->oam, g_ram + 0x200, 0x220);
  RtlPpuWrite(OAMADDH, 0x80);
  RtlPpuWrite(OAMADDL, mirror_oamaddress_lo);
}


void SmwDrawPpuFrame(void) {
  SimpleHdma hdma_chans[3];

  Dma *dma = g_dma;

  dma_startDma(dma, mirror_hdmaenable, true);

  SimpleHdma_Init(&hdma_chans[0], &dma->channel[5]);
  SimpleHdma_Init(&hdma_chans[1], &dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[2], &dma->channel[7]);

  int trigger = g_recomp.vIrqEnabled ? g_recomp.vTimer + 1 : -1;

  for (int i = 0; i <= 224; i++) {
    ppu_runLine(g_ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0]);
    SimpleHdma_DoLine(&hdma_chans[1]);
    SimpleHdma_DoLine(&hdma_chans[2]);
    //    dma_doHdma(snes->dma);
    if (i == trigger) {
      // Simulate hardware IRQ latch: I_IRQ's first instruction reads HW_TIMEUP
      // ($4211) and branches on the N flag to distinguish timer-IRQ from
      // other sources. recomp_hw.c's ReadReg(0x4211) returns g_snes->inIrq<<7
      // and clears the flag; assert it here so the handler takes the
      // timer-IRQ path instead of exiting immediately.
      g_snes->inIrq = true;
      I_IRQ();
      trigger = g_recomp.vIrqEnabled ? g_recomp.vTimer + 1 : -1;
    }
  }
}

void SmwRunOneFrameOfGame(void) {
  if (*(uint16 *)reset_sprites_y_function_in_ram == 0)
    SmwVectorReset();
  SmwRunOneFrameOfGame_Internal();
  auto_00_816A();
#ifdef SMW_ORACLE
  oracle_dump_frame((uint32_t)snes_frame_counter, g_ram);
#endif
}


void LoadStripeImage_UploadToVRAM(const uint8 *pp) {  // 00871e
  while (1) {
    if ((*pp & 0x80) != 0)
      break;
    uint16 vram_addr = pp[0] << 8 | pp[1];

    uint8 vmain = __CFSHL__(pp[2], 1);
    uint8 fixed_addr = (uint8)(pp[2] & 0x40) >> 3;
    uint16 num = (swap16(WORD(pp[2])) & 0x3FFF) + 1;
    pp += 4;

    if (fixed_addr) {
      if (vram_addr != 0xffff) {
        uint16 *dst = g_ppu->vram + vram_addr;
        uint16 src_data = WORD(*pp);
        int ctr = (num + 1) >> 1;
        if (vmain) {
          for (int i = 0; i < ctr; i++) {
            dst[i * 32] = src_data;
            debug_server_on_vram_write((vram_addr + i * 32) & 0x7fff, src_data);
          }
        } else {
          // uhm...?
          uint8 *dst_b = (uint8 *)dst;
          for (int i = 0; i < num; i++)
            dst_b[i + ((i & 1) << 1)] = src_data;
          for (int i = 0; i < num; i += 2)
            dst_b[i + 1] = src_data >> 8;
          // Emit one hook per word touched (may span more than the direct
          // indexing suggests — be conservative and cover both halves).
          for (int i = 0; i < (num + 1) >> 1; i++)
            debug_server_on_vram_write((vram_addr + i) & 0x7fff, dst[i]);
        }
      }
      pp += 2;
    } else {
      if (vram_addr != 0xffff) {
        uint16 *dst = g_ppu->vram + vram_addr;
        uint16 *src = (uint16 *)pp;
        if (vmain) {
          for (int i = 0; i < (num >> 1); i++) {
            dst[i * 32] = src[i];
            debug_server_on_vram_write((vram_addr + i * 32) & 0x7fff, src[i]);
          }
        } else {
          for (int i = 0; i < (num >> 1); i++) {
            dst[i] = src[i];
            debug_server_on_vram_write((vram_addr + i) & 0x7fff, src[i]);
          }
        }
      }
      pp += num;
    }
  }
}

