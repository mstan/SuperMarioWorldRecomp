/* Widescreen policies for SMW routines that the LLE-first generator keeps in
 * the interpreter. These run at the same block-entry PCs used by the former
 * generated-C injector, but on the live execution path. */

#include <stdint.h>
#include <stdio.h>

#include "cpu_state.h"
#include "widescreen.h"

void SmwWidescreenInterpPreOpcode(CpuState *cpu, uint32_t pc24) {
  if (!g_ws_active || cpu_read8(cpu, 0x7E, 0x0100) != 0x14)
    return;

  /* ParseLevelSpriteList spawn sweep. Alternate between the widened leading
   * edge and intermediate columns so respawnable sprites do not become holes
   * when the camera reverses direction. This is the live-LLE equivalent of
   * WS-SPAWN in tools/apply_overrides.py. */
  if ((pc24 & 0x7FFFFFu) == 0x02A828u &&
      !(cpu_read8(cpu, 0x7E, 0x005B) & 1)) {
    unsigned direction = cpu_read8(cpu, 0x7E, 0x0055);
    if (direction == 0 || direction == 2) {
      unsigned run = (unsigned)cpu_read8(cpu, 0x7E, 0x0014) >> 1;
      int extra = g_ws_extra > 95 ? 95 : g_ws_extra;
      int offset;
      if (run & 1) {
        offset = extra;
      } else {
        int columns = extra / 16 + 1;
        offset = 16 * (int)((run >> 1) % (unsigned)columns);
      }
      int column = (cpu_read8(cpu, 0x7E, 0x001A) |
                    (cpu_read8(cpu, 0x7E, 0x001B) << 8)) +
                   (direction == 0 ? -(0x30 + offset) : 0x120 + offset);
      if (column >= 0) {
        cpu_write8(cpu, 0x7E, (uint16_t)(cpu->D + 0x0000),
                   (uint8_t)(column & 0xF0));
        cpu_write_a_m(cpu, (uint16_t)((column >> 8) & 0xFF));
#if SNESRECOMP_TRACE
        static int reported;
        if (!reported) {
          reported = 1;
          fprintf(stderr,
                  "[widescreen] live LLE spawn hook active at $02:A828\n");
        }
#endif
      }
    }
  }
}
