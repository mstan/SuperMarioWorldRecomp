#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "snes/ppu.h"
#include "desktop/display_aspect.h"

/* The guest always scans out 256x224. Host geometry has no OAM width limit. */
enum { SMW_RENDER_MAX_WIDTH = 16384 };
typedef struct SmwViewport { int width, extra; double aspect; } SmwViewport;
typedef struct SmwVideoSettings {
  bool enabled, adaptive_spawns;
  /* 0 = Fit to screen; otherwise the desired display aspect. */
  double aspect;
} SmwVideoSettings;
extern SmwVideoSettings g_smw_video;
extern SmwViewport g_smw_viewport;
SmwViewport SmwCalculateViewport(const SmwVideoSettings *settings, int w, int h,
                                 SnesDisplayAspect display_aspect);
int SmwViewOffset(SmwViewport view, int camera, int level_width);
void SmwDestination(SmwViewport view, int w, int h, int *x, int *y, int *dw, int *dh);
void SmwRendererBeginFrame(void);
void SmwRendererCaptureLine(const Ppu *ppu, int line);
void SmwRendererDraw(uint8_t *pixels, size_t pitch, const uint8_t *stock);
int SmwRendererNativeOffset(void);
bool SmwRendererMapTile(const uint8_t *ram, unsigned layer, int x, int y, uint16_t *tile);
/* Signed host coordinates paired with the exact OAM image, latched at NMI. */
void SmwRendererRecordOam(unsigned slot, int x, uint16_t position, uint16_t attr);
void SmwRendererRecordSprite(unsigned slot, int x, int y, unsigned first, unsigned end);
void SmwRendererRecordSpriteTile(unsigned piece, int x, int y);
/* Commit each object's completed draw before another object reuses guest OAM.
 * kind 0 = normal sprite, 1 = extended sprite. Host storage grows as needed. */
void SmwRendererBeginActor(unsigned kind, unsigned index);
void SmwRendererEndActor(void);
/* Latch scene RAM and completed OAM ownership together, immediately before NMI. */
void SmwRendererLatchFrame(void);
void SmwRendererResetScene(void);
void SmwRendererSpawnFrame(void);
struct SaveLoadInfo;
void SmwRendererSaveExtra(struct SaveLoadInfo *sli);
void SmwRendererLoadExtra(struct SaveLoadInfo *sli, uint32_t version);
void SmwRendererStateLoaded(uint32_t version);
void SmwRendererDiagnostics(const uint8_t *stock, const uint8_t *image, size_t pitch);
