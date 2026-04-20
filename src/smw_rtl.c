#include "smw_rtl.h"
#include "recomp_state.h"
#include "variables.h"
#include "config.h"
#include <time.h>
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

void SmwSavePlaythroughSnapshot() {
  char buf[128];
  snprintf(buf, sizeof(buf), "playthrough/%d_%d_%d.sav", ow_level_number_lo, misc_exit_level_action, (int)time(NULL));
  RtlSaveSnapshot(buf, false);
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
    I_RESET();
  SmwRunOneFrameOfGame_Internal();
  auto_00_816A();
}

