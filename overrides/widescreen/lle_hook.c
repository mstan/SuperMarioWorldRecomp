/* Widescreen policies for SMW routines that the LLE-first generator keeps in
 * the interpreter. These run at the same block-entry PCs used by the former
 * generated-C injector, but on the live execution path. */

#include <stdint.h>
#include <stdio.h>

#include "cpu_state.h"
#include "widescreen.h"

#ifdef SMW_COOP_BUILD
static unsigned s_coop_tile_level_token = ~0u;
static int s_coop_tile_primed_cols;
static int s_coop_tile_last_host_frame = -1;
static uint16_t s_coop_row_camera_x;
static int s_coop_row_camera_shifted;

extern int snes_frame_counter;

static int SmwCoopWideTileColumns(void) {
  int extra = g_ws_extra > 95 ? 95 : g_ws_extra;
  return (extra + 15) / 16;
}

/* Prime the extra forward columns once per sublevel (and whenever Adaptive
 * grows). The co-op replacement normally queues only one Layer 1 strip when
 * cameraX crosses a 16-pixel boundary. Forcing its current-direction cursor
 * stale for a few stationary opening frames lets the genuine builder fill
 * offsets 18..(17+wideColumns) contiguously before normal shifted streaming
 * takes over. */
void SmwCoopWidescreenTileBegin(CpuState *cpu) {
  if (!g_ws_active || cpu_read8(cpu, 0x7E, 0x0100) != 0x14 ||
      (cpu_read8(cpu, 0x7E, 0x005B) & 2))
    return;
  unsigned token = cpu_read8(cpu, 0x7E, 0x13BF) |
                   ((unsigned)cpu_read8(cpu, 0x7E, 0x141A) << 8);
  int host_frame = snes_frame_counter;
  /* A death/reload can re-enter the same sublevel with the same $13BF/$141A
   * identity but freshly cleared VRAM. The replacement streamer is absent
   * during the intervening game modes, so a gap between calls is the reliable
   * lifecycle edge. Re-prime after that gap as well as on a different level. */
  if (token != s_coop_tile_level_token ||
      s_coop_tile_last_host_frame < 0 ||
      host_frame < s_coop_tile_last_host_frame ||
      host_frame - s_coop_tile_last_host_frame > 8) {
    s_coop_tile_level_token = token;
    s_coop_tile_primed_cols = 0;
  }
  s_coop_tile_last_host_frame = host_frame;

  /* The co-op replacement's vertical row builder refreshes only 256 pixels:
   * 16 Map16 columns beginning at cameraX/16-2. A centered 16:9 view needs
   * columns on both sides, so run the genuine row upload every frame and
   * alternate left/right 256-pixel segments. The builder helpers below shift
   * cameraX only for the duration of that row build, making both its Map16
   * source and cyclic VRAM destination agree without touching gameplay. */
  unsigned row_direction = cpu_read8(cpu, 0x7F, 0x831C) & 2u;
  cpu_write16(cpu, 0x7F, (uint16_t)(0x830F + row_direction), 0xFFFF);

  int columns = SmwCoopWideTileColumns();
  if (s_coop_tile_primed_cols < columns) {
    unsigned direction = cpu_read8(cpu, 0x7F, 0x831B) & 2u;
    cpu_write16(cpu, 0x7F, (uint16_t)(0x830B + direction), 0xFFFF);
  }
}

void SmwCoopWidescreenRowBegin(CpuState *cpu) {
  if (s_coop_row_camera_shifted || !g_ws_active ||
      cpu_read8(cpu, 0x7E, 0x0100) != 0x14 ||
      (cpu_read8(cpu, 0x7E, 0x005B) & 2))
    return;

  int camera_x = cpu_read8(cpu, 0x7E, 0x001A) |
                 (cpu_read8(cpu, 0x7E, 0x001B) << 8);
  int last_camera_x = cpu_read8(cpu, 0x7E, 0x005E) << 8;
  int shifted_x;
  if (snes_frame_counter & 1) {
    /* Original start is cameraCol-2; +192px makes the second segment begin
     * at cameraCol+10, directly after the left segment's last column. */
    shifted_x = camera_x + 192;
    if (shifted_x > last_camera_x)
      shifted_x = last_camera_x;
  } else {
    /* Move the original start four Map16 columns left. */
    shifted_x = camera_x > 64 ? camera_x - 64 : 0;
  }

  s_coop_row_camera_x = (uint16_t)camera_x;
  s_coop_row_camera_shifted = 1;
  cpu_write16(cpu, 0x7E, 0x001A, (uint16_t)shifted_x);
}

void SmwCoopWidescreenRowEnd(CpuState *cpu) {
  if (!s_coop_row_camera_shifted)
    return;
  cpu_write16(cpu, 0x7E, 0x001A, s_coop_row_camera_x);
  s_coop_row_camera_shifted = 0;
}

int SmwCoopWidescreenTileOffset(CpuState *cpu) {
  int columns = SmwCoopWideTileColumns();
  int offset = columns;
  if (s_coop_tile_primed_cols < columns)
    offset = ++s_coop_tile_primed_cols;
  return (cpu->X & 0xFFFFu) == 0u ? -offset :
         (cpu->X & 0xFFFFu) == 2u ? offset : 0;
}
#endif

void SmwWidescreenInterpPreOpcode(CpuState *cpu, uint32_t pc24) {
  if (!g_ws_active || cpu_read8(cpu, 0x7E, 0x0100) != 0x14)
    return;

#ifdef SMW_COOP_BUILD
  /* The co-op IPS replaces SMW's Layer 1 streamer with a four-camera-aware
   * routine. Immediately before its direction-table ADC, A is cameraX / 16
   * and X is 0 (left) or 2 (right). Move that column frontier outward by the
   * live widescreen margin. The AOT equivalent is WS-COOP-TILE in the
   * generated-C injector. */
  if ((pc24 & 0x7FFFFFu) == 0x1FB206u) {
    SmwCoopWidescreenTileBegin(cpu);
  } else if ((pc24 & 0x7FFFFFu) == 0x1FAA7Au) {
    cpu_write_a_m(cpu, (uint16_t)(cpu_read_a16(cpu) +
                                  SmwCoopWidescreenTileOffset(cpu)));
  } else if ((pc24 & 0x7FFFFFu) == 0x1FAB83u ||
             (pc24 & 0x7FFFFFu) == 0x1FAC27u) {
    SmwCoopWidescreenRowBegin(cpu);
  } else if ((pc24 & 0x7FFFFFu) == 0x1FAC23u ||
             (pc24 & 0x7FFFFFu) == 0x1FAC90u) {
    SmwCoopWidescreenRowEnd(cpu);
  }
#endif

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

  /* Brown chained platforms use a two-slot reserved range in stock SMW. The
   * wider despawn window can keep three neighboring platforms alive at once,
   * so include slot 5 when the allocation join executes through LLE. This is
   * the live counterpart of WS-SLOT in tools/apply_overrides.py. */
  if ((pc24 & 0x7FFFFFu) == 0x02A916u &&
      cpu_read8(cpu, 0x7E, (uint16_t)(cpu->D + 0x0005)) == 0x5F &&
      cpu_read8(cpu, 0x7E, (uint16_t)(cpu->D + 0x0006)) == 0x05) {
    cpu_write8(cpu, 0x7E, (uint16_t)(cpu->D + 0x0006), 0x04);
  }
}
