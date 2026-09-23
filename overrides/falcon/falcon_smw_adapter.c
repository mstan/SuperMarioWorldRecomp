/* Native SMW host boundary for the portable Captain Falcon controller. */
#include "falcon_smw_adapter.h"

#include "types.h"
#include "variables.h"
#include "common_rtl.h"
#include "src/mods/falcon/falcon_locomotion.h"
#include "src/mods/falcon/smw_falcon_audio.h"
#include "src/mods/falcon/smw_falcon_combat_apply.h"

#include <stdint.h>
#include <string.h>

#define FALCON_TO_SMW_PX 0.08
#define SMW_SPEED_PER_PX 16.0
#define SMW_TO_FALCON (1.0 / FALCON_TO_SMW_PX)

/* ADAPTATION: keep the source controller in water, but map its vertical
 * output through a deliberately floaty SMW host envelope. This avoids handing
 * the pad back to native swim while keeping Falcon attacks and air states. */
#define SMW_FALCON_WATER_VERTICAL_SCALE 0.45
#define SMW_FALCON_WATER_TERMINAL_FALL 42.0
#define SMW_FALCON_DASH_DOUBLE_TAP_FRAMES 15
#define SMW_FALCON_DIVE_IFRAME_GRACE_FRAMES 8u
#define SMW_FALCON_WALL_SAFETY_FRAMES 12u
#define SMW_FALCON_WALL_OOB_MAX_DELTA 64
/* Approved owner cache FalconDive TransN is subpixel through source frame 13
 * and first produces an upward SMW speed at frame 14.  The source resolver
 * itself preserves grounded-Dive air kinetics through frame 15. */
#define SMW_FALCON_DEPARTURE_MAX_FRAMES 16u

static int s_pending;
static int s_force_airborne_pending;
static unsigned s_force_airborne_frames;
static int s_dash_first_dir;
static int s_dash_prev_dir;
static int s_dash_full_hold;
static int s_special_grace_pending;
static int s_dash_ignore_until_release;
static unsigned s_dash_tap_age;
static int s_stomp_bounce_armed;
static int s_stomp_bounce_consumed;
static int s_stomp_contact_guard;
static SmwFalconCombatLedger s_combat_ledger;
/* Exact normal-sprite slots whose accepted Punch/Kick consequence belongs to
 * this ProcessNormalSprites pass.  This is deliberately not an attack-wide
 * immunity flag: each slot receives at most its own pre-contact timer tick. */
static uint16_t s_impact_contact_slots;
static int s_impact_slot_guard;
static uint16_t s_dive_catch_slots;
static int s_dive_catch_slot_guard;
static unsigned s_dive_iframe_grace_frames;
static int s_dive_iframes_active;
static int s_dive_iframe_timer_guard;
/* $01:80D2 executes once per normal-sprite slot.  A move consequence is
 * global to the frame, not to each slot, so remember the one pass which
 * committed it. */
static int s_combat_apply_frame = -1;
static int s_block_apply_frame = -1;
static int s_last_input_direction;
static int s_step_wall_latched;
static int s_step_wall_direction;
static uint16_t s_x_before;
static uint16_t s_y_before;
static uint8_t s_sub_x_before;
static uint8_t s_sub_y_before;
static uint8_t s_in_air_before;
static uint16_t s_step_wall_x;
static uint16_t s_step_wall_y;
static uint8_t s_step_wall_sub_x;
static uint8_t s_step_wall_sub_y;
static uint8_t s_step_wall_in_air;
static uint8_t s_step_wall_blocked;
static uint8_t s_step_wall_facing;
static unsigned s_wall_safety_recent_frames;
static int s_wall_safety_hazard_armed;
static int s_wall_safety_valid;
static uint16_t s_wall_safety_x;
static uint16_t s_wall_safety_y;
static uint8_t s_wall_safety_sub_x;
static uint8_t s_wall_safety_sub_y;
static uint8_t s_wall_safety_facing;
static uint8_t s_wall_safety_blocked;
static ForeignMoveResult s_last_move;
static struct {
    uint8_t hold1;
    uint8_t press1;
    uint8_t hold2;
    uint8_t press2;
    uint8_t carry_a;
    uint8_t carry_down;
    int valid;
    int carry_valid;
} s_foreign_pad;

static int signed8(uint8_t value) { return (int)(int8_t)value; }

static int smw_falcon_ground_run_wall_state(int state);
static void smw_falcon_reseed(ForeignState *state);

static void smw_falcon_reset_dash_taps(void)
{
    s_dash_first_dir = 0;
    s_dash_prev_dir = 0;
    s_dash_full_hold = 0;
    s_special_grace_pending = 0;
    s_dash_ignore_until_release = 0;
    s_dash_tap_age = 0;
}

static void smw_falcon_clear_step_wall_latch(void)
{
    s_step_wall_latched = 0;
    s_step_wall_direction = 0;
    s_step_wall_blocked = 0;
}

static void smw_falcon_restore_step_wall_latch(void)
{
    player_xpos = s_step_wall_x;
    player_ypos = s_step_wall_y;
    player_sub_xpos = s_step_wall_sub_x;
    player_sub_ypos = s_step_wall_sub_y;
    player_in_air_flag = s_step_wall_in_air;
    player_sub_xspeed = player_sub_yspeed = 0;
    player_xspeed = player_yspeed = 0;
    player_facing_direction = s_step_wall_facing;
    /* Preserve unrelated high bits but retain this side's wall and floor. */
    player_blocked_flags = (uint8_t)((player_blocked_flags & 0xE0u) |
                                     s_step_wall_blocked);
}

static void smw_falcon_install_step_wall_latch(uint8_t blocked_flags)
{
    s_step_wall_latched = 1;
    s_step_wall_direction = s_last_input_direction;
    s_step_wall_x = s_x_before;
    s_step_wall_y = s_y_before;
    s_step_wall_sub_x = s_sub_x_before;
    s_step_wall_sub_y = s_sub_y_before;
    s_step_wall_in_air = s_in_air_before;
    s_step_wall_facing = player_facing_direction;
    s_step_wall_blocked = (uint8_t)((blocked_flags & 0x03u) | 0x04u);
    smw_falcon_restore_step_wall_latch();
}

static void smw_falcon_install_current_step_wall_latch(int direction,
                                                       uint8_t blocked_flags)
{
    s_step_wall_latched = 1;
    s_step_wall_direction = direction;
    s_step_wall_x = player_xpos;
    s_step_wall_y = player_ypos;
    s_step_wall_sub_x = player_sub_xpos;
    s_step_wall_sub_y = player_sub_ypos;
    s_step_wall_in_air = player_in_air_flag;
    s_step_wall_facing = player_facing_direction;
    s_step_wall_blocked = (uint8_t)((blocked_flags & 0x03u) | 0x04u);
    smw_falcon_restore_step_wall_latch();
}

static void smw_falcon_note_wall_safety_context(const ForeignState *state)
{
    int wall_hazard = 0;
    if (state == NULL) return;
    if (player_in_air_flag == 0 &&
        ((player_blocked_flags & 0x03u) != 0 || s_step_wall_latched))
        wall_hazard = 1;
    if (wall_hazard) {
        s_wall_safety_hazard_armed = 1;
        s_wall_safety_recent_frames = SMW_FALCON_WALL_SAFETY_FRAMES;
    } else if (s_wall_safety_recent_frames != 0) {
        --s_wall_safety_recent_frames;
    } else if (player_in_air_flag == 0 &&
               (player_blocked_flags & 0x04u) != 0 &&
               !smw_falcon_ground_run_wall_state(state->state)) {
        s_wall_safety_hazard_armed = 0;
    }
}

static void smw_falcon_remember_wall_safe_ground(void)
{
    if (!snes_foreign_active() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        misc_game_mode != 0x14 || player_current_state != 0 ||
        player_in_air_flag != 0 || (player_blocked_flags & 0x04u) == 0 ||
        player_xpos >= 0xF000u)
        return;
    s_wall_safety_valid = 1;
    s_wall_safety_x = player_xpos;
    s_wall_safety_y = player_ypos;
    s_wall_safety_sub_x = player_sub_xpos;
    s_wall_safety_sub_y = player_sub_ypos;
    s_wall_safety_facing = player_facing_direction;
    s_wall_safety_blocked = (uint8_t)((player_blocked_flags & 0x03u) | 0x04u);
}

static int smw_falcon_wall_safety_should_restore(void)
{
    int16_t dx;
    int16_t dy;
    if (!s_wall_safety_valid || !snes_foreign_active() ||
        misc_game_mode != 0x14)
        return 0;
    dx = (int16_t)(player_xpos - s_wall_safety_x);
    dy = (int16_t)(player_ypos - s_wall_safety_y);
    /* $00:E9FB false-crush and $00:F595 pit/OOB both converge on native
     * state $09, but ordinary enemy damage can also enter state $09. Restore
     * death only while a wall/OOB hazard has been observed; never turn the
     * boundary safety net into global enemy invulnerability. */
    if (player_current_state == 9)
        return s_wall_safety_hazard_armed ||
               s_wall_safety_recent_frames != 0 ||
               player_xpos >= 0xF000u;
    if (player_xpos >= 0xF000u)
        return 1;
    if (!s_wall_safety_hazard_armed && s_wall_safety_recent_frames == 0)
        return 0;
    if (dx > SMW_FALCON_WALL_OOB_MAX_DELTA ||
        dx < -SMW_FALCON_WALL_OOB_MAX_DELTA)
        return 1;
    if (dy > 24 && player_in_air_flag != 0)
        return 1;
    if (player_in_air_flag != 0 && (player_blocked_flags & 0x03u) != 0)
        return 1;
    return 0;
}

static int smw_falcon_restore_wall_safe_ground(void)
{
    ForeignCollisionResult wall;
    ForeignState *state;
    int direction;
    uint8_t observed_side;
    uint8_t restore_blocked;

    if (!smw_falcon_wall_safety_should_restore())
        return 0;

    observed_side = (uint8_t)(player_blocked_flags & 0x03u);
    restore_blocked = (uint8_t)(((observed_side != 0
                                  ? observed_side
                                  : (s_wall_safety_blocked & 0x03u)) |
                                 0x04u));
    player_current_state = 0;
    player_in_air_flag = 0;
    player_xpos = s_wall_safety_x;
    player_ypos = s_wall_safety_y;
    player_sub_xpos = s_wall_safety_sub_x;
    player_sub_ypos = s_wall_safety_sub_y;
    player_xspeed = player_yspeed = 0;
    player_sub_xspeed = player_sub_yspeed = 0;
    player_facing_direction = s_wall_safety_facing;
    player_blocked_flags = restore_blocked;
    if (timer_player_hurt == 1)
        timer_player_hurt = 0;

    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    state = snes_foreign_state();
    if (state != NULL) {
        smw_falcon_reseed(state);
        memset(&wall, 0, sizeof(wall));
        wall.grounded = 1;
        wall.hit_floor = 1;
        wall.hit_wall = 1;
        snes_foreign_resolve(&wall);
    }

    direction = (restore_blocked & 0x01u) ? 1 :
                (restore_blocked & 0x02u) ? -1 :
                (s_wall_safety_facing ? 1 : -1);
    smw_falcon_install_current_step_wall_latch(direction, restore_blocked);
    s_pending = 0;
    s_force_airborne_pending = 0;
    s_force_airborne_frames = 0;
    s_wall_safety_hazard_armed = 1;
    s_wall_safety_recent_frames = SMW_FALCON_WALL_SAFETY_FRAMES;
    return 1;
}

static void smw_falcon_clear_stomp_contact_guard(void)
{
    /* $1497 is SMW's IFrameTimer, checked by the later native/custom-sprite
     * side-damage path. We write only its one-frame value and remove exactly
     * that value at the next early player seam. A real native timer update is
     * never overwritten. */
    if (s_stomp_contact_guard && timer_player_hurt == 1)
        timer_player_hurt = 0;
    s_stomp_contact_guard = 0;
}

static int smw_falcon_dive_iframe_state(int state)
{
    return state == FL_FALCON_DIVE_GROUND ||
           state == FL_FALCON_DIVE_AIR ||
           state == FL_FALCON_DIVE_CATCH ||
           state == FL_FALCON_DIVE_THROW;
}

static int smw_falcon_dive_iframe_recovery_state(int state)
{
    return state == FL_FALL || state == FL_FALL_AERIAL ||
           state == FL_FALCON_DIVE_FALL ||
           state == FL_LANDING_LIGHT || state == FL_LANDING_HEAVY ||
           state == FL_FALCON_DIVE_LANDING || state == FL_WAIT;
}

static void smw_falcon_clear_dive_iframe_timer_guard(void)
{
    /* This bridge owns only the exact one-frame value it installed. Native
     * power-up/star/hurt timers (>1) remain authoritative and untouched. */
    if (s_dive_iframe_timer_guard && timer_player_hurt == 1)
        timer_player_hurt = 0;
    s_dive_iframe_timer_guard = 0;
}

static void smw_falcon_forget_dive_iframes(void)
{
    s_dive_iframe_grace_frames = 0;
    s_dive_iframes_active = 0;
    s_dive_iframe_timer_guard = 0;
}

static void smw_falcon_reset_dive_iframes(void)
{
    smw_falcon_clear_dive_iframe_timer_guard();
    smw_falcon_forget_dive_iframes();
}

static void smw_falcon_advance_dive_iframes(int state)
{
    if (smw_falcon_dive_iframe_state(state)) {
        s_dive_iframes_active = 1;
        s_dive_iframe_grace_frames = SMW_FALCON_DIVE_IFRAME_GRACE_FRAMES;
    } else if (s_dive_iframe_grace_frames != 0 &&
               smw_falcon_dive_iframe_recovery_state(state)) {
        /* The first eight generic fall/idle frames after Up-B remain safe. */
        s_dive_iframes_active = 1;
        --s_dive_iframe_grace_frames;
    } else {
        /* Starting another move consumes the old Up-B grace immediately;
         * Punch/Kick must retain their narrower contact contracts. */
        s_dive_iframe_grace_frames = 0;
        s_dive_iframes_active = 0;
    }
}

static void smw_falcon_snap_dive_toward_target(CpuState *cpu,
                                                const ForeignState *state)
{
    int dx, dy;

    if (cpu == NULL || state == NULL ||
        (state->state != FL_FALCON_DIVE_CATCH &&
         state->state != FL_FALCON_DIVE_THROW) ||
        !smw_falcon_combat_dive_snap_delta(cpu, &s_combat_ledger, &dx, &dy))
        return;
    /* This runs before SMW's ordinary movement/collision pass.  The combat
     * helper caps each requested vector at four pixels (well below a 16px
     * tile), so native collision still owns any wall correction rather than
     * a host teleport crossing level geometry. */
    player_xpos = (uint16_t)(player_xpos + dx);
    player_ypos = (uint16_t)(player_ypos + dy);
}

static int smw_falcon_impact_guard_state(int state)
{
    /* Only source specials that this bridge deliberately maps to a native
     * sprite consequence receive the one-slot $1497 handoff.  Jabs, tilts,
     * aerial normals and every future non-special attack retain native
     * contact behavior until they get their own explicit contract. */
    return state == FL_FALCON_PUNCH_GROUND ||
           state == FL_FALCON_PUNCH_AIR ||
           state == FL_FALCON_KICK_GROUND ||
           state == FL_FALCON_KICK_AIR ||
           state == FL_FALCON_KICK_LANDING;
}

static int smw_falcon_ground_run_wall_state(int state)
{
    return state == FL_DASH || state == FL_RUN || state == FL_TURN_RUN;
}

static uint8_t clamp_speed(double source_delta, int y_axis)
{
    /* SMW stores a signed 8-bit speed in sixteenth-pixel units. Falcon's
     * +Y-up request is inverted exactly once here because SMW's +Y is down. */
    double value = source_delta * FALCON_TO_SMW_PX * SMW_SPEED_PER_PX;
    int rounded;
    if (y_axis) value = -value;
    rounded = value >= 0.0 ? (int)(value + 0.5) : (int)(value - 0.5);
    if (rounded > 127) rounded = 127;
    if (rounded < -127) rounded = -127;
    return (uint8_t)(int8_t)rounded;
}

/* A source controller's departure request is an edge, but SMW's collision
 * probe can still find the floor while the source animation has not yet
 * accumulated a representable upward displacement.  Keep the established
 * SMW airborne value and a one-pixel upward opportunity live only until the
 * native collision result accepts the lift.  This is the same bounded
 * quantization bridge used by the mature NES host seam, not a repeated input
 * or a controller-owned position write. */
static void smw_falcon_hold_departure_edge(int advance_timeout)
{
    if (!s_force_airborne_pending) return;
    if (advance_timeout &&
        ++s_force_airborne_frames > SMW_FALCON_DEPARTURE_MAX_FRAMES) {
        /* A malformed controller or a true obstruction cannot pin SMW in an
         * artificial airborne state. Native collision regains authority. */
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
        return;
    }
    player_in_air_flag = 1;
    if (signed8(player_yspeed) >= 0)
        player_yspeed = (uint8_t)(int8_t)-16;
}

static int smw_falcon_playable(void)
{
    const int active_pipe_handoff =
        flag_about_to_warp_in_pipe != 0 ||
        (player_pipe_action != 0 && player_pipe_action < 4);

    /* `$89` is overloaded. Values 1..3 are active pipe/sublevel handoffs, but
     * values 4..7 are persistent level-entrance metadata after the native
     * entry pipe returns the player to ordinary state. Pipe tops can also
     * leave `$88=20,$89=06` while the player is simply standing on them. Water
     * entrances can leave `$89=07`; treating either as a live pipe permanently
     * stranded Falcon in SCRIPTED ownership and exposed native Mario. */
    return misc_game_mode == 0x14 && player_current_state == 0 &&
           !active_pipe_handoff && timer_end_level == 0 &&
           timer_end_level_via_keyhole == 0;
}

static int smw_falcon_yoshi_lock_active(void)
{
    /* Keep the title/attract demo byte-exact.  In an ordinary level this is
     * deliberately broader than FOREIGN ownership: activation starts
     * SCRIPTED, and a restored Falcon save may reach the Yoshi code before
     * the first controller tick reclaims movement. */
    return snes_foreign_active() != NULL && misc_game_mode == 0x14;
}

static void smw_falcon_dismount_yoshi(void)
{
    unsigned i;

    /* A live rider is encoded by both $187A and the mounted Yoshi's $00C2=1
     * state. Drop both latches, but do not alter sprite status/position or
     * the selected slot ($18DF/$18E2), owned-Yoshi flags ($0DBA/$0DC1),
     * colour/wings, or level-transition metadata. Native SMW therefore keeps
     * the actual Yoshi entity and progression in its off-Yoshi state. */
    player_riding_yoshi_flag = 0;
    for (i = 0; i != 12; ++i) {
        if (spr_spriteid[i] == 0x35 && spr_table00c2[i] == 1)
            spr_table00c2[i] = 0;
    }

    /* These are player-owned tongue startup/visibility timers. */
    timer_yoshi_tongue_is_out = 0;
    timer_yoshi_tongue_init = 0;
}

static void smw_falcon_disable_native_extensions(void)
{
    /* Falcon's health/progression remains SMW-owned: do not change $19 or the
     * reserve item box. Only suppress native actions that compete with the
     * foreign controller while it owns the playable frame. */
    player_spin_jump_flag = 0;
    player_spinjump_fireball_timer = 0;
    timer_display_player_shoot_fireball_pose = 0;
    player_cape_image = 0;
    flag_cape_to_sprite_interaction = 0;
    timer_active_cape_spin = 0;
    timer_cape_flap_animation = 0;
    timer_wait_before_cape_flight_begins = 0;
    timer_time_to_float_after_cape_flight = 0;
    player_cape_flying_phase = 0;
    player_cape_glide_index = 0;
    player_furthest_cape_dive_stage = 0;

    /* Yoshi's active rider/tongue state cannot share Falcon's host boundary.
     * Preserve the persistent owned-Yoshi flags and the level entity. */
    smw_falcon_dismount_yoshi();

    if (flag_underwater_level)
        player_can_jump_out_of_water = 0;
}

static void smw_falcon_capture_and_mask_input(void)
{
    /* This runs at HandlePlayerPhysics ($00:D5F2), before SMW reads the
     * controller for movement, spin, fire, cape, or swimming. Preserve the
     * physical pad for Falcon's downstream tick, then remove only native
     * gameplay buttons. Start/Select retain their normal system behaviour. */
    s_foreign_pad.hold1 = io_controller_hold1;
    s_foreign_pad.press1 = io_controller_press1;
    s_foreign_pad.hold2 = io_controller_hold2;
    s_foreign_pad.press2 = io_controller_press2;
    s_foreign_pad.carry_a = (io_controller_hold2 & 0x80) != 0;
    s_foreign_pad.carry_down = (io_controller_hold1 & 0x04) != 0;
    s_foreign_pad.valid = 1;
    s_foreign_pad.carry_valid = 1;

    io_controller_hold1 &= (uint8_t)~0xCF;  /* B,Y,U,D,L,R */
    io_controller_press1 &= (uint8_t)~0xCF;
    io_controller_hold2 &= (uint8_t)~0xC0;  /* A carry, X normal */
    io_controller_press2 &= (uint8_t)~0xC0;

    if (player_pipe_action >= 4) {
        /* `$89 >= 4` marks pipe-adjacent/entrance metadata, not an active
         * handoff.  Keep only native Up/Down visible so SMW can still start
         * its own pipe-entry script while Falcon owns ordinary movement and
         * action buttons. */
        io_controller_hold1 |= (uint8_t)(s_foreign_pad.hold1 & 0x0Cu);
        io_controller_press1 |= (uint8_t)(s_foreign_pad.press1 & 0x0Cu);
    }
}

static void smw_falcon_clear_carry_bridge(void)
{
    s_foreign_pad.carry_a = 0;
    s_foreign_pad.carry_down = 0;
    s_foreign_pad.carry_valid = 0;

    /* Disabled hooks still run at compiled sprite boundaries and state load.
     * Native Y/Down belongs to the current native player in that mode. */
    if (!snes_foreign_active()) return;

    /* These bits were emitted only after native player physics. Remove them
     * when a scripted handoff preempts the normal next-frame input refresh. */
    io_controller_hold1 &= (uint8_t)~0x44;  /* translated Y and Down */
    io_controller_press1 &= (uint8_t)~0x40;
}

static void smw_falcon_emit_carry_input(void)
{
    if (!s_foreign_pad.carry_valid) return;

    /* SMWDisX $01:AA42 owns pickup eligibility and changes a valid sprite to
     * native status $0B. Its $01:9F9B carried lifecycle subsequently reads
     * Y/Down: A held means Y held; A released means native throw, with Down
     * retained only for native set-down. Physical Y never enters this bridge. */
    io_controller_hold1 &= (uint8_t)~0x44;
    io_controller_press1 &= (uint8_t)~0x40;
    if (s_foreign_pad.carry_a) {
        io_controller_hold1 |= 0x40;
    } else if (player_carrying_something_flag1 != 0 &&
               s_foreign_pad.carry_down) {
        io_controller_hold1 |= 0x04;
    }
}

static void smw_falcon_adapt_water_motion(ForeignMoveResult *move)
{
    double vertical;
    if (!flag_underwater_level) return;

    /* Source +Y is up. Clamp only falling speed, then scale every vertical
     * velocity so its gravity also feels buoyant at the SMW boundary. */
    vertical = move->requested_dy;
    if (vertical < -SMW_FALCON_WATER_TERMINAL_FALL)
        vertical = -SMW_FALCON_WATER_TERMINAL_FALL;
    move->requested_dy = vertical * SMW_FALCON_WATER_VERTICAL_SCALE;
    move->vy = move->requested_dy;

    /* Never let SMW's swim-button branch create a separate movement model. */
    player_can_jump_out_of_water = 0;
}

static void smw_falcon_reseed(ForeignState *state)
{
    state->x = (double)player_xpos * SMW_TO_FALCON;
    state->y = -(double)player_ypos * SMW_TO_FALCON;
    state->vx = (double)signed8(player_xspeed) /
                (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    state->vy = -(double)signed8(player_yspeed) /
                (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    state->grounded = player_in_air_flag == 0;
    state->air_cause = FOREIGN_AIR_NONE;
    state->facing = player_facing_direction ? 1.0f : -1.0f;
    snes_foreign_trace_note_reseed();
}

static ForeignInput smw_falcon_input(void)
{
    ForeignInput input;
    const uint8_t hold1 = s_foreign_pad.valid ? s_foreign_pad.hold1 :
                                                io_controller_hold1;
    const uint8_t press1 = s_foreign_pad.valid ? s_foreign_pad.press1 :
                                                 io_controller_press1;
    const uint8_t hold2 = s_foreign_pad.valid ? s_foreign_pad.hold2 :
                                                io_controller_hold2;
    const uint8_t press2 = s_foreign_pad.valid ? s_foreign_pad.press2 :
                                                 io_controller_press2;
    const int special_press = (press1 & 0x40) != 0;

    memset(&input, 0, sizeof(input));
    int direction = (hold1 & 0x01) ? 1 : (hold1 & 0x02) ? -1 : 0;

    /* $15 is %byetUDLR; $17 is %axlr0000.
     *
     * Smash's analogue tap buffer treats one 0->full stick edge as dash. A
     * D-pad has no walk magnitude, so expose a 0.5 walk on the first tap and
     * a full source stick only for the second same-direction tap within 15
     * frames.  The source Dash->Run transition is otherwise untouched. */
    if (s_dash_first_dir != 0) {
        if (s_dash_tap_age < SMW_FALCON_DASH_DOUBLE_TAP_FRAMES)
            ++s_dash_tap_age;
        else
            s_dash_first_dir = 0;
    }
    if (s_dash_ignore_until_release) {
        /* A turn-start press is the first edge of a deliberate double tap.
         * It remains held through authored Turn frames, so wait for neutral
         * before accepting the second edge, but retain its tap buffer. */
        s_dash_full_hold = 0;
        if (direction == 0) s_dash_ignore_until_release = 0;
    } else {
        if (direction == 0) {
            s_dash_full_hold = 0;
        } else if (direction != s_dash_prev_dir) {
            if (direction == s_dash_first_dir &&
                s_dash_tap_age <= SMW_FALCON_DASH_DOUBLE_TAP_FRAMES) {
                s_dash_full_hold = 1;
                s_dash_first_dir = 0;
                s_dash_tap_age = 0;
            } else {
                s_dash_full_hold = 0;
                s_dash_first_dir = direction;
                s_dash_tap_age = 0;
            }
        } else {
            /* Still holding the same source magnitude. */
        }
    }
    s_dash_prev_dir = direction;
    s_last_input_direction = direction;
    input.stick_x = direction == 0 ? 0.0f :
                    direction * (s_dash_full_hold ? 1.0f : 0.5f);
    input.stick_y = (hold1 & 0x08) ? 1.0f : (hold1 & 0x04) ? -1.0f : 0.0f;
    input.jump_pressed = (press1 & 0x80) != 0; /* PlayStation Cross / SNES B */
    input.jump_held = (hold1 & 0x80) != 0;
    input.down_pressed = (press1 & 0x04) != 0;
    input.attack_pressed = (press2 & 0x40) != 0; /* SNES X: normal */
    /* Port the mature NES bridge's one-frame directional-special grace.
     * A directionless Square/Y edge waits one frame so Y then Up still
     * selects Falcon Dive rather than committing Falcon Punch.  Directional
     * edges remain immediate and source-state priority remains authoritative. */
    if (special_press) {
        if (direction != 0 || input.stick_y != 0.0f) {
            input.special_pressed = 1;
            s_special_grace_pending = 0;
        } else {
            s_special_grace_pending = 1;
        }
    } else if (s_special_grace_pending) {
        input.special_pressed = 1;
        s_special_grace_pending = 0;
    }
    input.raw_buttons = (int)hold1 | ((int)hold2 << 8);
    if (flag_underwater_level != 0)
        input.raw_buttons |= 0x10000000;
    s_foreign_pad.valid = 0;
    return input;
}

void SmwFalconBeforePlayerPhysics(struct CpuState *cpu)
{
    (void)cpu;
    smw_falcon_clear_stomp_contact_guard();
    smw_falcon_clear_dive_iframe_timer_guard();
    /* ProcessNormalSprites is wholly within the preceding player frame. A
     * missing/aborted pass must never carry a pending Kick slot into the next
     * one, and the old exact guard value is safe to remove at D5F2. */
    if ((s_impact_slot_guard || s_dive_catch_slot_guard) &&
        timer_player_hurt == 1)
        timer_player_hurt = 0;
    s_impact_slot_guard = 0;
    s_dive_catch_slot_guard = 0;
    s_impact_contact_slots = 0;
    s_dive_catch_slots = 0;
    s_foreign_pad.valid = 0;
    s_foreign_pad.carry_valid = 0;

    /* $00:D5F2 is the action-input seam. Do not move this work to the later
     * $00:DC2D velocity seam: native spin/cape/fire decisions have already
     * happened there. */
    /* Activation enters SCRIPTED and is reclaimed at $00:DC2D. Mask here in
     * either ownership state so that handoff's first playable frame cannot
     * leak B/Y/X/A into native SMW before the later controller tick. */
    if (!snes_foreign_active() || !smw_falcon_playable())
    {
        smw_falcon_reset_dive_iframes();
        smw_falcon_clear_step_wall_latch();
        smw_falcon_reset_dash_taps();
        return;
    }
    smw_falcon_capture_and_mask_input();
    smw_falcon_disable_native_extensions();
}

void SmwFalconBeforePhysics(struct CpuState *cpu)
{
    ForeignState *state;
    ForeignInput input;
    int departure_started = 0;

    /* BoostMarioSpeed runs later in ProcessNormalSprites. Never let its
     * previous-frame observation cross a reset, handoff, or next tick. */
    s_stomp_bounce_armed = 0;
    s_stomp_bounce_consumed = 0;
    s_stomp_contact_guard = 0;
    smw_falcon_restore_wall_safe_ground();
    if (!snes_foreign_active()) {
        /* A deselected mod has no controller tick in which to age a Catch.
         * Drop the host-only catch identity rather than allowing a later
         * reselect to release an old sprite. */
        smw_falcon_combat_ledger_update(&s_combat_ledger, 0, 0);
        s_dive_catch_slots = 0;
        s_dive_catch_slot_guard = 0;
        smw_falcon_reset_dive_iframes();
        smw_falcon_clear_step_wall_latch();
        s_wall_safety_recent_frames = 0;
        s_wall_safety_hazard_armed = 0;
        s_wall_safety_valid = 0;
        return;
    }
    if (!smw_falcon_playable()) {
        if (snes_foreign_ownership() == FOREIGN_OWNERSHIP_FOREIGN)
            snes_foreign_set_ownership(FOREIGN_OWNERSHIP_SCRIPTED);
        s_pending = 0;
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
        smw_falcon_combat_ledger_update(&s_combat_ledger, 0, 0);
        smw_falcon_clear_step_wall_latch();
        smw_falcon_reset_dash_taps();
        smw_falcon_clear_carry_bridge();
        smw_falcon_reset_dive_iframes();
        return;
    }

    state = snes_foreign_state();
    if (!state) return;
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        /* Script/death/pipe/goal handoffs leave native WRAM authoritative.
         * Re-select resets transient move state before controllable play. */
        const char *id = snes_foreign_active()->id;
        smw_falcon_reset_dive_iframes();
        smw_falcon_clear_step_wall_latch();
        if (!snes_foreign_select(id)) return;
        state = snes_foreign_state();
        smw_falcon_reseed(state);
        snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    }

    smw_falcon_note_wall_safety_context(state);
    smw_falcon_remember_wall_safe_ground();

    /* Smash CaptureCaptain moves both bodies toward the capture anchor.
     * Apply the bounded Falcon-side convergence before native physics so
     * SMW, not a post-collision host write, remains authoritative for walls. */
    smw_falcon_snap_dive_toward_target(cpu, state);

    /* This must precede state->grounded.  A floor rediscovered after the
     * previous tick is a host quantization artefact until native collision
     * has accepted a whole upward lift; feeding it back now would turn a
     * grounded Falcon Dive into a perpetual floor-bound pose. */
    if (s_force_airborne_pending) {
        smw_falcon_hold_departure_edge(1);
    }

    if (state->grounded && player_in_air_flag != 0)
        state->air_cause = signed8(player_yspeed) < 0 ? FOREIGN_AIR_LAUNCHED
                                                       : FOREIGN_AIR_FELL;
    else
        state->air_cause = FOREIGN_AIR_NONE;
    state->x = (double)player_xpos * SMW_TO_FALCON;
    state->y = -(double)player_ypos * SMW_TO_FALCON;
    state->grounded = player_in_air_flag == 0;

    input = smw_falcon_input();
    /* If Falcon is already grounded against a side wall, a fresh double-tap
     * into that wall must not be allowed to become a high-speed Dash/Run
     * request before native collision gets another chance to respond.  Treat
     * this as the same held-wall state installed after an approach collision,
     * but seed it from the current native-safe coordinate. */
    if (!s_step_wall_latched && state->grounded && player_in_air_flag == 0) {
        const int held_direction = input.stick_x > 0.0f ? 1 :
                                   input.stick_x < 0.0f ? -1 : 0;
        const uint8_t expected_side = held_direction > 0 ? 0x01u :
                                      held_direction < 0 ? 0x02u : 0u;
        if (expected_side != 0 &&
            (player_blocked_flags & 0x04u) != 0 &&
            (player_blocked_flags & 0x03u) == expected_side) {
            smw_falcon_install_current_step_wall_latch(
                held_direction, player_blocked_flags);
            input.stick_x = 0.0f;
        }
    }
    /* A $77=$1D step latch is only a held-horizontal wall stop. It releases
     * on neutral, reversal, jump, loss of ground, or any outer handoff; it
     * never grants general crush immunity. Feed neutral stick to the source
     * while held so Run cannot immediately re-enter the false-crush branch. */
    if (s_step_wall_latched) {
        const int held_direction = input.stick_x > 0.0f ? 1 :
                                   input.stick_x < 0.0f ? -1 : 0;
        if (held_direction != s_step_wall_direction || input.jump_pressed ||
            !state->grounded || player_in_air_flag != s_step_wall_in_air) {
            smw_falcon_clear_step_wall_latch();
        } else {
            input.stick_x = 0.0f;
        }
    }
    memset(&s_last_move, 0, sizeof(s_last_move));
    if (!snes_foreign_tick(snes_frame_counter, &input, &s_last_move)) {
        smw_falcon_reset_dive_iframes();
        return;
    }
    /* ADAPTATION: Smash's capture immunity does not protect against SMW's
     * overlapping side-damage path. Up-B therefore owns native no-hurt from
     * startup through Catch/Throw plus eight bounded recovery frames. */
    smw_falcon_advance_dive_iframes(s_last_move.state);
    smw_falcon_combat_ledger_update(&s_combat_ledger, s_last_move.state,
                                    s_last_move.attack.active);
    smw_falcon_note_wall_safety_context(state);
    /* An opposite-facing first press starts a source Turn, not a completed
     * SMW D-pad tap. Suppress it until its eventual neutral release. */
    if (state->state == FL_TURN || state->state == FL_TURN_RUN)
        s_dash_ignore_until_release = 1;
    smw_falcon_audio_play_events(&s_last_move.audio);
    smw_falcon_adapt_water_motion(&s_last_move);

    if (s_step_wall_latched) {
        smw_falcon_restore_step_wall_latch();
        s_x_before = s_step_wall_x;
        s_y_before = s_step_wall_y;
        s_sub_x_before = s_step_wall_sub_x;
        s_sub_y_before = s_step_wall_sub_y;
        s_in_air_before = s_step_wall_in_air;
        s_pending = 1;
    } else {
        s_x_before = player_xpos;
        s_y_before = player_ypos;
        s_sub_x_before = player_sub_xpos;
        s_sub_y_before = player_sub_ypos;
        s_in_air_before = player_in_air_flag;
        s_pending = 1;
        player_xspeed = clamp_speed(s_last_move.requested_dx, 0);
        player_yspeed = clamp_speed(s_last_move.requested_dy, 1);
        player_sub_xspeed = player_sub_yspeed = 0;
        player_facing_direction = state->facing >= 0.0f;
    }
    if ((s_last_move.force_airborne ||
         state->jump_phase == FOREIGN_JUMP_LAUNCH) &&
        !s_force_airborne_pending) {
        s_force_airborne_pending = 1;
        s_force_airborne_frames = 0;
        departure_started = 1;
    }
    /* The controller's source delta was clamped just above. Reassert the
     * already-authorized native departure after that clamp; timeout advances
     * only at the pre-tick presentation, never twice in one guest frame. */
    if (s_force_airborne_pending)
        smw_falcon_hold_departure_edge(departure_started);

    /* $00:DC2D is intentionally velocity/collision ownership only. */
}

void SmwFalconBeforeCrushCheck(struct CpuState *cpu)
{
    const ForeignState *state;
    ForeignCollisionResult wall;
    int grounded_kick;
    uint8_t side_bits;
    uint8_t expected_side;
    (void)cpu;

    /* The BLOCK_PATCH in HandlePlayerLevelCollision_M1X1 reaches the inlined
     * SMWDisX $00:E9FB block before it sends $77&$1C==$1C to $00:EA08, which calls
     * DamagePlayer_KillAndDisableButtons.  That exact combination means the
     * movement reached the vertical face of a one-block step while grounded;
     * it is not ordinary head contact. Falcon's high-speed Dash/Run and
     * Ground SpecialLw can reach that branch before the later
     * CD36 seam. Restore the DC2D snapshot and let the original routine take
     * its normal non-crush path, so the step behaves as a solid wall rather
     * than leaving Falcon embedded or granting broad damage immunity. */
    if (!s_pending || !snes_foreign_active() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        !smw_falcon_playable() ||
        /* Native $00:E9FB is side-neutral: $1D is right+$1C and $1E is
         * left+$1C.  Accept exactly one forward wall bit, never a two-sided
         * or mismatched moving-ceiling crush. */
        (player_blocked_flags & 0x1Cu) != 0x1Cu ||
        s_in_air_before != 0 ||
        (int16_t)(player_ypos - s_y_before) < -1 ||
        (int16_t)(player_ypos - s_y_before) > 1) return;
    state = snes_foreign_state();
    grounded_kick = state != NULL && state->grounded &&
        state->state == FL_FALCON_KICK_GROUND;
    {
        const int motion_direction = s_last_move.requested_dx > 0.0 ? 1 :
                                     s_last_move.requested_dx < 0.0 ? -1 : 0;
        if (state == NULL || !state->grounded || motion_direction == 0 ||
            (!grounded_kick &&
             !smw_falcon_ground_run_wall_state(state->state)))
            return;
        side_bits = (uint8_t)(player_blocked_flags & 0x03u);
        expected_side = (uint8_t)(motion_direction > 0 ? 0x01u : 0x02u);
    }
    if (side_bits != expected_side)
        return;

    /* Grounded SpecialLw is a one-shot correction for every kick frame.  The
     * user-facing contract is stronger than Smash's narrow rebound window:
     * a wall-facing Falcon Kick must stop cleanly, never enter SMW's crush
     * death/OOB path. */
    if (grounded_kick) {
        player_xpos = s_x_before;
        player_ypos = s_y_before;
        player_sub_xpos = s_sub_x_before;
        player_sub_ypos = s_sub_y_before;
        player_in_air_flag = s_in_air_before;
        player_sub_xspeed = player_sub_yspeed = 0;
        player_xspeed = player_yspeed = 0;
        /* Keep precisely native wall+floor, removing only the false crush
         * ceiling bits ($1D -> $05) before the original kill branch. */
        player_blocked_flags = (uint8_t)((player_blocked_flags & 0x03u) | 0x04u);
        /* HandlePlayerLevelCollision can return nonlocally from this native
         * crush branch, bypassing the caller's inline $CD36 callback.  Do
         * not leave this source Kick pending for the next DC2D tick: resolve
         * the already accepted wall directly to the user-approved grounded
         * stop, with no host position change and no force-airborne handoff.
         * If an ordinary CD36 path does run, s_pending is clear and it cannot
         * resolve the same wall a second time. */
        memset(&wall, 0, sizeof(wall));
        wall.grounded = 1;
        wall.hit_floor = 1;
        wall.hit_wall = 1;
        snes_foreign_resolve(&wall);
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
        s_pending = 0;
        return;
    }

    /* Dash/Run needs a held-direction latch to remain pressed against the
     * step. Its existing behavior is deliberately unchanged. */
    /* $77 bit $04 is floor; retain it with the observed side wall bit while
     * clearing only $08/$10 ceiling/crush ($1D -> $05, $1E -> $06). */
    smw_falcon_install_step_wall_latch(player_blocked_flags);
}

/* A full player collision can return nonlocally before reaching inline
 * $00:CD36.  The normal-sprite pass is nevertheless guaranteed afterwards;
 * use its first $01:80D2 entry as the durable combat consequence seam. The
 * generated body reaches it in M1X1 with DB=$01 after bank $01's PHK/PLB
 * prologue (D remains zero). $00 and $01 are proven WRAM mirrors; the native
 * transaction temporarily enters DB=$02 and restores the caller's exact DB. */
static void smw_falcon_apply_combat_once(CpuState *cpu,
                                         ForeignCollisionResult *collision)
{
    ForeignCollisionResult ignored;
    ForeignCollisionResult *out = collision != NULL ? collision : &ignored;
    const ForeignState *state;
    int is_dive_throw_release;
    int contacts;

    state = snes_foreign_state();
    is_dive_throw_release = state != NULL &&
                            state->state == FL_FALCON_DIVE_THROW &&
                            state->state_frame == 0;
    if (cpu == NULL || !snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        (!s_last_move.attack.active && !is_dive_throw_release) ||
        s_combat_apply_frame == snes_frame_counter || cpu->m_flag != 1 ||
        cpu->x_flag != 1 || (cpu->DB != 0 && cpu->DB != 1) || cpu->D != 0)
        return;
    s_combat_apply_frame = snes_frame_counter;
    memset(&ignored, 0, sizeof(ignored));
    if (is_dive_throw_release) {
        /* BattleShip keeps catch_gobj through Catch and releases it at
         * ftCaptainSpecialHiThrowSetStatus: Throw frame zero. */
        contacts = smw_falcon_combat_release_dive(cpu, &s_combat_ledger, out);
    } else {
        contacts = smw_falcon_combat_apply(cpu, &s_last_move.attack,
                                           state != NULL ? state->facing : 1.0f,
                                           &s_combat_ledger, out);
    }
    if (contacts != 0 && smw_falcon_impact_guard_state(s_last_move.state))
        s_impact_contact_slots |= s_combat_ledger.new_hit_slots;
    if (contacts != 0 &&
        (s_last_move.attack.flags & FOREIGN_ATTACK_CONTACT_ONLY) != 0)
        s_dive_catch_slots |= s_combat_ledger.new_hit_slots;
}

void SmwFalconOnPlayerBlockCode(struct CpuState *cpu)
{
    const ForeignState *state;
    if (cpu == NULL || !snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        !s_last_move.attack.active ||
        (s_last_move.attack.flags & FOREIGN_ATTACK_BREAK_BLOCKS) == 0 ||
        s_block_apply_frame == snes_frame_counter || cpu->m_flag != 1 ||
        cpu->x_flag != 1 || (cpu->DB != 0 && cpu->DB != 1) || cpu->D != 0)
        return;
    state = snes_foreign_state();
    s_block_apply_frame = snes_frame_counter;
    if (smw_falcon_combat_apply_blocks_only(
            cpu, &s_last_move.attack, state != NULL ? state->facing : 1.0f,
            &s_combat_ledger)) {
        /* Falcon's authored sweep owns this block contact.  Prevent the
         * native single-tile Mario block route from immediately repeating the
         * underfoot/support break or running content/bounce behavior. */
        cpu->ram[0x0004] = 0;
        cpu->ram[0x009C] = 0;
        cpu->ram[0x1693] = 0;
    }
}

void SmwFalconAfterPhysics(struct CpuState *cpu)
{
    ForeignCollisionResult hit;
    const ForeignState *state;
    const int dx = (int)(int16_t)(player_xpos - s_x_before);
    const int dy = (int)(int16_t)(player_ypos - s_y_before);
    uint8_t side_bits;
    uint8_t expected_side;
    if (!s_pending || snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
    {
        smw_falcon_clear_carry_bridge();
        return;
    }
    memset(&hit, 0, sizeof(hit));
    hit.actual_dx = (double)dx * SMW_TO_FALCON;
    hit.actual_dy = -(double)dy * SMW_TO_FALCON; /* SMW down -> Falcon up */
    hit.grounded = s_force_airborne_pending ? 0 : player_in_air_flag == 0;
    hit.hit_floor = hit.grounded && dy >= 0;
    hit.hit_ceiling = (player_blocked_flags & 0x08) != 0;
    hit.hit_wall = (player_blocked_flags & 0x03) != 0;
    /* SMW has accepted this departure only when its own collision result is
     * airborne after a real upward whole-pixel integration.  A one-pixel
     * request that collision immediately re-grounds deliberately remains
     * pending; the next DC2D seam presents the same native airborne state. */
    if (s_force_airborne_pending && dy < 0 && player_in_air_flag != 0 &&
        !hit.hit_ceiling) {
        s_force_airborne_pending = 0;
        s_force_airborne_frames = 0;
    }
    state = snes_foreign_state();
    side_bits = (uint8_t)(player_blocked_flags & 0x03u);
    expected_side = state != NULL && state->facing < 0.0f ? 0x02u : 0x01u;
    if (state != NULL && hit.grounded && hit.hit_wall && s_in_air_before == 0 &&
        smw_falcon_ground_run_wall_state(state->state) &&
        s_last_input_direction != 0 &&
        s_last_input_direction == (state->facing >= 0.0f ? 1 : -1) &&
        side_bits == expected_side) {
        smw_falcon_install_step_wall_latch(player_blocked_flags);
        memset(&hit, 0, sizeof(hit));
        hit.grounded = 1;
        hit.hit_floor = 1;
        hit.hit_wall = 1;
    }
    smw_falcon_apply_combat_once(cpu, &hit);
    /* Native normal-sprite collision has not run at $00:CD36 yet. Arm the
     * post-write observer for this one frame so an accepted native stomp can
     * hand its exact $D0/$A8 bounce back to the controller without changing
     * any native contact, damage, score, or sound decision. */
    s_stomp_bounce_armed = 1;
    snes_foreign_resolve(&hit);
    snes_foreign_trace_note_native(player_xpos, player_ypos);
    smw_falcon_restore_wall_safe_ground();
    smw_falcon_remember_wall_safe_ground();
    s_pending = 0;
}

/* Some native player-collision exits do not fall through the caller's inline
 * $00:CD36 AfterPhysics block.  That is normally harmless for a move which
 * stays airborne, but direct SpecialAirLw's source ProcMap ends at the first
 * grounded result.  If CD36 was skipped, the renderer otherwise sees the
 * stale AIR Kick for one grounded frame (including its boot flame), even
 * though native Mario is already standing.  $01:80D2 is guaranteed before
 * normal-sprite interaction and before presentation for that frame, so use
 * it only as a narrowly-gated landing completion seam.
 *
 * Do not generalize this to other pending moves: their source landing maps
 * can have a continuation, and ordinary CD36 has already cleared s_pending.
 */
static void smw_falcon_finish_skipped_direct_air_kick_landing(void)
{
    ForeignCollisionResult hit;
    const ForeignState *state;
    const int dx = (int)(int16_t)(player_xpos - s_x_before);
    const int dy = (int)(int16_t)(player_ypos - s_y_before);

    if (!s_pending || s_force_airborne_pending || player_in_air_flag != 0 ||
        !snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN)
        return;
    state = snes_foreign_state();
    if (state == NULL || state->state != FL_FALCON_KICK_AIR)
        return;

    memset(&hit, 0, sizeof(hit));
    hit.actual_dx = (double)dx * SMW_TO_FALCON;
    hit.actual_dy = -(double)dy * SMW_TO_FALCON;
    hit.grounded = 1;
    hit.hit_floor = 1;
    hit.hit_ceiling = (player_blocked_flags & 0x08) != 0;
    hit.hit_wall = (player_blocked_flags & 0x03) != 0;
    snes_foreign_resolve(&hit);
    /* This is an immediate source WAIT, not a KickLanding/Ground-Kick
     * continuation.  Prevent the stale pre-collision AIR Kick attack from
     * being consumed later in this same normal-sprite/presentation pass. */
    if (snes_foreign_state() != NULL &&
        snes_foreign_state()->state == FL_WAIT) {
        s_last_move.state = FL_WAIT;
        memset(&s_last_move.attack, 0, sizeof(s_last_move.attack));
        smw_falcon_combat_ledger_update(&s_combat_ledger, FL_WAIT, 0);
        s_impact_contact_slots = 0;
    }
    snes_foreign_trace_note_native(player_xpos, player_ypos);
    s_pending = 0;
}

void SmwFalconOnNativeStompBounce(struct CpuState *cpu)
{
    ForeignCollisionResult bounce;
    const uint8_t native_speed = player_yspeed;
    (void)cpu;

    /* SMWDisX $01:AA33 BoostMarioSpeed returns here after a successful native
     * stomp. It writes precisely $D0 (or $A8 while B is held); reject every
     * other call path, including climbing's no-write return. The earlier
     * pad seam normally masks B, but both documented native values remain
     * valid for exact mod-off-compatible semantics. */
    if (!s_stomp_bounce_armed || !snes_foreign_active() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN ||
        (native_speed != 0xD0 && native_speed != 0xA8)) return;

    s_stomp_bounce_armed = 0;
    s_stomp_bounce_consumed = 1;
    /* The current normal-sprite pass can dispatch later custom/multi-hit
     * interaction bodies after the native stomp path returns. $1497 is their
     * own established no-hurt guard. Arm exactly one frame only when it was
     * clear, preserving any pre-existing native invulnerability untouched. */
    if (timer_player_hurt == 0) {
        timer_player_hurt = 1;
        s_stomp_contact_guard = 1;
    }
    memset(&bounce, 0, sizeof(bounce));
    bounce.grounded = 0;
    bounce.has_imposed_vy = 1;
    /* SMW stores downward-positive sixteenth-pixel speed; Falcon uses
     * upward-positive source units. This is the inverse of clamp_speed(). */
    bounce.imposed_vy = -(double)signed8(native_speed) /
                        (FALCON_TO_SMW_PX * SMW_SPEED_PER_PX);
    snes_foreign_resolve(&bounce);
}

void SmwFalconBeforeNormalSprites(struct CpuState *cpu)
{
    unsigned slot = 12u;
    ForeignCollisionResult skipped_cd36_hit;
    smw_falcon_restore_wall_safe_ground();
    smw_falcon_finish_skipped_direct_air_kick_landing();
    /* Some player-collision branches return before the inline $00:CD36
     * callback.  A pending foreign tick still reaches this guaranteed
     * per-sprite seam before native stomp processing, so arm the exact
     * post-$01:AA33 observer here as the fallback. */
    if (s_pending && !s_stomp_bounce_consumed && snes_foreign_active() &&
        smw_falcon_playable() &&
        snes_foreign_ownership() == FOREIGN_OWNERSHIP_FOREIGN)
        s_stomp_bounce_armed = 1;
    /* ProcessNormalSprites calls $01:80D2 once per ordinary sprite. Its first
     * instruction saves the current X; later in the same per-slot body it
     * invokes $01:A7E4 CheckPlayerToNormalSpriteCollision. Thus hook entry
     * has the unmodified current slot and is before that slot's hurt test.
     * Clear only our prior-slot exact value, then guard only a newly accepted
     * Punch/Kick slot. An untouched/behind slot receives $1497==0. */
    if (!s_dive_iframes_active &&
        (s_impact_slot_guard || s_dive_catch_slot_guard) &&
        timer_player_hurt == 1)
        timer_player_hurt = 0;
    s_impact_slot_guard = 0;
    s_dive_catch_slot_guard = 0;
    /* Live Ground Kick collisions can nonlocally leave the player collision
     * body before its inline $00:CD36 AfterPhysics callback.  $01:80D2 is
     * the guaranteed later normal-sprite pass and still precedes this slot's
     * $01:A7E4 side-damage check.  Commit the move once here when CD36 did
     * not run, then arm only the exact accepted Punch/Kick slots below. */
    memset(&skipped_cd36_hit, 0, sizeof(skipped_cd36_hit));
    smw_falcon_apply_combat_once(cpu, &skipped_cd36_hit);
    if (skipped_cd36_hit.attack_connected &&
        (s_last_move.attack.flags & FOREIGN_ATTACK_CONTACT_ONLY) != 0) {
        const ForeignState *resolved;
        /* The durable $01:80D2 fallback can be the only seam reached after a
         * native player-collision nonlocal return.  A Dive latch is not
         * complete until the controller sees this same-frame connection and
         * enters Catch; otherwise the ledger keeps an inert slot forever. */
        snes_foreign_resolve(&skipped_cd36_hit);
        resolved = snes_foreign_state();
        if (resolved != NULL && resolved->state == FL_FALCON_DIVE_CATCH) {
            s_last_move.state = resolved->state;
            memset(&s_last_move.attack, 0, sizeof(s_last_move.attack));
            smw_falcon_combat_ledger_update(&s_combat_ledger,
                                            resolved->state, 0);
        }
    }
    if (!snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        s_impact_contact_slots = 0;
        s_dive_catch_slots = 0;
    } else if (cpu != NULL && cpu->x_flag == 1) {
        slot = cpu->X & 0xffu;
    }
    if (s_dive_iframes_active) {
        /* Unlike Kick's exact connected-slot guard, the requested Up-B
         * protection covers every normal-sprite contact: startup, missed
         * targets, the latched target, Throw, and the short exit grace. */
        if (timer_player_hurt == 0) {
            timer_player_hurt = 1;
            s_dive_iframe_timer_guard = 1;
        }
    } else if (slot < 12u &&
        ((s_impact_contact_slots | s_dive_catch_slots) &
         (uint16_t)(1u << slot)) != 0 &&
        timer_player_hurt == 0) {
        timer_player_hurt = 1;
        if ((s_impact_contact_slots & (uint16_t)(1u << slot)) != 0)
            s_impact_slot_guard = 1;
        else
            s_dive_catch_slot_guard = 1;
    }
    /* $01:80D2 remains the first safe carry-input bridge point: $00:CD36 is
     * earlier than native climb/door/player interactions, so Down must not
     * be emitted there. */
    if (snes_foreign_active() != NULL && misc_game_mode == 0x07) {
        /* GameMode07 retains its own mode value while it JMPs into the title
         * demo's level handler.  The demo's recorded side-hit otherwise
         * enters native death and can strand the title-to-start handoff.
         * SMWDisX's collision check treats any nonzero $1497 as invulnerable;
         * PlayerDraw uses a one-frame value without the flicker branch. */
        timer_player_hurt = 1;
        /* Drop only host bridge state.  Do not call clear_carry_bridge here:
         * GM07's native script deliberately uses Y/Down bits (for example
         * $41 = Y+Right), and its controller bytes must remain untouched. */
        s_foreign_pad.carry_a = 0;
        s_foreign_pad.carry_down = 0;
        s_foreign_pad.carry_valid = 0;
        return;
    }
    if (!snes_foreign_active() || !smw_falcon_playable() ||
        snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) {
        smw_falcon_clear_carry_bridge();
        return;
    }
    smw_falcon_emit_carry_input();
}

void SmwFalconBeforeYoshi(struct CpuState *cpu)
{
    (void)cpu;
    /* Clear restored C2=1 before Yoshi's earlier mounted fast path. Fresh
     * mount contact is skipped at its precise $01:ECE1 block seam below. */
    if (smw_falcon_yoshi_lock_active()) smw_falcon_dismount_yoshi();
}

int SmwFalconSkipYoshiMount(struct CpuState *cpu)
{
    (void)cpu;
    /* $01:ED38 is after native Yoshi movement and a successful contact test,
     * but before the fresh-mount eligibility/latch path. The generated block
     * patch jumps to $01:ED70, whose C2 check returns because
     * SmwFalconBeforeYoshi cleared C2=1. No player velocity/register/scratch
     * state is changed, so later sprite slots retain exact Falcon movement. */
    return smw_falcon_yoshi_lock_active();
}

void SmwFalconOnStateLoaded(void)
{
    /* Native WRAM carries sprite status $0B and the player carry flags in the
     * outer savestate. The bridge is only a one-frame input translation, so
     * never revive a pre-save A/Down decision or deferred Square/Y edge from
     * static host memory.  The grace latch is intentionally not schema state. */
    s_pending = 0;
    s_force_airborne_pending = 0;
    s_force_airborne_frames = 0;
    s_stomp_bounce_armed = 0;
    s_stomp_bounce_consumed = 0;
    s_stomp_contact_guard = 0;
    s_impact_contact_slots = 0;
    s_impact_slot_guard = 0;
    s_dive_catch_slots = 0;
    s_dive_catch_slot_guard = 0;
    /* Host latches belong to the abandoned timeline, while $1497 has just
     * been restored from the save. Forget ownership without writing WRAM. */
    smw_falcon_forget_dive_iframes();
    s_combat_apply_frame = -1;
    s_block_apply_frame = -1;
    smw_falcon_combat_ledger_update(&s_combat_ledger, 0, 0);
    s_last_input_direction = 0;
    s_wall_safety_recent_frames = 0;
    s_wall_safety_hazard_armed = 0;
    s_wall_safety_valid = 0;
    smw_falcon_clear_step_wall_latch();
    smw_falcon_reset_dash_taps();
    s_foreign_pad.valid = 0;
    smw_falcon_clear_carry_bridge();
    memset(&s_last_move, 0, sizeof(s_last_move));
    /* A save can resume a transition before GM14 is reinstated. */
    if (snes_foreign_active() != NULL)
        smw_falcon_dismount_yoshi();
}

const ForeignAttackHitbox *smw_falcon_last_attack(void)
{
    return &s_last_move.attack;
}

const ForeignAudioEvents *smw_falcon_last_audio(void)
{
    return &s_last_move.audio;
}
