#include "smw_parallax.h"

#include <stdio.h>

#include "config.h"
#include "parallax.h"
#include "snes/ppu.h"

extern Ppu *g_ppu;
extern uint8_t g_ram[0x20000];
extern bool g_new_ppu;
extern void WriteConfigFile(const char *filename);
extern const char *SmwParallax_ConfigPath(void);

/* ── Layer stack ──────────────────────────────────────────────────────────
 *
 * Array order IS the painter's draw order and mirrors the Mode-1 priority
 * ranks the PPU composites with (snes/ppu.c PpuDrawBackgrounds' table), so
 * occlusion matches hardware. Depth (`z`) carries only parallax and is shared
 * between a layer and its priority band (and across all sprite bands) so a
 * layer never parallax-splits against itself.
 *
 * SMW layer semantics:
 *   BG1  the level playfield — ground, pipes, blocks. The focal plane.
 *   BG2  the scenery layer behind it (hills, clouds, cave walls), which the
 *        game already scrolls at a fraction of BG1. Pushing it back turns
 *        that ratio into real depth.
 *   BG3  the status bar on scanlines < 40, PLUS in-level content further down
 *        (water surface/body tiles) — see the limitation below.
 *   OBJ  Mario, enemies, items. Sprite screen positions already embed the
 *        camera, so they sit at the playfield depth and stay welded to it.
 *
 * KNOWN LIMITATION: BG3 is one layer carrying two unrelated things. Placing it
 * forward keeps the status bar in front of the level (where it belongs and
 * stays readable), but any BG3 *level* content — most visibly the water
 * surface in water levels — rides forward with it and will read as floating in
 * front of the playfield. Splitting them needs a scanline-banded capture (the
 * status bar is rows < 40, exactly the band the widescreen HUD split already
 * uses), which the overlay API cannot express per-band today: capture
 * rectangles are per-source, so BG3 would need two. ar-recomp hit the same
 * conflict on ActRaiser and solved it with a separate anchored flat-HUD pass.
 */
static const ParallaxPlaneDesc kSmwPlanes[] = {
  /* plane                    group                   z      shade r,g,b   shadow */
  { kParallaxPlane_Backdrop,  kParallaxGroup_Backdrop, 0.00f, 0.70f, 0.70f, 0.80f, false },
  { kParallaxPlane_Obj,       kParallaxGroup_Obj,      0.50f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Obj1,      kParallaxGroup_Obj,      0.50f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Bg2,       kParallaxGroup_Bg2,      0.18f, 0.82f, 0.82f, 0.88f, false },
  { kParallaxPlane_Bg1,       kParallaxGroup_Bg1,      0.50f, 0.94f, 0.94f, 0.97f, true  },
  { kParallaxPlane_Obj2,      kParallaxGroup_Obj,      0.51f, 1.00f, 1.00f, 1.00f, true  },
  { kParallaxPlane_Bg2Hi,     kParallaxGroup_Bg2,      0.19f, 0.82f, 0.82f, 0.88f, false },
  { kParallaxPlane_Bg1Hi,     kParallaxGroup_Bg1,      0.51f, 0.94f, 0.94f, 0.97f, true  },
  { kParallaxPlane_Obj3,      kParallaxGroup_Obj,      0.52f, 1.00f, 1.00f, 1.00f, true  },
  /* The status bar: forward, unshaded, and casting no shadow — a HUD that
   * dropped a shadow onto the playfield would read as an object in the world. */
  { kParallaxPlane_Bg3,       kParallaxGroup_Bg3,      0.92f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg3Hi,     kParallaxGroup_Bg3,      0.93f, 1.00f, 1.00f, 1.00f, false },
};

static const ParallaxProfile kSmwProfile = {
  .name = "supermarioworld",
  .planes = kSmwPlanes,
  .plane_count = (int)(sizeof(kSmwPlanes) / sizeof(kSmwPlanes[0])),
  .capture_mask = (1u << kPpuOverlaySource_Bg1) |
                  (1u << kPpuOverlaySource_Bg2) |
                  (1u << kPpuOverlaySource_Bg3) |
                  (1u << kPpuOverlaySource_Obj),
};

void SmwParallax_Init(void) {
  Parallax_SetProfile(&kSmwProfile);
  g_parallax.enabled = g_config.parallax;
  if (g_parallax.enabled) {
    char state[256];
    Parallax_DescribeState(state, sizeof state);
    fprintf(stderr, "[parallax] %s\n", state);
  }
}

void SmwParallax_Toggle(void) {
  g_config.parallax = !g_config.parallax;
  g_parallax.enabled = g_config.parallax;
  printf("Parallax = %s\n", g_config.parallax ? "on" : "off");
  WriteConfigFile(SmwParallax_ConfigPath());
}

/* misc_game_mode 0x14 = the level main routine — the same in-level gate the
 * widescreen side-margin policy uses (RtlDrawPpuFrame). Overworld, title,
 * menus and transitions stay authentic: they are not worlds with parallax
 * scenery, and their BG2 is bounded, so tilting them just looks broken. */
static bool SmwParallaxSceneIsLevel(void) {
  return g_ram[0x100] == 0x14;
}

/* Camera motion for the presenter's lean (Parallax_ReportCameraMotion). BG1 is
 * the level playfield, so BG1's scroll registers ARE the gameplay camera.
 *
 * Scroll registers are 10-bit and wrap, so a raw subtraction reads a wrap as a
 * ~1024px lurch; wrap to the shortest signed distance instead. A delta larger
 * than a plausible frame of travel is a discontinuity (level load, pipe warp,
 * door) and is reported as zero rather than as a huge sweep. */
static void SmwParallaxReportMotion(void) {
  static bool have_prev;
  static int prev_x, prev_y;
  if (!g_ppu) return;
  int x = g_ppu->hScroll[0] & 0x3ff;
  int y = g_ppu->vScroll[0] & 0x3ff;
  if (!have_prev) {
    have_prev = true;
    prev_x = x;
    prev_y = y;
    Parallax_ReportCameraMotion(0.0f, 0.0f);
    return;
  }
  int dx = ((x - prev_x + 512) & 0x3ff) - 512;
  int dy = ((y - prev_y + 512) & 0x3ff) - 512;
  prev_x = x;
  prev_y = y;
  const int kMaxFrameTravel = 24;   /* px; a running Mario is well inside this */
  if (dx > kMaxFrameTravel || dx < -kMaxFrameTravel) dx = 0;
  if (dy > kMaxFrameTravel || dy < -kMaxFrameTravel) dy = 0;
  Parallax_ReportCameraMotion((float)dx, (float)dy);
}

void SmwParallax_PrepareFrame(int frame_width, int frame_height, int extra) {
  SmwParallaxReportMotion();
  /* Parallax REQUIRES the priority-buffer PPU: host-overlay layer extraction
   * (and hence every captured plane) only exists on that path. SMW defaults to
   * it, but the Alt+R renderer toggle can turn it off, which would otherwise
   * leave the feature silently capturing nothing. */
  if (Parallax_Enabled())
    g_new_ppu = true;
  /* Mode 1 is the only BG mode with overlay capture wired up; SMW's levels run
   * in it, but Mode 7 appears (e.g. the ending/bonus effects), so gate here
   * rather than declaring captures that would export nothing. */
  bool mode1 = g_ppu && PPU_mode(g_ppu) == 1;
  Parallax_PrepareFrame(g_ppu, frame_width, frame_height, extra,
                        mode1 && SmwParallaxSceneIsLevel());
}
