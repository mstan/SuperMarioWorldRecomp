#include "cpu_state.h"
#include "funcs.h"
#include "smw_rtl.h"
#include "variables.h"


void SmwRunOneFrameOfGame_Internal() {
  assert(waiting_for_vblank != 0);
  ++counter_global_frames;
  /* Option-1 cpu->S ABI: the hardware main loop reaches the GameMode
   * processor via JSR; its tail-dispatched handler RTS-pops that 2-byte
   * frame. This glue replaces the main loop with a bare host call, so push
   * the JSR frame here — otherwise the handler's trampoline-RTS over-pops
   * cpu->S by 2 every frame. See cpu_push_jsr_return_frame in cpu_state.h. */
  cpu_push_jsr_return_frame(&g_cpu);
  InitAndMainLoop_ProcessGameMode(&g_cpu);
  waiting_for_vblank = 0;
}

void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
