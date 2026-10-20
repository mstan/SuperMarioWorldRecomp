#ifndef SMW_COOP_MACHINE_H
#define SMW_COOP_MACHINE_H
#include "coop_guest.h"

/* The ROM body/cape draw stage emits at most seven 8x8/16x16 pieces. This is
 * an audited graphics-stage bound, independent of the session roster size. */
enum { COOP_BODY_PIECES=7, COOP_PIECE_PIXELS=16*16 };
enum { COOP_MOUNT_PIECES=16, COOP_MOUNT_BYTES=21 };
/* Two is the original allowance per fire-powered actor, not a roster bound.
 * The twelve extended-sprite tables include residue in currently empty slots. */
enum { COOP_FIREBALL_SLOTS=2, COOP_FIREBALL_BYTES=12 };
typedef struct CoopVisualPiece {
    int32_t x,y;
    uint32_t width,height,priority,math,slot;
    uint16_t pixels[COOP_PIECE_PIXELS]; /* RGB15 plus opaque bit */
} CoopVisualPiece;
typedef struct CoopVisual {
    uint32_t count;
    CoopVisualPiece pieces[COOP_BODY_PIECES];
} CoopVisual;
typedef struct CoopMountVisual {
    uint32_t count;
    CoopVisualPiece pieces[COOP_MOUNT_PIECES];
} CoopMountVisual;
typedef struct CoopFireball {
    CoopEntityId id; /* retained through its final latched picture */
    uint8_t guest[COOP_FIREBALL_BYTES];
    uint32_t pending_count,visible_count;
    CoopVisualPiece pending,visible; /* one native fireball/puff OAM piece */
} CoopFireball;

/* Native guest images have explicit owners. Input seats and character choice
 * are separate identities; neither indexes the roster implicitly. */
typedef struct CoopActor {
    CoopPlayerId player;
    uint32_t input_seat;
    CoopGuestPlayer guest;
    CoopVisual pending,visible; /* guest draws, then NMI presents next frame */
    CoopFireball fireballs[COOP_FIREBALL_SLOTS];
    /* Native no-Yoshi/overworld storage has no physical sprite slot. Zero ID
     * means none; a stored identity cannot also exist in the shared world. */
    CoopEntityId stored_mount;
    uint32_t stored_mount_color;
} CoopActor;

/* Stable native identities are separate from reusable guest sprite slots.
 * Slot limits describe the original engine, never the participation limit. */
typedef enum CoopEntityKind { COOP_ENTITY_NORMAL, COOP_ENTITY_EXTENDED } CoopEntityKind;
enum { COOP_ENTITY_HELD=1, COOP_ENTITY_RIDDEN=2, COOP_ENTITY_PROJECTILE=4,
       COOP_ENTITY_ATTACHED=8 };
typedef struct CoopEntity {
    CoopEntityId id;
    uint32_t kind,slot,type;
    CoopPlayerId owner,target;
    uint32_t flags;
    bool mount_valid;
    uint8_t mount[COOP_MOUNT_BYTES];
    CoopMountVisual pending,visible;
} CoopEntity;
typedef struct CoopYoshiSource {
    uint32_t level,layer,x,y,tile,uses;
} CoopYoshiSource;

typedef struct CoopMachine {
    CoopSession session;
    CoopActor *actors;
    size_t actor_count;
    uint32_t room; /* diagnostic SpriteDataPtr; empty rooms may share it */
    uint32_t previous_mode;
    bool room_initialized;
    CoopEntity *entities;
    size_t entity_count,entity_capacity;
    uint32_t next_entity,level;
    /* Camera focus has its own continuity across roster changes. Removing a
     * dying actor must not move a stationary survivor's view. */
    int32_t focus_x,focus_y,focus_center_x,focus_center_y;
    uint32_t focus_count;
    bool focus_initialized,focus_hold;
    bool mounts_initialized;
    bool fireballs_initialized;
    bool entrance_prepared;
    CoopYoshiSource *sources;
    size_t source_count,source_capacity;
} CoopMachine;

bool coop_machine_init(CoopMachine *m, size_t players);
void coop_machine_destroy(CoopMachine *m);
CoopActor *coop_machine_actor(CoopMachine *m, CoopPlayerId player);
CoopEntity *coop_machine_entity(CoopMachine *m,CoopEntityId id);
CoopEntity *coop_machine_entity_slot(CoopMachine *m,unsigned kind,unsigned slot);
CoopEntity *coop_machine_spawn_entity(CoopMachine *m,unsigned kind,unsigned slot,unsigned type);
void coop_machine_forget_entity(CoopMachine *m,CoopEntityId id);
void coop_machine_focus(CoopMachine *m,int32_t x,int32_t y,uint32_t active);
void coop_machine_refit_view(CoopMachine *m);
void coop_mount_capture(CoopEntity *e,const uint8_t *ram);
void coop_mount_bind(const CoopEntity *e,uint8_t *ram);
void coop_fireball_capture(CoopFireball *f,const uint8_t *ram,unsigned slot);
void coop_fireball_bind(const CoopFireball *f,uint8_t *ram,unsigned slot);
CoopYoshiSource *coop_yoshi_source(CoopMachine *m,unsigned level,unsigned layer,
                                 unsigned x,unsigned y,unsigned tile);
size_t coop_machine_save_size(const CoopMachine *m);
bool coop_machine_save(const CoopMachine *m, void *data, size_t capacity);
/* Transactional: failure leaves both policy and native images untouched. */
bool coop_machine_load(CoopMachine *m, const void *data, size_t size);
/* Cross-check native mouth indices before a snapshot touches the live guest.
 * This validates references/ownership, not all native sprite mechanics. */
bool coop_machine_mouths_valid(const CoopMachine *m, const uint8_t *ram);
/* Adults retain mouth indices; babies reserve food while their native eat
 * timer is above $20. At $20 the original routine consumes it. */
bool coop_mount_mouth_active(const uint8_t *ram,unsigned slot);
#endif
