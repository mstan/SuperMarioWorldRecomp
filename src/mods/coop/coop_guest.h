#ifndef SMW_COOP_GUEST_H
#define SMW_COOP_GUEST_H

#include "coop_session.h"

enum { COOP_GUEST_BYTES = 0
#define COOP_FIELD(name, address, count) + (count)
#include "coop_player_fields.def"
#undef COOP_FIELD
};
typedef struct CoopGuestPlayer { uint8_t bytes[COOP_GUEST_BYTES]; } CoopGuestPlayer;
typedef struct CoopGuestField { const char *name; uint16_t address, size; } CoopGuestField;

const CoopGuestField *coop_guest_fields(size_t *count);
void coop_guest_capture(CoopGuestPlayer *out, const uint8_t *wram);
void coop_guest_bind(const CoopGuestPlayer *in, uint8_t *wram);
bool coop_guest_peek(const CoopGuestPlayer *in, uint16_t address, uint8_t *value);
void coop_guest_read_player(CoopPlayer *out, const uint8_t *wram);
void coop_guest_place_player(const CoopPlayer *in, uint8_t *wram);
void coop_guest_set_input(uint8_t *wram, uint16_t held, uint16_t pressed);

#endif
