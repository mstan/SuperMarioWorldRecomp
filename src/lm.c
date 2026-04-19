/*
 * lm.c — Lunar Magic emulation hook stubs.
 *
 * SMW_DISABLE_LM=1 in our build (and kLmFeatures is zeros for stock
 * SMW), so HAS_LM_FEATURE() is 0 and g_lunar_magic is false. Every
 * LM hook would take the no-op branch in the original implementation,
 * so we just stub them out. These exist only as link symbols for
 * callers in smw_00.c, smw_cpu_infra.c, smw_rtl.c, and gen.
 *
 * The whole file disappears once those callers are themselves stripped.
 */
#include "consts.h"
#include "funcs.h"
#include "smw_rtl.h"
#include "variables.h"

void LmHook_InitExanimForLevel(void) { }
void LmHook_BeginLoadingLevelDataB(void) { }
void LmHook_PrepareLevel(void) { }
void LmFunc1_CustomPalettes(int a) { }
void LmHook_UploadGraphicsFiles(void) { }
uint8 LmFunc15_DecompressSlotB(uint8 a) { return 0; }
uint8 LmFunc15_DecompressSlot(uint8 a) { return 0; }
void LmHook_OwGraphicsDecompress() { }
void LmGraphicsDecompress(uint16 a) { }
const uint8 * LmHook_UploadGFXFile(uint8 a, uint8 index) { return 0; }
uint8 LmFunc18_GetFF200(uint8 a, uint8 k) { return 0; }
uint8 LmHook_ModifyMap16IDForSpecialBlocks(uint8 a) { return 0; }
uint8 LmFunc20_ModifyMap16IDForSpecialBlocks(uint8 a) { return 0; }
uint8 * LmHook_GraphicsDecompress(uint8 a) { return 0; }
void LmHook_LoadSpritesOnLevelLoad(void) { }
void Lm_ParseLevelSpriteList(uint8 k, const uint8 *p, PointU16 pt) { }
void LmHook_BE8A(uint16 x_base) { }
uint16 LmFunc_InitScreenTables(int k, int a) { return 0; }
void LmHook_LoadLevelInfo(void) { }
void LmHook_BufferCreditsBackgrounds(void) { }
void LmHook_LoadLevel(uint16 j) { }
void LmHook_LoadLevelB(const uint8 *ptr_layer1_data, uint8 R2, uint16 level_number) { }
void LmFunc_UploadOneLevelAnimation(ExAnimationInfo *anim) { }
void LmHook_UploadLevelAnimations(void) { }
bool LmHook_GameMode11_LoadSublevel(void) { return 0; }
void LmHook_PreserveLevelDataPointerInObjects(void) { }
void LmHook_RestoreLevelDataPointerInObjects(void) { }
uint8 LmHook_HandleHorizontalSubScreenCrossingForCurrentObject_Entry2(void) { return 0; }
uint8 LmHook_HandleVerticalSubScreenCrossingForCurrentObject(void) { return 0; }
void LmHook_UploadLevelLayer1And2Tilemaps(void) { }
void LmHook_InitializeLevelLayer1And2Tilemaps(void) { }
void Lm_SetupPipeTiles(uint16 a) { }
const uint16 * Lm_GetMap16RomAddr(uint16 addr) { return 0; }
void Lm_CopyTilesToL1UploadBuffer(const uint8 *plo, const uint8 *phi, uint16 j, uint16 r6) { }
void Lm_CopyToVramBufD_6(const uint8 *plo, const uint8 *phi, uint16 j, const uint16 *ptile) { }
void LmFunc_CopyTilemapA_12(const uint8 *plo, const uint8 *phi, uint16 j) { }
void LmFunc_108CB0(const uint8 *plo, const uint8 *phi, uint16 j, uint16 r6, uint16 r3) { }
void LmFunc_CopyTilemapC_12(const uint8 *plo, const uint8 *phi, uint16 j, uint16 r3) { }
void LmFunc_109662(const uint8 *plo, const uint8 *phi, uint16 j) { }
void LmFunc_109C29(const uint8 *plo, const uint8 *phi, uint16 j, uint16 r3) { }
void Lm_BufferTilemap_L1_0(void) { }
void Lm_BufferTilemap_L1_1(void) { }
uint16 * LmHook_CustomBgMap16(void) { return 0; }
void LmHook_BufferScrollingTiles_L2_Background(void) { }
void LmHook_BufferScrollingTiles_L2_1(void) { }
void LmHook_BufferScrollingTiles_L2_7(void) { }
void LmFunc_UpdateTilemapA_0(void) { }
void LmFunc_UpdateTilemapB_0(void) { }
void LmFunc_UpdateTilemapB_7(void) { }
void LmFunc_UpdateTilemapC_0(void) { }
void LmFunc_UpdateTilemapD_0(void) { }
void LmFunc_UpdateTilemapD_1(void) { }
void LmFunc_UpdateTilemapD_2(void) { }
void LmHook_BufferTilemap_L1(void) { }
void LmHook_BufferTilemap_L2(void) { }
void LmFunc_UpdateTilemapA() { }
void LmFunc_UpdateTilemapB() { }
void LmFunc_UpdateTilemapC() { }
void LmFunc_UpdateTilemapD() { }
void LmHook_CheckIfLevelTilemapsNeedScrollUpdate(void) { }
void Lm_SetupLmVramDma_0(void) { }
void Lm_SetupLmVramDma_6(void) { }
void Lm_SetupLmVramDma_12(void) { }
void Lm_SetupLmVramDma_16(void) { }
void LmHook_LevelTileAnimations(void) { }
void LmFunc_10C713(LongPtr p, uint16 r8, uint8 r10, uint16 k) { }
void LmHook_SetStandardPPUSettings(void) { }
void LmHook_HandleStandardLevelCameraScroll() { }
void LmHook_HandleStandardLevelCameraScrollB(void) { }
void LmHook_HandleStandardLevelCameraScrollC(void) { }
uint16 LmHook_HandleStandardLevelCameraScrollD(void) { return 0; }
uint16 LmHook_HandleStandardLevelCameraScrollG(uint16 r2, uint16 r4) { return 0; }
uint16 LmHook_LoadStripeImage(uint16 r3) { return 0; }
void LmHook_GameMode0C_LoadOverworld(uint16 k) { }
bool LmHook_WantEraseSprite(uint16 k, uint16 y) { return 0; }
uint16 LmHook_LoadLevelInfo_C(uint16 a) { return 0; }
uint16 LmHook_LoadLevelInfo_A(uint16 a, uint16 k) { return 0; }
void LmHook_ExpandLvlHdr(uint16 j) { }
void LmStdObj22_DirectMap16(uint8 k) { }
void LmStdObj23(uint8 k) { }
void LmHandleDirectMapObjsInner(uint8 r0, uint8 r1, uint8 r2, uint8 r3) { }
void LmHandleDirectMapObjs(uint8 a) { }
void LmStdObj24(uint8 k) { }
void LmStdObj25(uint8 k) { }
void LmStdObj26_MusicBypass(uint8 k) { }
void LmStdObj27(uint8 k) { }
void LmFunc_DF1C3(uint8 k, uint8 a) { }
void LmStdObj28(uint8 k) { }
void LmStdObj29(uint8 k) { }
bool LmHook_CustomTitleScreenDemo(void) { return 0; }
void LmHook_InitializeSaveData(void) { }
void LmFunc13_SwitchBlock(uint8 j) { }
void LmHook_DisplayMessage(void) { }
void LmHook_OverworldPalette(void) { }
void LmHook_LevelNamesPatch(uint16 a) { }
int LmHook_LoadLevelInfo_E(uint16 k, uint16 lvl, uint8 r0, uint8 r1) { return 0; }
uint16 LmHook_DestroyTileAnimation2(uint16 a) { return 0; }
const uint8* LmHook_DestroyTileAnimation3(uint16 k) { return 0; }
uint16 LmHook_RemapDestroyTile(uint16 a) { return 0; }
void LmHook_EventStuff(uint8 a, bool from_where) { }
uint8 LmFunc_ReadLevelInfoByte(const uint8 *p) { return 0; }
void LmFunc_UploadGraphics_StepA(void) { }
uint16 LmFunc_10D796(uint8 r11, uint16 *r7, uint16 *r9) { return 0; }
void LmFunc_10D7BC(uint16 k, uint16 *r1, uint16 *r4, uint16 r7, uint16 r9) { }
void LmFunc_10D7CF(uint16 k, uint16 *r1, uint16 *r4, uint16 r7, uint16 r9) { }
void LmFunc_10D7FA(uint16 k, uint16 r1_value, uint16 *r1, uint16 *r4, uint16 r7, uint16 r9) { }
void LmHook_InitializeLevelLayer3_GenerateInteractiveTideWater(void) { }
void LmFunc_UpdateBG12NBA(void) { }
