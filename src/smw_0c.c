#include "consts.h"
#include "funcs.h"
#include "smw_rtl.h"
#include "variables.h"

void (*kUnk_ca1de[5])(void) = {
    &sub_CA1ED,
    &YoshisWatchInExcitementDuringEnding,
    &HatchYoshiEggsDuringEnding,
    &SlideInThankYouDuringEnding,
    &EveryoneCheeringDuringEnding,
};
void (*kGameMode19_Cutscene_Ptrs0CC9A5[7])(void) = {
    &GameMode19_Cutscene_IggyCutscene,   &GameMode19_Cutscene_MortonCutscene, &GameMode19_Cutscene_LemmyCutscene,
    &GameMode19_Cutscene_LudwigCutscene, &GameMode19_Cutscene_RoyCutscene,    &GameMode19_Cutscene_WendyCutscene,
    &GameMode19_Cutscene_LarryCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CC9C0[6])(void) = {
    &HandleTNTFuse,
    &InitializeTNTExplosion,
    &HandleTNTExplosion,
    &InitializeCastleCrumblingDown,
    &HandleCastleCrumblingDown,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CC9D6[5])(void) = {
    &PlayerDropkicksAndStompsCastle,
    &InitializeCastleCrumblingDown,
    &HandleCastleCrumblingDown,
    &DrawPlayerCough_MortonCutscene,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CC9F0[10])(void) = {
    &HandleTNTFuse,
    &InitializeDudTNTExplosion,
    &HandleDudTNTExplosion,
    &DelayTNTExplosionUntilPlayerComesBy,
    &InitializeTNTExplosion,
    &HandleTNTExplosion,
    &InitializeCastleCrumblingDown,
    &HandleCastleCrumblingDown,
    &WaitForCastleDestructionTextToFinishInRoyCutscene,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CCA1F[8])(void) = {
    &HandleTNTFuse,
    &InitializeTNTExplosion,
    &HandleTNTExplosion,
    &InitializeCastleLiftoff,
    &HandleCastleLiftoff,
    &InitializeFarawayCastleRocket,
    &HandleFarawayCastleRocket,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CCA49[4])(void) = {
    &WaitBeforeMakingHammeredCastleCrumble,
    &InitializeCastleCrumblingDown,
    &HandleCastleCrumblingDown,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CCA6E[2])(void) = {
    &WaitForWendysCastleToBeFullyMopped,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};
void (*kGameMode19_Cutscene_Ptrs0CCA79[5])(void) = {
    &UprootCastleFromGround,
    &KickCastleAway,
    &KickedCastleCreatesQuake,
    &WaitForPlayerVictoryPoseAfterCastleQuake,
    &CheckIfPlayerCanEndCastleDestructionCutscene,
};

const uint8 kDrawEndingThankYou_Tiles[16] = { 0x26, 0x28, 0x2a, 0x2c, 0x46, 0x48, 0x4a, 0x4c, 0x60, 0x62, 0x64, 0x66, 0x6a, 0x6c, 0x6e, 0xa,  };
const uint16 kDrawCreditsPeachRedAndYellowYoshi_DATA_0CA7B9[26] = { 0x6463, 0x6968, 0x6463, 0x6968, 0x4c4b, 0x6c6b, 0x8b8a, 0x68aa, 0x8e8d, 0xaead, 0x8a, 0x44aa, 0xe8a, 0x2eaa, 0x8081, 0xa0a1, 0x8384, 0xa3a4, 0x8687, 0xa6a7, 0x8180, 0xa1a0, 0x8483, 0xa4a3, 0x8786, 0xa7a6,  };
const uint16 kDrawCreditsPeachRedAndYellowYoshi_DATA_0CA7ED[26] = { 0x2121, 0x2121, 0x2121, 0x2121, 0x2121, 0x2121, 0x2121, 0x2121, 0x2020, 0x2020, 0x2121, 0x2121, 0x2121, 0x2121, 0x7878, 0x7878, 0x7878, 0x7878, 0x7878, 0x7878, 0x3434, 0x3434, 0x3434, 0x3434, 0x3434, 0x3434,  };
const uint16 kDrawLeaningEndingYoshis_DATA_0CA93A[12] = { 0xb9bb, 0xdbdc, 0xd9da, 0x898b, 0xabac, 0xa9aa, 0xbbb9, 0xdad9, 0xdcdb, 0x8b89, 0xaaa9, 0xacab,  };
const uint16 kDrawLeaningEndingYoshis_DATA_0CA952[12] = { 0x7878, 0x7878, 0x7878, 0x7878, 0x7878, 0x7878, 0x3434, 0x3434, 0x3434, 0x3434, 0x3434, 0x3434,  };
const uint16 kDrawLeaningEndingYoshis_DATA_0CA96A[12] = { 0x1000, 0x800, 0x1810, 0x1000, 0x800, 0x1810, 0x1000, 0x800, 0x1810, 0x1000, 0x800, 0x1810,  };
const uint16 kDrawLeaningEndingYoshis_DATA_0CA982[12] = { 0x0, 0x1010, 0x1010, 0x0, 0x1010, 0x1010, 0x0, 0x1010, 0x1010, 0x0, 0x1010, 0x1010,  };
const uint8 kDrawLeaningEndingYoshis_DATA_0CA99A[4] = { 0x0, 0x6, 0xc, 0x12,  };
// todo: oob

