#ifndef SMW_COOP_SESSION_H
#define SMW_COOP_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Product input limits belong in the adapter, not in the simulation. */
typedef uint32_t CoopPlayerId;
typedef uint32_t CoopEntityId;
#define COOP_NO_PLAYER UINT32_MAX
#define COOP_NO_ENTITY UINT32_MAX

typedef enum CoopLifeState {
    COOP_PLAYING, COOP_DYING, COOP_DEATH_BUBBLE, COOP_CATCHUP_BUBBLE
} CoopLifeState;
typedef enum CoopPower { COOP_SMALL, COOP_BIG, COOP_CAPE, COOP_FIRE } CoopPower;
typedef enum CoopOutcome { COOP_CONTINUE, COOP_RETRY, COOP_GAME_OVER, COOP_CLEAR } CoopOutcome;

typedef struct CoopPlayer {
    CoopPlayerId id;
    uint32_t character;
    CoopLifeState life;
    CoopPower power;
    uint32_t reserve;
    int32_t x, y;                 /* actor center in world pixels */
    int32_t half_width, height;
    uint32_t star_ticks, protection_ticks, recovery_ticks, separation_ticks;
    CoopEntityId mount, held_object;
    uint32_t held_input, pressed_input, action_latch;
    bool connected, grounded, swimming, supported_flight;
    bool behind_fence, checkpoint_upgrade;
} CoopPlayer;

typedef struct CoopCamera {
    int32_t x, y, width, height, room_width, room_height;
    int32_t previous_center_x, previous_center_y;
    int32_t travel_x, travel_y;
    CoopPlayerId leader;
    bool initialized, autoscroll;
} CoopCamera;

typedef enum CoopEventKind {
    COOP_EVENT_PICKUP,
    COOP_EVENT_ENEMY_CONTACT,
    COOP_EVENT_ON_OFF,
    COOP_EVENT_CHECKPOINT,
    COOP_EVENT_DAMAGE,
    COOP_EVENT_DEATH,
    COOP_EVENT_DEATH_FINISHED,
    COOP_EVENT_TIMEOUT,
    COOP_EVENT_EXIT
} CoopEventKind;

typedef struct CoopEvent {
    CoopEventKind kind;
    CoopPlayerId player;
    CoopEntityId entity;
    uint32_t value;               /* pickup/exit type, damage; defined by adapter */
    uint32_t flags;
    uint64_t distance_squared;
} CoopEvent;
enum {
    COOP_EVENT_SECRET = 1u,
    COOP_EVENT_STOMP = 2u,
    COOP_EVENT_LETHAL = 4u
};

typedef enum CoopActionKind {
    COOP_ACTION_PICKUP,
    COOP_ACTION_ENEMY_DAMAGE,
    COOP_ACTION_STOMP_BOUNCE,
    COOP_ACTION_ON_OFF,
    COOP_ACTION_CHECKPOINT,
    COOP_ACTION_DAMAGE,
    COOP_ACTION_DROP_RESERVE,
    COOP_ACTION_DROP_OBJECT,
    COOP_ACTION_DETACH_MOUNT,
    COOP_ACTION_DEATH,
    COOP_ACTION_RECOVER,
    COOP_ACTION_EXIT,
    COOP_ACTION_RETRY,
    COOP_ACTION_GAME_OVER
} CoopActionKind;
typedef struct CoopAction {
    CoopActionKind kind;
    CoopPlayerId player;
    CoopEntityId entity;
    uint32_t value;
} CoopAction;

typedef struct CoopSession {
    CoopPlayer *players;
    size_t player_count;
    CoopPlayerId primary;
    CoopCamera camera;
    uint64_t frame;
    uint32_t ticks_per_second;
    uint32_t lives, coins, score, dragon_coins, bonus_stars;
    uint32_t checkpoint, level_ticks;
    CoopOutcome outcome;
    bool advancing, input_blocked, failed, resolved;
    CoopEvent *events;
    size_t event_count, event_capacity;
    CoopAction *actions;
    size_t action_count, action_capacity;
} CoopSession;

/* A safe-placement query must have no side effects. The game adapter checks
 * terrain, hazards, movement capability and (when mounted) full clearance. */
typedef bool (*CoopSafePlacement)(const CoopPlayer *returning,
    const CoopPlayer *anchor, int32_t *x, int32_t *y, void *context);

bool coop_session_init(CoopSession *s, size_t count, uint32_t ticks_per_second);
void coop_session_destroy(CoopSession *s);
CoopPlayer *coop_player(CoopSession *s, CoopPlayerId id);
const CoopPlayer *coop_player_const(const CoopSession *s, CoopPlayerId id);
bool coop_player_precedes(const CoopSession *s,CoopPlayerId a,CoopPlayerId b);
size_t coop_active_count(const CoopSession *s);
bool coop_session_begin_frame(CoopSession *s, bool gameplay_advances);
bool coop_session_event(CoopSession *s, CoopEvent event);
bool coop_session_resolve(CoopSession *s);
void coop_player_input(CoopPlayer *p, uint32_t held, uint32_t action_mask);
bool coop_camera_update(CoopSession *s, int32_t width, int32_t height,
    int32_t room_width, int32_t room_height, bool autoscroll);
/* Native adapters may retain the original camera's smoothing and room rules.
 * First choose a focus, then supply its actual viewport before edge checks. */
bool coop_camera_frame(CoopSession *s,int32_t width,int32_t height,
    int32_t room_width,int32_t room_height,bool autoscroll);
bool coop_camera_check_separation(CoopSession *s,bool resized);
bool coop_session_recover(CoopSession *s, CoopSafePlacement safe, void *context);
void coop_session_restart(CoopSession *s);
CoopPlayerId coop_nearest_player(const CoopSession *s, int32_t x, int32_t y,
    bool match_fence, bool behind_fence);

/* Explicit little-endian records. Decode into a temporary session and replace
 * the destination only after the entire bounded payload has been validated. */
size_t coop_session_save_size(const CoopSession *s);
bool coop_session_save(const CoopSession *s, void *data, size_t capacity);
bool coop_session_load(CoopSession *s, const void *data, size_t size);

#endif
