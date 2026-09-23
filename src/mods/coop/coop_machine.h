#ifndef SMW_COOP_MACHINE_H
#define SMW_COOP_MACHINE_H
#include "coop_guest.h"

/* Native guest images have explicit owners. Input seats and character choice
 * are separate identities; neither indexes the roster implicitly. */
typedef struct CoopActor {
    CoopPlayerId player;
    uint32_t input_seat;
    CoopGuestPlayer guest;
} CoopActor;

typedef struct CoopMachine {
    CoopSession session;
    CoopActor *actors;
    size_t actor_count;
    uint32_t room;
    uint32_t previous_mode;
    bool room_initialized;
} CoopMachine;

bool coop_machine_init(CoopMachine *m, size_t players);
void coop_machine_destroy(CoopMachine *m);
CoopActor *coop_machine_actor(CoopMachine *m, CoopPlayerId player);
size_t coop_machine_save_size(const CoopMachine *m);
bool coop_machine_save(const CoopMachine *m, void *data, size_t capacity);
/* Transactional: failure leaves both policy and native images untouched. */
bool coop_machine_load(CoopMachine *m, const void *data, size_t size);
#endif
