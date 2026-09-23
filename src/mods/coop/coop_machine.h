#ifndef SMW_COOP_MACHINE_H
#define SMW_COOP_MACHINE_H
#include "coop_guest.h"

/* The ROM body/cape draw stage emits at most seven 8x8/16x16 pieces. This is
 * an audited graphics-stage bound, independent of the session roster size. */
enum { COOP_BODY_PIECES=7, COOP_PIECE_PIXELS=16*16 };
typedef struct CoopVisualPiece {
    int32_t x,y;
    uint32_t width,height,priority,math,slot;
    uint16_t pixels[COOP_PIECE_PIXELS]; /* RGB15 plus opaque bit */
} CoopVisualPiece;
typedef struct CoopVisual {
    uint32_t count;
    CoopVisualPiece pieces[COOP_BODY_PIECES];
} CoopVisual;

/* Native guest images have explicit owners. Input seats and character choice
 * are separate identities; neither indexes the roster implicitly. */
typedef struct CoopActor {
    CoopPlayerId player;
    uint32_t input_seat;
    CoopGuestPlayer guest;
    CoopVisual pending,visible; /* guest draws, then NMI presents next frame */
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
} CoopEntity;

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
} CoopMachine;

bool coop_machine_init(CoopMachine *m, size_t players);
void coop_machine_destroy(CoopMachine *m);
CoopActor *coop_machine_actor(CoopMachine *m, CoopPlayerId player);
CoopEntity *coop_machine_entity(CoopMachine *m,CoopEntityId id);
CoopEntity *coop_machine_entity_slot(CoopMachine *m,unsigned kind,unsigned slot);
CoopEntity *coop_machine_spawn_entity(CoopMachine *m,unsigned kind,unsigned slot,unsigned type);
void coop_machine_forget_entity(CoopMachine *m,CoopEntityId id);
void coop_machine_focus(CoopMachine *m,int32_t x,int32_t y,uint32_t active);
size_t coop_machine_save_size(const CoopMachine *m);
bool coop_machine_save(const CoopMachine *m, void *data, size_t capacity);
/* Transactional: failure leaves both policy and native images untouched. */
bool coop_machine_load(CoopMachine *m, const void *data, size_t size);
#endif
