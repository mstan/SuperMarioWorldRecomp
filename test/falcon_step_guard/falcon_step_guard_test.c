/* Exact mock of the native step-crush seam recorded from save slot 0.
 * This test reaches the adapter hook directly so it stays independent of the
 * broader controller-regression harness. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "cpu_state.h"
#include "../../src/variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;

void cpu_write8(CpuState *cpu, uint8 bank, uint16 addr, uint8 value)
{
    if (cpu != NULL && cpu->ram != NULL && (bank == 0 || bank == 1))
        cpu->ram[addr] = value;
}

void SprStatus02_Dead_SetNorSprStatus04(CpuState *cpu) { (void)cpu; }
void SpawnSpinJumpStars(CpuState *cpu) { (void)cpu; }
void CheckPlayerToNormalSpriteColl_01AB46(CpuState *cpu) { (void)cpu; }
void SpawnBounceSprite(CpuState *cpu) { (void)cpu; }
void KillNormalSprite_AcceptedConsequence(CpuState *cpu) { (void)cpu; }
/* Block destruction is outside this collision observer fixture. Fail rather
 * than silently accepting an unexpected call into native tile handling. */
void GetPlayerLevelCollisionMap16ID_Entry2(CpuState *cpu) { (void)cpu; abort(); }
void SpawnBrickPieces(CpuState *cpu) { (void)cpu; abort(); }
void GenerateTile(CpuState *cpu) { (void)cpu; abort(); }
void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    (void)events;
}
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z)
{
    (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    /* A Kick wall now stops before any Bound root can be sampled. Keep this
     * callback inert so the fixture proves no hidden force-air/root route. */
    if (animation != NULL && strcmp(animation, "FalconDiveEnd1") == 0) {
        return 0;
    }
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static void ground_adapter_frame(uint8_t hold1, uint8_t press1)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    /* This fixture models the native floor result between DC2D and CD36. */
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    SmwFalconAfterPhysics(NULL);
}

int main(void)
{
    CpuState cpu;
    ForeignState *state;

    memset(g_ram, 0, sizeof(g_ram));
    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Falcon");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_current_power_up = 0;
    player_in_air_flag = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    player_sub_xpos = 0x33;
    player_sub_ypos = 0x55;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);

    /* UpdatePlayerSpritePosition's pre-hook snapshots the immediately prior
     * valid coordinate. The f310/f324 live trace supplied the values and
     * $77=$1D signature, not an artificial 59px single-frame movement. */
    io_controller_hold1 = 0x01; /* continue holding right into the step */
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    if (state == NULL) return fail("foreign state available");
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = 1.0f;
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
        player_in_air_flag != 0 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("step guard keeps native wall and floor contact ($1D -> $05)");

    /* The f324 correction is not enough by itself: held-right must remain a
     * narrow wall stop over following frames, rather than re-integrating
     * Falcon downward through the step's floor.  Deliberately perturb every
     * native coordinate the latch owns to prove it restores the exact saved
     * position/subposition/ground state three times. */
    for (unsigned i = 0; i != 3; ++i) {
        ++snes_frame_counter;
        player_xpos = 0x074A;
        player_ypos = (uint16_t)(0x016F + i);
        player_sub_xpos = (uint8_t)(0xA1 + i);
        player_sub_ypos = (uint8_t)(0xB1 + i);
        player_in_air_flag = 0;
        player_xspeed = 0x65;
        player_yspeed = 0x96;
        player_blocked_flags = 0;
        state->grounded = 1;
        SmwFalconBeforePhysics(NULL);
        if (player_xpos != 0x070F || player_ypos != 0x0160 ||
            player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
            player_in_air_flag != 0 || player_xspeed != 0 ||
            player_yspeed != 0 || player_blocked_flags != 0x05)
            return fail("held-right step latch keeps Falcon standing at the wall");
    }

    /* Neutral releases the latch; it must not turn later ordinary motion into
     * blanket position or crush immunity. */
    io_controller_hold1 = 0;
    ++snes_frame_counter;
    player_xpos = 0x0711;
    player_ypos = 0x0160;
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    state->grounded = 1;
    SmwFalconBeforePhysics(NULL);
    if (player_xpos != 0x0711)
        return fail("neutral releases the persistent step-wall latch");

    /* The wall/OOB rollback must not become blanket enemy invulnerability.
     * A normal grounded snapshot with no side-wall hazard leaves native
     * damage/death state $09 untouched at the later guaranteed seam. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for ordinary enemy damage");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0800;
    player_ypos = 0x0160;
    player_sub_xpos = 0x22;
    player_sub_ypos = 0x44;
    player_blocked_flags = 0x04;
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    player_current_state = 9;
    player_xpos = 0x0800;
    player_ypos = 0x0160;
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    SmwFalconBeforeNormalSprites(NULL);
    if (player_current_state != 9)
        return fail("ordinary enemy damage is not cancelled by wall safety");

    /* Mirror the exact native false-crush signature at a left wall.  The
     * side-neutral ROM test is ($77 & $1C)==$1C, so left is $1E and must
     * retain left+floor as $06. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for left step guard");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0900;
    player_ypos = 0x0160;
    player_sub_xpos = 0x44;
    player_sub_ypos = 0x66;
    io_controller_hold1 = 0x02;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    if (state == NULL) return fail("foreign state available for left wall");
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = -1.0f;
    player_xpos = 0x08C0;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x90;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1E;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x0900 || player_ypos != 0x0160 ||
        player_sub_xpos != 0x44 || player_sub_ypos != 0x66 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x06)
        return fail("left step guard mirrors $1E to stable left+floor $06");

    /* A taller wall can report ordinary side contact without reaching the
     * $00:E9FB false-crush branch.  Slot 0 reproduced this by double-tapping
     * left into a larger wall: the wall result must still become the same
     * held neutral-input latch, not a one-frame speed zero that can re-enter
     * geometry on the next frame. */
    SmwFalconOnStateLoaded();
    player_xpos = 0x0900;
    player_ypos = 0x0160;
    player_sub_xpos = 0x44;
    player_sub_ypos = 0x66;
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    io_controller_hold1 = 0x02;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = -1.0f;
    player_xpos = 0x08C0;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x90;
    player_yspeed = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 0x06;
    SmwFalconAfterPhysics(NULL);
    if (player_xpos != 0x0900 || player_ypos != 0x0160 ||
        player_sub_xpos != 0x44 || player_sub_ypos != 0x66 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x06)
        return fail("ordinary left wall contact installs stable held-wall latch");
    for (unsigned i = 0; i != 2; ++i) {
        ++snes_frame_counter;
        player_xpos = 0x08BF;
        player_ypos = 0x0170;
        player_xspeed = 0x90;
        player_yspeed = 0x90;
        player_blocked_flags = 0;
        state->grounded = 1;
        SmwFalconBeforePhysics(NULL);
        if (player_xpos != 0x0900 || player_ypos != 0x0160 ||
            player_xspeed != 0 || player_yspeed != 0 ||
            player_blocked_flags != 0x06)
            return fail("ordinary wall latch holds safe position while left remains held");
    }

    /* Starting a fresh sprint while already pressed into the wall is the
     * remaining slot0 failure: the second tap must be neutralized before the
     * source tick can emit Dash/Run velocity into the solid tile. */
    SmwFalconOnStateLoaded();
    player_xpos = 0x0900;
    player_ypos = 0x0160;
    player_sub_xpos = 0x21;
    player_sub_ypos = 0x43;
    player_in_air_flag = 0;
    player_blocked_flags = 0x06;
    player_xspeed = 0;
    player_yspeed = 0;
    io_controller_hold1 = 0x02;
    io_controller_press1 = 0x02;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    if (player_xpos != 0x0900 || player_ypos != 0x0160 ||
        player_sub_xpos != 0x21 || player_sub_ypos != 0x43 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x06 ||
        state == NULL || state->state == FL_DASH || state->state == FL_RUN)
        return fail("already-at-wall left tap is neutralized before Dash/Run");
    for (unsigned i = 0; i != 2; ++i) {
        ++snes_frame_counter;
        player_xpos = 0x08C8;
        player_ypos = 0x0170;
        player_xspeed = 0x90;
        player_yspeed = 0x90;
        player_blocked_flags = 0;
        state->grounded = 1;
        SmwFalconBeforePhysics(NULL);
        if (player_xpos != 0x0900 || player_ypos != 0x0160 ||
            player_xspeed != 0 || player_yspeed != 0 ||
            player_blocked_flags != 0x06)
            return fail("already-at-wall sprint latch holds safe left wall coordinate");
    }

    /* A wall bit opposite the authored facing is not Falcon's forward
     * high-speed step and must remain native-owned. */
    SmwFalconOnStateLoaded();
    player_xpos = 0x0900;
    player_ypos = 0x0160;
    player_in_air_flag = 0;
    io_controller_hold1 = 0x02;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    state = snes_foreign_state();
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = -1.0f;
    player_xpos = 0x08C0;
    player_ypos = 0x0160;
    player_xspeed = 0x90;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x08C0 || player_xspeed != 0x90 ||
        player_blocked_flags != 0x1D)
        return fail("opposite-side crush remains native-owned");

    /* The same bit pattern cannot shield an airborne / vertically displaced
     * Falcon from a real crush. */
    player_xpos = 0x074A;
    player_ypos = 0x015F;
    player_xspeed = 0x65;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    state->grounded = 0;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x074A || player_ypos != 0x015F ||
        player_xspeed != 0x65 || player_yspeed != 0x96 ||
        player_blocked_flags != 0x1D)
        return fail("real vertical crush remains native-owned");

    /* Ground SpecialLw's source flag1 opens at frame 12. A one-block step
     * produces native $77=$1D before CD36/AfterPhysics, so this one-shot
     * correction must restore the exact DC2D snapshot, retain wall+floor,
     * then enter the approved solid-wall Wait directly. The native branch can
     * return nonlocally before CD36, so deliberately do not call
     * SmwFalconAfterPhysics below; no force-airborne or root may remain. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Kick low-step guard");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    player_sub_xpos = 0x33;
    player_sub_ypos = 0x55;
    /* Enter Kick, then advance exactly through authored source frame 12. */
    ground_adapter_frame(0x44, 0x44);
    for (unsigned i = 0; i != 12; ++i)
        ground_adapter_frame(0, 0);
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_FALCON_KICK_GROUND ||
        state->state_frame != 12u)
        return fail("Kick reaches authored Ground SpecialLw flag1 frame");

    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL); /* snapshot and advance to active frame 13 */
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x65;
    player_yspeed = 0x96;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
        player_in_air_flag != 0 || player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("active Ground Kick restores low-step snapshot once ($1D -> $05)");
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_WAIT || !state->grounded)
        return fail("pre-crush Kick wall stop reaches grounded Wait without CD36");
    {
        const uint16_t safe_wall_x = player_xpos;
        for (unsigned i = 0; i != 3; ++i) {
            ++snes_frame_counter;
            SmwFalconBeforePlayerPhysics(NULL);
            SmwFalconBeforePhysics(NULL);
            if (player_in_air_flag != 0 || player_xspeed != 0 ||
                player_yspeed != 0 || player_xpos != safe_wall_x)
                return fail("Kick wall stop holds grounded safe position without root");
            player_blocked_flags = 0x05;
            SmwFalconAfterPhysics(NULL);
            state = snes_foreign_state();
            if (state == NULL || state->state != FL_WAIT || !state->grounded)
                return fail("Kick wall stop remains Wait across native collision ticks");
        }
    }

    /* User repro: a wall-facing grounded Kick can hit the crush branch after
     * the authored damage window too.  Every grounded Kick frame must stop,
     * not only source flag1/active frames 12..<32. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for late Kick wall guard");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    player_sub_xpos = 0x33;
    player_sub_ypos = 0x55;
    ground_adapter_frame(0x44, 0x44);
    for (unsigned i = 0; i != 40; ++i)
        ground_adapter_frame(0, 0);
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_FALCON_KICK_GROUND ||
        state->state_frame <= 32u)
        return fail("late grounded Kick remains in source state after active frames");
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    player_xpos = 0x074A;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x40;
    player_yspeed = 0x90;
    player_blocked_flags = 0x1D;
    SmwFalconBeforeCrushCheck(NULL);
    if (player_xpos != 0x070F || player_ypos != 0x0160 ||
        player_sub_xpos != 0x33 || player_sub_ypos != 0x55 ||
        player_in_air_flag != 0 || player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("late Ground Kick restores low-step snapshot once ($1D -> $05)");
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_WAIT || !state->grounded)
        return fail("late pre-crush Kick wall stop reaches grounded Wait");

    /* UX safety net: taller/awkward walls can skip both the false-crush hook
     * and CD36 before native WRAM has already entered the death/fallthrough
     * state.  A recent Falcon wall-run context must restore the last safe
     * grounded coordinate at the guaranteed normal-sprite seam. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for wall safety rollback");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0800;
    player_ypos = 0x0160;
    player_sub_xpos = 0x12;
    player_sub_ypos = 0x34;
    player_blocked_flags = 0x04;
    player_facing_direction = 1;
    state = snes_foreign_state();
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = 1.0f;
    io_controller_hold1 = 0x01;
    io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    player_current_state = 9;
    player_in_air_flag = 1;
    player_xpos = 0x0838;
    player_ypos = 0x0184;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x65;
    player_yspeed = 0x96;
    player_blocked_flags = 0;
    SmwFalconBeforeNormalSprites(NULL);
    if (player_current_state != 0 || player_xpos != 0x0800 ||
        player_ypos != 0x0160 || player_sub_xpos != 0x12 ||
        player_sub_ypos != 0x34 || player_in_air_flag != 0 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x04)
        return fail("wall safety rollback restores last safe grounded Falcon");

    /* Slot1 left-wall sprint can underflow X to $FFFF while native state is
     * still ordinary player state 00. This is already OOB and must restore
     * immediately, preserving the observed left-wall side bit as $06. */
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x001B;
    player_ypos = 0x0160;
    player_sub_xpos = 0x56;
    player_sub_ypos = 0x78;
    player_blocked_flags = 0x04;
    player_facing_direction = 0;
    state = snes_foreign_state();
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = -1.0f;
    io_controller_hold1 = 0x02;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0xFFFF;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x9B;
    player_yspeed = 0;
    player_blocked_flags = 0x06;
    SmwFalconBeforeNormalSprites(NULL);
    if (player_current_state != 0 || player_xpos != 0x001B ||
        player_ypos != 0x0160 || player_sub_xpos != 0x56 ||
        player_sub_ypos != 0x78 || player_in_air_flag != 0 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x06)
        return fail("wall safety rollback catches X underflow without native death");

    /* Symmetric right-side ejection can stay below $F000 and still be an OOB
     * UX failure.  A recent wall-run frame that moves more than four tiles
     * from the last safe grounded point restores even before native death. */
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0800;
    player_ypos = 0x0160;
    player_sub_xpos = 0x22;
    player_sub_ypos = 0x44;
    player_blocked_flags = 0x04;
    player_facing_direction = 1;
    state = snes_foreign_state();
    state->state = FL_RUN;
    state->grounded = 1;
    state->facing = 1.0f;
    io_controller_hold1 = 0x01;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0860;
    player_ypos = 0x0160;
    player_sub_xpos = 0xA0;
    player_sub_ypos = 0xB0;
    player_xspeed = 0x65;
    player_yspeed = 0;
    player_blocked_flags = 0x05;
    SmwFalconBeforeNormalSprites(NULL);
    if (player_current_state != 0 || player_xpos != 0x0800 ||
        player_ypos != 0x0160 || player_sub_xpos != 0x22 ||
        player_sub_ypos != 0x44 || player_in_air_flag != 0 ||
        player_xspeed != 0 || player_yspeed != 0 ||
        player_blocked_flags != 0x05)
        return fail("wall safety rollback catches large right-side ejection");

    /* A player-collision nonlocal return can omit CD36/AfterPhysics.  The
     * first guaranteed normal-sprite seam must still arm the observer which
     * adopts SMW's exact successful stomp impulse from $01:AA33. */
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for skipped-CD36 stomp");
    SmwFalconOnStateLoaded();
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 0x0700;
    player_ypos = 0x0160;
    io_controller_hold1 = io_controller_press1 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePhysics(NULL);
    memset(&cpu, 0, sizeof(cpu));
    cpu.ram = g_ram;
    cpu.m_flag = cpu.x_flag = 1;
    cpu.P = 0x30;
    cpu.DB = 1;
    cpu.X = 0;
    SmwFalconBeforeNormalSprites(&cpu); /* deliberately omit AfterPhysics */
    timer_player_hurt = 0;
    player_in_air_flag = 1;
    player_yspeed = 0xD0;
    SmwFalconOnNativeStompBounce(&cpu);
    state = snes_foreign_state();
    if (state == NULL || state->vy < 37.49 || state->vy > 37.51 ||
        state->grounded || timer_player_hurt != 1)
        return fail("skipped-CD36 stomp adopts native bounce at guaranteed seam");
    /* The next sprite in the same ProcessNormalSprites pass must not re-arm
     * the already-consumed observer while s_pending is still set. */
    state->vy = 0.0;
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    player_yspeed = 0xD0;
    SmwFalconOnNativeStompBounce(&cpu);
    if (state->vy != 0.0)
        return fail("skipped-CD36 fallback consumes at most one stomp per frame");

    puts("falcon_step_guard_test: PASS");
    return 0;
}
