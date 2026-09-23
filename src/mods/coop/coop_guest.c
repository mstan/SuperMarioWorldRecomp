#include "coop_guest.h"
#include <string.h>

static const CoopGuestField fields[] = {
#define COOP_FIELD(name, address, count) {#name, address, count},
#include "coop_player_fields.def"
#undef COOP_FIELD
};

const CoopGuestField *coop_guest_fields(size_t *count) {
    *count = sizeof(fields)/sizeof(*fields);
    return fields;
}

void coop_guest_capture(CoopGuestPlayer *out,const uint8_t *ram) {
    size_t at=0;
    for(size_t i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        memcpy(out->bytes+at,ram+fields[i].address,fields[i].size);
        at+=fields[i].size;
    }
}

void coop_guest_bind(const CoopGuestPlayer *in,uint8_t *ram) {
    size_t at=0;
    for(size_t i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        memcpy(ram+fields[i].address,in->bytes+at,fields[i].size);
        at+=fields[i].size;
    }
}

static unsigned read16(const uint8_t *r,unsigned a) {return r[a]|((unsigned)r[a+1]<<8);}
static void put16(uint8_t *r,unsigned a,unsigned v) {r[a]=(uint8_t)v;r[a+1]=(uint8_t)(v>>8);}

void coop_guest_read_player(CoopPlayer *p,const uint8_t *r) {
    p->power=(CoopPower)r[0x19];
    p->reserve=r[0xdc2];
    p->half_width=8;
    p->height=r[0x73] || r[0x19]==0 ? 16 : 32;
    p->x=(int32_t)read16(r,0x94)+8;
    p->y=(int32_t)read16(r,0x96)+p->height/2;
    p->grounded=(r[0x77]&4)!=0 || r[0x1471]!=0;
    p->swimming=r[0x75]!=0;
    p->supported_flight=r[0x1407]!=0 || r[0x1891]!=0;
    p->behind_fence=r[0x13f9]!=0;
}

void coop_guest_place_player(const CoopPlayer *p,uint8_t *r) {
    put16(r,0x94,(unsigned)(p->x-8));
    put16(r,0x96,(unsigned)(p->y-p->height/2));
    memcpy(r+0xd1,r+0x94,4);
    r[0x19]=(uint8_t)p->power;r[0xdc2]=(uint8_t)p->reserve;
    r[0x7a]=r[0x7b]=r[0x7c]=r[0x7d]=0;
    r[0x13da]=r[0x13dc]=0;
    r[0x71]=r[0x1496]=0;
    r[0x1497]=(uint8_t)(p->protection_ticks>255?255:p->protection_ticks);
    r[0x1407]=r[0x1408]=r[0x140d]=0;
    r[0x72]=p->grounded?0:0x24;
}

void coop_guest_set_input(uint8_t *r,uint16_t held,uint16_t pressed) {
    /* Host input uses the SNES serial bit order (R=bit4 .. B=bit15),
     * matching JOY1L/JOY1H. X and A contribute to the guest Y/B aliases. */
    r[0x15]=(uint8_t)(held>>8);
    r[0x16]=(uint8_t)(pressed>>8);
    r[0x17]=(uint8_t)held&0xc0; /* manual L/R camera is disabled */
    r[0x18]=(uint8_t)pressed&0xc0;
    r[0x15]|=r[0x17];r[0x16]|=r[0x18];
}
