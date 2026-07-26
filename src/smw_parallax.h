#ifndef SMW_PARALLAX_H
#define SMW_PARALLAX_H

#include <stdbool.h>

/* Super Mario World's binding for the shared layered-parallax presenter
 * (snesrecomp/runner/src/parallax.h, docs/PARALLAX.md). Holds the only
 * SMW-specific knowledge the feature needs: per-layer depth, and when a frame
 * is in-level gameplay. */

/* Install the SMW layer profile and seed the master switch from g_config.
 * Call once, after the config files are parsed. */
void SmwParallax_Init(void);

/* Per-frame gate + capture policy. Call from RtlDrawPpuFrame, after the
 * widescreen policy has been applied and BEFORE draw_ppu_frame(). */
void SmwParallax_PrepareFrame(int frame_width, int frame_height, int extra);

/* Hotkey toggle (persists to config.ini). */
void SmwParallax_Toggle(void);

#endif  /* SMW_PARALLAX_H */
