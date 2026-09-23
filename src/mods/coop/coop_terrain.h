#ifndef SMW_COOP_TERRAIN_H
#define SMW_COOP_TERRAIN_H
#include "coop_session.h"

/* Read-only stock Map16 collision data. No speculative guest execution and
 * no write/rollback of a shared world are allowed for placement queries. */
typedef struct CoopTerrain {
    const uint8_t *ram, *rom;
    size_t rom_size;
    const CoopSession *session; /* optional actor-clearance query */
} CoopTerrain;
bool coop_terrain_block(const CoopTerrain *t,unsigned layer,int32_t x,int32_t y,
                        uint16_t *block);
bool coop_terrain_safe(const CoopTerrain *t,const CoopPlayer *returning,
                       const CoopPlayer *anchor,int32_t *x,int32_t *y);
#endif
