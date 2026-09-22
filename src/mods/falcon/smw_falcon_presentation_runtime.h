#pragma once

#include <stddef.h>
#include <stdint.h>

#include "falcon_presentation.h"
#include "snes/ppu.h"

/* Called only after mod_runtime has committed the exact owner-ROM resource. */
int smw_falcon_presentation_activate(const char *owner_rom_path);
void smw_falcon_presentation_reset(void);
/* Call after the host audio mutex exists and before the first game frame. */
void smw_falcon_presentation_audio_ready(void);

/* Configure the narrow player OBJ suppression before PPU draw, then composite
 * the approved owner cache into the PPU-owned frame before presentation. */
void smw_falcon_presentation_prepare_ppu(Ppu *ppu);
/* Run after every guest OAM DMA and immediately before PPU scanline zero.
 * This is deliberately separate from prepare_ppu: the latter configures OBJ
 * capture, whereas this changes only the finalized, transient PPU OAM pair
 * used for a carried native shell. */
void smw_falcon_presentation_finalize_ppu_oam(Ppu *ppu);
void smw_falcon_presentation_present(uint8_t *pixels, size_t pitch,
                                     int width, int height);
int smw_falcon_presentation_is_active(void);
/* Samples owner-cache TransN motion for the controller.  The cache remains
 * external; 0 means it is unavailable or does not contain this animation. */
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z);
/* Convert SMW PlayerGFXRt's screen origin ($80) to its 32px foot contact. */
float smw_falcon_presentation_foot_anchor_y(int player_screen_y);

/* Reanchor exactly one completed native OAM group.  The caller owns the
 * selection and count; this helper changes only the X/Y bytes, never guest
 * sprite state or adjacent OAM entries.  It is public for the host seam test. */
void smw_falcon_presentation_reanchor_oam_group(uint8_t *entries,
                                                unsigned count,
                                                int anchor_x, int anchor_y);

/* Reanchor completed OAM entries already copied into the PPU.  This is the
 * presentation seam used immediately before rasterization: changing WRAM OAM
 * here would miss the current frame's completed NMI/DMA transfer.  Only X/Y
 * position fields of [first, first + count) are changed. */
void smw_falcon_presentation_reanchor_ppu_oam_group(Ppu *ppu,
                                                     unsigned first,
                                                     unsigned count,
                                                     int anchor_x,
                                                     int anchor_y);
/* SMW normal-sprite draw routines address OAM relative to WRAM $0300.  PPU
 * OAM is absolute, so its first normal-sprite entry is slot 64. */
unsigned smw_falcon_presentation_normal_sprite_ppu_slot(uint8_t oam_offset);
int smw_falcon_presentation_stunned_shell_ppu_slot(uint8_t restored_offset,
                                                    unsigned *out_slot);

/* Exposed for focused host-boundary tests. */
FalconPresentationPose smw_falcon_presentation_pose_for_state(
    int state, unsigned state_frame, float facing);
