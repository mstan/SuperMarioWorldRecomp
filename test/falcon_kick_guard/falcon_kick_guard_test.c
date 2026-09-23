/* Direct adapter regression for the narrow active-Kick side-damage guard.
 * It intentionally omits the broad pad/dash fixture so this contract stays
 * deterministic even when that larger integration fixture changes. */
#include "../../overrides/falcon/falcon_smw_adapter.h"
#include "../../src/mods/falcon/captain_falcon_foreign.h"
#include "../../src/mods/falcon/falcon_locomotion.h"
#include "types.h"
#include "cpu_state.h"
#include "../../src/variables.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
int snes_frame_counter;
static int s_native_contacts;

void cpu_write8(CpuState *cpu, uint8 bank, uint16 addr, uint8 value)
{
    if (cpu != NULL && cpu->ram != NULL && (bank == 0 || bank == 1))
        cpu->ram[addr] = value;
}

static int consume_native_frame(CpuState *cpu, uint8 frame_size,
                                uint8 expected_pb)
{
    if (cpu == NULL || cpu->host_return_valid != frame_size ||
        cpu->PB != expected_pb) return 0;
    cpu->S = (uint16)(cpu->S + frame_size);
    return 1;
}

void SprStatus02_Dead_SetNorSprStatus04(CpuState *cpu)
{
    if (cpu == NULL || cpu->m_flag != 1 || cpu->x_flag != 1 ||
        cpu->DB != 1 || cpu->D != 0 ||
        cpu->ram[0x15E9] != (cpu->X & 0xffu) ||
        !consume_native_frame(cpu, 2, 1)) return;
    ++s_native_contacts;
    cpu->ram[0x14C8u + (cpu->X & 0xffu)] = 4;
    cpu->ram[0x1540u + (cpu->X & 0xffu)] = 31;
}
void SpawnSpinJumpStars(CpuState *cpu)
{
    if (cpu == NULL || cpu->m_flag != 1 || cpu->x_flag != 1 ||
        cpu->DB != 1 || cpu->D != 0 ||
        cpu->ram[0x15E9] != (cpu->X & 0xffu) ||
        !consume_native_frame(cpu, 3, 7)) return;
    cpu->ram[0x170B] = 16;
}
void CheckPlayerToNormalSpriteColl_01AB46(CpuState *cpu)
{
    if (cpu == NULL || cpu->m_flag != 1 || cpu->x_flag != 1 ||
        cpu->DB != 1 || cpu->D != 0 ||
        cpu->ram[0x15E9] != (cpu->X & 0xffu) ||
        !consume_native_frame(cpu, 2, 1)) return;
    ++cpu->ram[0x1DFC];
}

void SpawnBounceSprite(CpuState *cpu)
{
    (void)consume_native_frame(cpu, 3, 2);
}
void SpawnBrickPieces(CpuState *cpu)
{
    (void)consume_native_frame(cpu, 3, 2);
}
void GetPlayerLevelCollisionMap16ID_Entry2(CpuState *cpu)
{
    if (!consume_native_frame(cpu, 2, 0)) return;
    cpu->A = 0x25; /* This sprite-contact fixture has empty terrain. */
    cpu->ram[0x1693] = 0;
}
void GenerateTile(CpuState *cpu)
{
    (void)consume_native_frame(cpu, 3, 0);
}
void KillNormalSprite_AcceptedConsequence(CpuState *cpu)
{
    const unsigned slot = cpu != NULL ? cpu->X & 0xffu : 12u;
    if (!consume_native_frame(cpu, 2, 2) || slot >= 12u) return;
    ++s_native_contacts;
    cpu->ram[0x14C8u + slot] = 2;
}
void smw_falcon_audio_play_events(const ForeignAudioEvents *events)
{
    (void)events;
}
int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z)
{
    (void)animation;
    (void)frame;
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    return 0;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static void frame(uint8_t hold1, uint8_t press1, CpuState *cpu)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(cpu);
}

static void normal_frame(uint8_t hold1, uint8_t press1, uint8_t normal_press,
                         CpuState *cpu)
{
    io_controller_hold1 = hold1;
    io_controller_press1 = press1;
    io_controller_hold2 = normal_press;
    io_controller_press2 = normal_press;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    SmwFalconAfterPhysics(cpu);
}

static void model_later_side_damage(void)
{
    if (timer_player_hurt == 0) player_current_state = 9;
}

static int dive_protected_state(int state)
{
    return state == FL_FALCON_DIVE_GROUND ||
           state == FL_FALCON_DIVE_AIR ||
           state == FL_FALCON_DIVE_CATCH ||
           state == FL_FALCON_DIVE_THROW;
}

int main(void)
{
    CpuState cpu;

    memset(g_ram, 0, sizeof(g_ram));
    /* Inert hooks must preserve every native controller bit, including
     * Y/Down, through the compiled sprite seam and native state restore. */
    misc_game_mode = 0x14;
    for (unsigned buttons = 0; buttons < 256; ++buttons) {
        io_controller_hold1 = io_controller_press1 = (uint8_t)buttons;
        io_controller_hold2 = io_controller_press2 = (uint8_t)~buttons;
        SmwFalconBeforePlayerPhysics(NULL);
        SmwFalconBeforePhysics(NULL);
        SmwFalconAfterPhysics(NULL);
        SmwFalconBeforeNormalSprites(NULL);
        SmwFalconOnStateLoaded();
        if (io_controller_hold1 != buttons || io_controller_press1 != buttons ||
            io_controller_hold2 != (uint8_t)~buttons ||
            io_controller_press2 != (uint8_t)~buttons)
            return fail("disabled Falcon preserves native controller input");
    }
    memset(g_ram, 0, sizeof(g_ram));
    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("register/select Falcon");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    /* Slot 8 is the retained Koopa side-contact. Slot 9 is a loose shell:
     * both are immediately ahead, mirroring save1's native lifecycle mix. */
    spr_current_status[8] = 8;
    spr_spriteid[8] = 0x05;
    spr_xpos_lo[8] = 148;
    spr_ypos_lo[8] = 220;
    spr_current_status[9] = 9;
    spr_spriteid[9] = 0x05;
    spr_xpos_lo[9] = 156;
    spr_ypos_lo[9] = 220;

    memset(&cpu, 0, sizeof(cpu));
    cpu.ram = g_ram;
    cpu.m_flag = cpu.x_flag = 1;
    cpu.P = 0x30;

    /* Advance to source frame 12.  Model the real low ground-collision
     * nonlocal return by deliberately omitting CD36/AfterPhysics on the
     * active frame; $01:80D2 must still apply the consequence before native
     * side damage. */
    for (int i = 0; i != 12; ++i)
        frame(i == 0 ? 0x44 : 0, i == 0 ? 0x44 : 0, &cpu);
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (!smw_falcon_last_attack()->active || s_native_contacts != 0)
        return fail("active Kick reaches the normal-sprite seam without CD36");
    /* $01:80D2 is re-entered once per ordinary slot before that slot's
     * collision check. Its bank-$01 PHK/PLB prologue leaves DB=$01; the
     * native $02:9404 consequence must restore that exact caller bank. */
    cpu.DB = 1;
    cpu.X = 8;
    SmwFalconBeforeNormalSprites(&cpu);
    if (s_native_contacts != 2 || spr_current_status[8] != 4 ||
        spr_current_status[9] != 4 || timer_player_hurt != 1 || cpu.DB != 1)
        return fail("normal-sprite seam destroys shell and guards its Koopa");
    model_later_side_damage();
    if (player_current_state != 0)
        return fail("connected Kick slot prevents its same-pass side damage");

    /* The next slot is behind/unhit. Its entry removes exactly the previous
     * guard, so this later native collision stays dangerous. */
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 0)
        return fail("Kick guard clears before the following unhit slot");
    model_later_side_damage();
    if (player_current_state != 9)
        return fail("unhit/behind same-pass slot remains native-dangerous");

    /* Punch uses the same exact-slot metadata as Kick.  Commit its native
     * consequence once at an unrelated $01:80D2 slot, prove that slot sees no
     * timer, then prove the connected slot is protected immediately before
     * its later native side-contact check. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Punch contact guard");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    timer_player_hurt = 0;
    memset(spr_current_status, 0, 12);
    spr_current_status[8] = 8;
    spr_spriteid[8] = 0x05;
    spr_xpos_lo[8] = 148;
    spr_ypos_lo[8] = 220;
    s_native_contacts = 0;
    for (int i = 0; i != 43; ++i)
        frame(i == 0 ? 0x40 : 0, i == 0 ? 0x40 : 0, &cpu);
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (!smw_falcon_last_attack()->active || s_native_contacts != 0)
        return fail("active Punch reaches normal-sprite seam without CD36");
    cpu.DB = 1;
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (s_native_contacts != 1 || spr_current_status[8] != 4 ||
        timer_player_hurt != 0)
        return fail("Punch consequence leaves unrelated slot unguarded");
    cpu.X = 8;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 1)
        return fail("connected Punch slot arms exact native side-damage guard");
    model_later_side_damage();
    if (player_current_state != 0)
        return fail("connected Punch slot blocks same-pass side damage");
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 0)
        return fail("Punch guard clears before the following unhit slot");

    /* Oracle review requires this to remain special-only. A real Jab and a
     * real forward tilt can still apply their ordinary native consequence at
     * $01:80D2, but neither may put the accepted slot in the Punch/Kick
     * $1497 handoff mask: side damage on that same pass must stay native. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Jab no-guard contract");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    timer_player_hurt = 0;
    memset(spr_current_status, 0, 12);
    spr_current_status[8] = 8;
    spr_spriteid[8] = 0x05;
    spr_xpos_lo[8] = 130;
    spr_ypos_lo[8] = 220;
    s_native_contacts = 0;
    for (int i = 0; i != 6; ++i)
        normal_frame(0, 0, i == 0 ? 0x40 : 0, &cpu);
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_state()->state != FL_JAB ||
        !smw_falcon_last_attack()->active)
        return fail("Jab reaches its active normal-sprite seam");
    cpu.DB = 1;
    cpu.X = 8;
    SmwFalconBeforeNormalSprites(&cpu);
    if (s_native_contacts != 1 || spr_current_status[8] != 4 ||
        timer_player_hurt != 0)
        return fail("Jab consequence receives no special-only guard");
    model_later_side_damage();
    if (player_current_state != 9)
        return fail("Jab-connected slot remains native-dangerous");

    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for FTilt no-guard contract");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = 100;
    player_ypos = 200;
    timer_player_hurt = 0;
    memset(spr_current_status, 0, 12);
    spr_current_status[8] = 8;
    spr_spriteid[8] = 0x05;
    spr_xpos_lo[8] = 130;
    spr_ypos_lo[8] = 220;
    s_native_contacts = 0;
    for (int i = 0; i != 10; ++i)
        normal_frame(i == 0 ? 0x01 : 0, i == 0 ? 0x01 : 0,
                     i == 0 ? 0x40 : 0, &cpu);
    io_controller_hold1 = io_controller_press1 = 0;
    io_controller_hold2 = io_controller_press2 = 0;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_state()->state != FL_FTILT ||
        !smw_falcon_last_attack()->active)
        return fail("FTilt reaches its active normal-sprite seam");
    cpu.DB = 1;
    cpu.X = 8;
    SmwFalconBeforeNormalSprites(&cpu);
    if (s_native_contacts != 1 || spr_current_status[8] != 4 ||
        timer_player_hurt != 0)
        return fail("FTilt consequence receives no special-only guard");
    model_later_side_damage();
    if (player_current_state != 9)
        return fail("FTilt-connected slot remains native-dangerous");

    /* A player-collision exit may skip inline $00:CD36 entirely.  Direct
     * SpecialAirLw must nevertheless complete at the first grounded native
     * frame, before $01:80D2's normal-sprite pass/presentation can expose a
     * stale airborne Kick pose or flame.  Keep the move active through its
     * authored attack window, then deliberately omit AfterPhysics exactly as
     * the nonlocal native path does. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for skipped-CD36 air landing");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 1;
    player_blocked_flags = 0;
    player_xpos = 100;
    player_ypos = 200;
    frame(0x44, 0x44, &cpu); /* Down + Square/Y starts direct Air Kick. */
    for (int i = 0; i != 12; ++i)
        frame(0, 0, &cpu);
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(NULL);
    if (snes_foreign_state()->state != FL_FALCON_KICK_AIR ||
        !smw_falcon_last_attack()->active)
        return fail("direct Air Kick remains active before its grounded frame");
    player_in_air_flag = 0;
    player_blocked_flags = 0x04;
    cpu.DB = 1;
    cpu.X = 12;
    SmwFalconBeforeNormalSprites(&cpu); /* CD36 intentionally absent. */
    if (snes_foreign_state()->state != FL_WAIT ||
        snes_foreign_state()->grounded == 0 || smw_falcon_last_attack()->active)
        return fail("skipped-CD36 direct Air Kick lands as immediate idle");

    /* The same skipped-CD36 route must deliver a Dive's catch contact to the
     * source resolver.  Previously it only latched the ledger at $01:80D2,
     * so all following frames saw an inert target and never entered Catch. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for skipped-CD36 Dive catch");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 4;
    player_xpos = 100;
    player_ypos = 200;
    memset(spr_current_status, 0, 12);
    frame(0x48, 0x48, &cpu); /* Up + Square/Y */
    for (int i = 0; i != 40 && !smw_falcon_last_attack()->active; ++i)
        frame(0, 0, &cpu);
    if (!smw_falcon_last_attack()->active)
        return fail("Dive reaches its capture window before fallback test");
    spr_current_status[4] = 8;
    spr_spriteid[4] = 0x0f;
    spr_xpos_lo[4] = 150;
    spr_ypos_lo[4] = 204; /* 16x32/16x24 centres already vertically aligned */
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(&cpu);
    cpu.DB = 1;
    cpu.X = 4;
    SmwFalconBeforeNormalSprites(&cpu); /* deliberately no AfterPhysics */
    if (snes_foreign_state()->state != FL_FALCON_DIVE_CATCH)
        return fail("skipped-CD36 Dive contact resolves into Catch");
    /* Capture convergence is pre-physics and tile-safe: one 4px step toward
     * the exact identity, never a host teleport through a 16px wall. */
    {
    const uint16_t catch_x = player_xpos;
    const uint16_t catch_y = player_ypos;
    ++snes_frame_counter;
    SmwFalconBeforePlayerPhysics(NULL);
    SmwFalconBeforePhysics(&cpu);
    if ((int16_t)(player_xpos - catch_x) != 4 || player_ypos != catch_y) {
        return fail("Dive Catch takes bounded player-side capture snap");
    }
    }

    /* Up-B intentionally differs from Kick's exact-slot guard. Protect every
     * normal-sprite contact from startup through the source Catch/Throw, then
     * for exactly eight generic recovery frames. Drive a real controller
     * launch into an eligible target so this covers the complete lifecycle. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for Up-B guard");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 4;
    player_xpos = 100;
    player_ypos = 200;
    memset(spr_current_status, 0, 12);
    spr_current_status[4] = 8;
    spr_spriteid[4] = 0x0f;
    spr_xpos_lo[4] = 126;
    spr_ypos_lo[4] = 200;
    timer_player_hurt = 5;
    frame(0x48, 0x48, &cpu); /* Up + Square/Y, startup frame zero. */
    cpu.DB = 1;
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (!dive_protected_state(snes_foreign_state()->state) ||
        timer_player_hurt != 5)
        return fail("Up-B preserves a pre-existing native hurt timer");
    timer_player_hurt = 0;
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 1)
        return fail("Up-B protects an unlatched startup contact");
    cpu.X = 2;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 1)
        return fail("Up-B protection spans every slot in one native pass");
    model_later_side_damage();
    if (player_current_state != 0)
        return fail("Up-B startup prevents native side damage");

    {
        int saw_catch = 0;
        int saw_throw = 0;
        int exited = 0;
        int i;
        for (i = 0; i != 180; ++i) {
            frame(0, 0, &cpu);
            cpu.DB = 1;
            cpu.X = 2;
            SmwFalconBeforeNormalSprites(&cpu);
            if (snes_foreign_state()->state == FL_FALCON_DIVE_CATCH)
                saw_catch = 1;
            if (snes_foreign_state()->state == FL_FALCON_DIVE_THROW)
                saw_throw = 1;
            if (dive_protected_state(snes_foreign_state()->state)) {
                if (timer_player_hurt != 1)
                    return fail("Up-B launch/Catch/Throw keeps global guard");
            } else if (saw_throw) {
                exited = 1;
                break;
            }
        }
        if (!saw_catch || !saw_throw || !exited)
            return fail("controller-driven Up-B reaches Catch, Throw, and exit");

        /* The loop's exit frame is grace frame one. Seven more remain safe;
         * the ninth post-exit frame must restore ordinary native danger. */
        if (timer_player_hurt != 1)
            return fail("Up-B first recovery grace frame is protected");
        for (i = 1; i != 8; ++i) {
            frame(0, 0, &cpu);
            cpu.DB = 1;
            cpu.X = 2;
            SmwFalconBeforeNormalSprites(&cpu);
            if (timer_player_hurt != 1)
                return fail("Up-B eight-frame recovery grace is exact");
        }
        frame(0, 0, &cpu);
        cpu.DB = 1;
        cpu.X = 2;
        SmwFalconBeforeNormalSprites(&cpu);
        if (timer_player_hurt != 0)
            return fail("Up-B grace expires before recovery frame nine");
        model_later_side_damage();
        if (player_current_state != 9)
            return fail("ordinary contact is dangerous after Up-B grace");
    }

    /* A new attack consumes residual Up-B grace. Drive a second natural
     * Catch/Throw, then start direct aerial Kick on recovery frame two; an
     * unhit slot must see Kick's normal timer==0 contract, not global armor. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for post-Up-B attack");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 4;
    player_xpos = 100;
    player_ypos = 200;
    memset(spr_current_status, 0, 12);
    spr_current_status[4] = 8;
    spr_spriteid[4] = 0x0f;
    spr_xpos_lo[4] = 126;
    spr_ypos_lo[4] = 200;
    frame(0x48, 0x48, &cpu);
    {
        int saw_throw = 0;
        int i;
        for (i = 0; i != 180; ++i) {
            frame(0, 0, &cpu);
            cpu.DB = 1;
            cpu.X = 2;
            SmwFalconBeforeNormalSprites(&cpu);
            if (snes_foreign_state()->state == FL_FALCON_DIVE_THROW)
                saw_throw = 1;
            if (saw_throw && !dive_protected_state(snes_foreign_state()->state))
                break;
        }
        if (!saw_throw || i == 180 || timer_player_hurt != 1)
            return fail("second Up-B reaches protected recovery");
    }
    frame(0x44, 0x44, &cpu); /* Down + Square/Y starts direct Air Kick. */
    cpu.DB = 1;
    cpu.X = 2;
    SmwFalconBeforeNormalSprites(&cpu);
    if (snes_foreign_state()->state != FL_FALCON_KICK_AIR ||
        timer_player_hurt != 0)
        return fail("new Kick consumes old Up-B global grace");

    /* Loading replaces WRAM but not adapter statics. A stale owned-guard bit
     * must never erase a restored native timer whose saved value is also 1. */
    SmwFalconOnStateLoaded();
    if (!snes_foreign_select(SMW_CAPTAIN_FALCON_ID))
        return fail("reset selected controller for load timer ownership");
    misc_game_mode = 0x14;
    player_current_state = 0;
    player_in_air_flag = 0;
    player_blocked_flags = 4;
    frame(0x48, 0x48, &cpu);
    timer_player_hurt = 0;
    cpu.DB = 1;
    cpu.X = 1;
    SmwFalconBeforeNormalSprites(&cpu);
    if (timer_player_hurt != 1)
        return fail("arm Dive-owned timer before state load");
    /* Model a snapshot restoring a native one-frame value at the same byte. */
    timer_player_hurt = 1;
    SmwFalconOnStateLoaded();
    if (timer_player_hurt != 1)
        return fail("state load preserves restored native timer value one");
    puts("falcon_kick_guard_test: PASS");
    return 0;
}
