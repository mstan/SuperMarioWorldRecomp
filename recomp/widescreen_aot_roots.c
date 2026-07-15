#include "cpu_state.h"
#include "funcs.h"

/* Generator-only root manifest for the widescreen override injector.
 *
 * v2 generation is LLE-first, so functions used only by build-time injected
 * widescreen hooks would otherwise remain interpreter-only and leave no C
 * block for tools/apply_overrides.py to patch. This file is scanned by
 * tools/regen.sh but is not compiled into the game. Authentic behavior is
 * unchanged until the runtime-gated overrides are injected for a release. */
void WidescreenOverrideAotRoots(CpuState *cpu) {
  GenericExtendedSpriteGFXRt_FireballEntry(cpu);
  GetDrawInfo_Bank01_Recomp(cpu);
  GetDrawInfo_Bank23_Recomp(cpu);
  DrawWingTiles_ParaKoopaEntry(cpu);
  Spr05F_BrownChainedPlatform(cpu);
  sub_1C9EC(cpu);
  SubOffscreen_Bank01_Entry4(cpu);
  SubOffscreen_Bank01_Entry3(cpu);
  SubOffscreen_Bank01_Entry2(cpu);
  SubOffscreen_Bank01_01AC2D(cpu);
  SubOffscreen_Bank01_Entry1(cpu);
  SubOffscreen_Bank01_01AC33(cpu);
  SubOffscreen_Bank02_Entry4(cpu);
  SubOffscreen_Bank02_Entry3(cpu);
  SubOffscreen_Bank02_Entry2(cpu);
  SubOffscreen_Bank02_Entry1(cpu);
  SubOffscreen_Bank03_Entry4(cpu);
  SubOffscreen_Bank03_Entry3(cpu);
  SubOffscreen_Bank03_Entry2(cpu);
  SubOffscreen_Bank03_Entry1(cpu);
  SubOffscreen_Bank03_03B85F(cpu);
  ParseLevelSpriteList(cpu);
  ParseLevelSpriteList_Entry2(cpu);
}
