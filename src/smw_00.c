#include "cpu_state.h"
#include "funcs.h"
#include "smw_rtl.h"
#include "variables.h"


void RunOneFrameOfGame_Internal() {
  assert(waiting_for_vblank != 0);
  ++counter_global_frames;
  /* Option-1 cpu->S ABI + main-loop stack-neutrality invariant.
   *
   * The hardware main loop reaches the GameMode processor via `JSR
   * ProcessGameMode` — a stack-NEUTRAL iteration (the JSR pushes 2, the
   * handler's terminal RTS pops 2; net 0 per frame). The push below models
   * that JSR so the handler's trampoline-RTS has a real 2-byte frame to pop
   * (without it the RTS over-pops live stack — the boot/title +6/frame leak).
   *
   * But on HEAVY frames (the title demo entering a level → GameMode14_InLevel
   * under GameMode07) some handler exit paths don't fully recover that
   * synthetic frame through the dispatch/miss-restore/NLR machinery, leaking
   * −3 B/frame; cpu->S drains into GameMode $0100 over ~86 frames and clobbers
   * it (garbled GAME-OVER/Nintendo-Presents screen, stuck GameMode 01). The
   * handler's actual work is correct (guest pushes/pulls balance); only the
   * synthetic-frame accounting drifts.
   *
   * Enforce the guest invariant explicitly at this dispatch boundary: a
   * ProcessGameMode iteration leaves cpu->S unchanged. Snapshot S before the
   * modelled JSR and restore it after the handler returns. Deterministic,
   * SMW-glue-only (no recompiler/MMX change). See ISSUES.md 2026-05-28. */
  uint16 _s_main_pre = g_cpu.S;
  cpu_push_jsr_return_frame(&g_cpu);
  InitAndMainLoop_ProcessGameMode(&g_cpu);
  g_cpu.S = _s_main_pre;
  waiting_for_vblank = 0;
}

void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
