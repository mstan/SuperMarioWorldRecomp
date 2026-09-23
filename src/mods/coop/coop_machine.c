#include "coop_machine.h"
#include <stdlib.h>
#include <string.h>

enum { HEADER = 64, ACTOR_HEADER = 16, PIECE_BYTES = 28+COOP_PIECE_PIXELS*2,
       ENTITY_HEADER = 16, ENTITY_BYTES = 28, FOCUS_BYTES = 32, TRAILER = 4 };
/* Stock US ROM SHA-256. A native session can never restore into a patched ROM. */
static const uint8_t rom_id[32] = {
    0x08,0x38,0xe5,0x31,0xfe,0x22,0xc0,0x77,0x52,0x8f,0xeb,0xe1,0x4c,0xb3,0xff,0x7c,
    0x49,0x2f,0x1f,0x5f,0xa8,0xde,0x35,0x41,0x92,0xbd,0xff,0x71,0x37,0xc2,0x7f,0x5b
};
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t c=UINT32_MAX;
    while(n--) {c^=*p++;for(unsigned k=0;k<8;++k)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
    return ~c;
}
static uint32_t layout_id(void) {
    /* A changed ownership mask must not silently reinterpret an old image. */
    size_t n;const CoopGuestField *f=coop_guest_fields(&n);uint32_t h=2166136261u;
    for(size_t i=0;i<n;++i) {
        h=(h^f[i].address)*16777619u;h=(h^f[i].size)*16777619u;
    }
    return h;
}
bool coop_machine_init(CoopMachine *m,size_t count) {
    if(!m || count>UINT32_MAX || count>SIZE_MAX/sizeof(CoopActor))return false;
    CoopMachine tmp={0};
    if(!coop_session_init(&tmp.session,count,60))return false;
    tmp.actors=calloc(count,sizeof(*tmp.actors));
    if(!tmp.actors){coop_session_destroy(&tmp.session);return false;}
    tmp.actor_count=count;
    tmp.next_entity=1;tmp.level=UINT32_MAX;
    for(size_t i=0;i<count;++i) {
        tmp.actors[i].player=tmp.session.players[i].id;
        tmp.actors[i].input_seat=(uint32_t)i;
    }
    *m=tmp;return true;
}
void coop_machine_destroy(CoopMachine *m) {
    if(!m)return;
    coop_session_destroy(&m->session);free(m->actors);free(m->entities);memset(m,0,sizeof(*m));
}
CoopActor *coop_machine_actor(CoopMachine *m,CoopPlayerId player) {
    for(size_t i=0;i<m->actor_count;++i)if(m->actors[i].player==player)return &m->actors[i];
    return NULL;
}
CoopEntity *coop_machine_entity(CoopMachine *m,CoopEntityId id) {
    for(size_t i=0;i<m->entity_count;++i)if(m->entities[i].id==id)return &m->entities[i];
    return NULL;
}
CoopEntity *coop_machine_entity_slot(CoopMachine *m,unsigned kind,unsigned slot) {
    for(size_t i=0;i<m->entity_count;++i)
        if(m->entities[i].kind==kind && m->entities[i].slot==slot)return &m->entities[i];
    return NULL;
}
void coop_machine_forget_entity(CoopMachine *m,CoopEntityId id) {
    for(size_t i=0;i<m->entity_count;++i)if(m->entities[i].id==id) {
        for(size_t j=0;j<m->session.player_count;++j) {
            CoopPlayer *p=&m->session.players[j];
            if(p->held_object==id)p->held_object=COOP_NO_ENTITY;
            if(p->mount==id)p->mount=COOP_NO_ENTITY;
        }
        m->entities[i]=m->entities[--m->entity_count];return;
    }
}
static bool entity_slot_valid(unsigned kind,unsigned slot) {
    return (kind==COOP_ENTITY_NORMAL && slot<12) ||
           (kind==COOP_ENTITY_EXTENDED && slot<10);
}
CoopEntity *coop_machine_spawn_entity(CoopMachine *m,unsigned kind,unsigned slot,unsigned type) {
    if(!entity_slot_valid(kind,slot) || type>255 || m->next_entity==COOP_NO_ENTITY)return NULL;
    if(m->entity_count==m->entity_capacity) {
        size_t count=m->entity_capacity?m->entity_capacity*2:16;
        CoopEntity *p=realloc(m->entities,count*sizeof(*p));if(!p)return NULL;
        m->entities=p;m->entity_capacity=count;
    }
    CoopEntity *old=coop_machine_entity_slot(m,kind,slot);
    if(old)coop_machine_forget_entity(m,old->id);
    CoopEntity *e=&m->entities[m->entity_count++];
    *e=(CoopEntity){m->next_entity++,kind,slot,type,COOP_NO_PLAYER,COOP_NO_PLAYER,0};
    return e;
}
static int32_t focus_step(int64_t value) {return value < -4 ? -4 : value > 4 ? 4 : (int32_t)value;}
void coop_machine_focus(CoopMachine *m,int32_t x,int32_t y,uint32_t active) {
    if(!active)return;
    if(!m->focus_initialized) {
        m->focus_x=x;m->focus_y=y;m->focus_initialized=true;
    } else {
        if(active!=m->focus_count)m->focus_hold=active<m->focus_count;
        int32_t dx=0,dy=0;
        if(active==m->focus_count) {
            dx=focus_step((int64_t)x-m->focus_center_x);
            dy=focus_step((int64_t)y-m->focus_center_y);
        }
        if(!m->focus_hold) {
            dx=focus_step((int64_t)x-m->focus_x);
            dy=focus_step((int64_t)y-m->focus_y);
        }
        m->focus_x+=dx;m->focus_y+=dy;
    }
    m->focus_count=active;m->focus_center_x=x;m->focus_center_y=y;
}
size_t coop_machine_save_size(const CoopMachine *m) {
    if(!m || m->actor_count!=m->session.player_count || m->actor_count>UINT32_MAX)return 0;
    size_t core=coop_session_save_size(&m->session);
    if(!core || core>UINT32_MAX || core>SIZE_MAX-HEADER-TRAILER ||
        m->actor_count>(SIZE_MAX-HEADER-TRAILER-core)/(ACTOR_HEADER+COOP_GUEST_BYTES))return 0;
    size_t n=HEADER+core+m->actor_count*(ACTOR_HEADER+COOP_GUEST_BYTES)+TRAILER;
    for(size_t i=0;i<m->actor_count;++i) {
        const CoopActor *a=&m->actors[i];
        if(a->pending.count>COOP_BODY_PIECES || a->visible.count>COOP_BODY_PIECES)return 0;
        size_t bytes=(a->pending.count+a->visible.count)*PIECE_BYTES;
        if(bytes>SIZE_MAX-n)return 0;
        n+=bytes;
    }
    if(m->entity_count>UINT32_MAX || n>SIZE_MAX-ENTITY_HEADER ||
       m->entity_count>(SIZE_MAX-n-ENTITY_HEADER)/ENTITY_BYTES)return 0;
    n+=ENTITY_HEADER+m->entity_count*ENTITY_BYTES;
    if(n>SIZE_MAX-FOCUS_BYTES)return 0;
    n+=FOCUS_BYTES;
    return n;
}
static bool entities_valid(const CoopMachine *m) {
    if(m->entity_count>22 || !m->next_entity || (m->level!=UINT32_MAX && m->level>=512))return false;
    for(size_t i=0;i<m->entity_count;++i) {
        const CoopEntity *e=&m->entities[i];
        if(!e->id || e->id>=m->next_entity || !entity_slot_valid(e->kind,e->slot) ||
           e->type>255 || (e->flags&~15u) ||
           (e->owner!=COOP_NO_PLAYER && !coop_player_const(&m->session,e->owner)) ||
           (e->target!=COOP_NO_PLAYER && !coop_player_const(&m->session,e->target)) ||
           ((e->owner==COOP_NO_PLAYER)!=(e->flags==0)))return false;
        if(e->flags && (e->flags&(e->flags-1u)))return false;
        if((e->flags&(COOP_ENTITY_HELD|COOP_ENTITY_RIDDEN|COOP_ENTITY_ATTACHED)) &&
           e->kind!=COOP_ENTITY_NORMAL)return false;
        const CoopPlayer *p=coop_player_const(&m->session,e->owner);
        if((e->flags==COOP_ENTITY_HELD && p->held_object!=e->id) ||
           (e->flags==COOP_ENTITY_RIDDEN && p->mount!=e->id))return false;
        for(size_t j=0;j<i;++j)
            if(e->id==m->entities[j].id ||
               (e->kind==m->entities[j].kind && e->slot==m->entities[j].slot))return false;
    }
    for(size_t i=0;i<m->session.player_count;++i) {
        const CoopPlayer *p=&m->session.players[i];
        bool held=p->held_object==COOP_NO_ENTITY,mount=p->mount==COOP_NO_ENTITY;
        for(size_t j=0;j<m->entity_count;++j) {
            const CoopEntity *e=&m->entities[j];
            if(e->owner!=p->id)continue;
            held|=e->id==p->held_object && e->flags==COOP_ENTITY_HELD;
            mount|=e->id==p->mount && e->flags==COOP_ENTITY_RIDDEN;
        }
        if(!held || !mount)return false;
    }
    return true;
}
static bool piece_valid(const CoopVisualPiece *v) {
    return (v->width==8 || v->width==16) && v->height==v->width &&
           v->priority<=3 && v->math<=1 && v->slot<128;
}
static void save_piece(uint8_t *p,const CoopVisualPiece *v) {
    put32(p,(uint32_t)v->x);put32(p+4,(uint32_t)v->y);
    put32(p+8,v->width);put32(p+12,v->height);put32(p+16,v->priority);
    put32(p+20,v->math);put32(p+24,v->slot);
    for(size_t i=0;i<COOP_PIECE_PIXELS;++i) {
        p[28+i*2]=(uint8_t)v->pixels[i];p[29+i*2]=(uint8_t)(v->pixels[i]>>8);
    }
}
static bool load_piece(CoopVisualPiece *v,const uint8_t *p) {
    v->x=(int32_t)u32(p);v->y=(int32_t)u32(p+4);
    v->width=u32(p+8);v->height=u32(p+12);v->priority=u32(p+16);
    v->math=u32(p+20);v->slot=u32(p+24);
    if(!piece_valid(v))return false;
    for(size_t i=0;i<COOP_PIECE_PIXELS;++i)v->pixels[i]=(uint16_t)(p[28+i*2]|(p[29+i*2]<<8));
    return true;
}
bool coop_machine_save(const CoopMachine *m,void *data,size_t capacity) {
    size_t n=coop_machine_save_size(m);if(!data || !n || capacity<n || !entities_valid(m))return false;
    uint8_t *p=data;memset(p,0,HEADER);
    memcpy(p,"CNR1",4);put32(p+4,4);memcpy(p+8,rom_id,32);
    size_t core=coop_session_save_size(&m->session);
    put32(p+40,(uint32_t)core);put32(p+44,(uint32_t)m->actor_count);
    put32(p+48,layout_id());put32(p+52,m->room);put32(p+56,m->previous_mode);
    put32(p+60,m->room_initialized?1:0);
    if(!coop_session_save(&m->session,p+HEADER,core))return false;
    size_t at=HEADER+core;
    for(size_t i=0;i<m->actor_count;++i) {
        put32(p+at,m->actors[i].player);put32(p+at+4,m->actors[i].input_seat);
        put32(p+at+8,m->actors[i].pending.count);put32(p+at+12,m->actors[i].visible.count);
        memcpy(p+at+ACTOR_HEADER,m->actors[i].guest.bytes,COOP_GUEST_BYTES);
        at+=ACTOR_HEADER+COOP_GUEST_BYTES;
        const CoopVisual *visuals[]={&m->actors[i].pending,&m->actors[i].visible};
        for(unsigned phase=0;phase<2;++phase)for(size_t j=0;j<visuals[phase]->count;++j) {
            if(!piece_valid(&visuals[phase]->pieces[j]))return false;
            save_piece(p+at,&visuals[phase]->pieces[j]);at+=PIECE_BYTES;
        }
    }
    memcpy(p+at,"ENT1",4);put32(p+at+4,(uint32_t)m->entity_count);
    put32(p+at+8,m->next_entity);put32(p+at+12,m->level);at+=ENTITY_HEADER;
    for(size_t i=0;i<m->entity_count;++i) {
        const CoopEntity *e=&m->entities[i];
        put32(p+at,e->id);put32(p+at+4,e->kind);put32(p+at+8,e->slot);
        put32(p+at+12,e->type);put32(p+at+16,e->owner);put32(p+at+20,e->target);
        put32(p+at+24,e->flags);at+=ENTITY_BYTES;
    }
    memcpy(p+at,"CAM1",4);
    put32(p+at+4,(uint32_t)m->focus_x);put32(p+at+8,(uint32_t)m->focus_y);
    put32(p+at+12,(uint32_t)m->focus_center_x);put32(p+at+16,(uint32_t)m->focus_center_y);
    put32(p+at+20,m->focus_count);put32(p+at+24,m->focus_initialized);
    put32(p+at+28,m->focus_hold);at+=FOCUS_BYTES;
    put32(p+at,crc(p,at));return true;
}
bool coop_machine_load(CoopMachine *m,const void *data,size_t size) {
    if(!m || !data || size<HEADER+TRAILER)return false;
    const uint8_t *p=data;
    uint32_t version=u32(p+4);
    if(memcmp(p,"CNR1",4)||(version<2 || version>4)||memcmp(p+8,rom_id,32)||u32(p+48)!=layout_id()||
        u32(p+60)>1||u32(p+56)>0x29||u32(p+size-4)!=crc(p,size-4))return false;
    size_t core=u32(p+40),count=u32(p+44);
    if(core>size-HEADER-TRAILER || count>(size-HEADER-TRAILER-core)/(ACTOR_HEADER+COOP_GUEST_BYTES))return false;
    CoopMachine tmp={0};
    if(!coop_session_load(&tmp.session,p+HEADER,core))return false;
    if(tmp.session.player_count!=count || count>SIZE_MAX/sizeof(*tmp.actors))goto fail;
    tmp.actors=calloc(count,sizeof(*tmp.actors));if(!tmp.actors)goto fail;
    tmp.actor_count=count;tmp.room=u32(p+52);tmp.previous_mode=u32(p+56);tmp.room_initialized=u32(p+60)!=0;
    size_t at=HEADER+core;
    for(size_t i=0;i<count;++i) {
        if(size-TRAILER-at<ACTOR_HEADER+COOP_GUEST_BYTES)goto fail;
        CoopActor *a=&tmp.actors[i];a->player=u32(p+at);a->input_seat=u32(p+at+4);
        a->pending.count=u32(p+at+8);a->visible.count=u32(p+at+12);
        if(a->pending.count>COOP_BODY_PIECES || a->visible.count>COOP_BODY_PIECES)goto fail;
        if(!coop_player(&tmp.session,a->player))goto fail;
        for(size_t j=0;j<i;++j)
            if(a->player==tmp.actors[j].player || a->input_seat==tmp.actors[j].input_seat)goto fail;
        memcpy(a->guest.bytes,p+at+ACTOR_HEADER,COOP_GUEST_BYTES);at+=ACTOR_HEADER+COOP_GUEST_BYTES;
        if(version==2) {
            /* Legacy images have no entity ownership. Import only states
             * without carried objects, ridden mounts or attached balloons. */
            const uint16_t owned[]={0x1470,0x148f,0x187a,0x13f3,0x1891};
            for(size_t k=0;k<sizeof(owned)/sizeof(*owned);++k) {
                uint8_t value=0;coop_guest_peek(&a->guest,owned[k],&value);
                if(value)goto fail;
            }
        }
        CoopVisual *visuals[]={&a->pending,&a->visible};
        for(unsigned phase=0;phase<2;++phase)for(size_t j=0;j<visuals[phase]->count;++j) {
            if(size-TRAILER-at<PIECE_BYTES || !load_piece(&visuals[phase]->pieces[j],p+at))goto fail;
            at+=PIECE_BYTES;
        }
    }
    tmp.next_entity=1;tmp.level=UINT32_MAX;
    if(version>=3) {
        if(size-TRAILER-at<ENTITY_HEADER || memcmp(p+at,"ENT1",4))goto fail;
        tmp.entity_count=tmp.entity_capacity=u32(p+at+4);
        tmp.next_entity=u32(p+at+8);tmp.level=u32(p+at+12);at+=ENTITY_HEADER;
        size_t tail=version>=4?FOCUS_BYTES:0;
        if(size-TRAILER-at<tail || tmp.entity_count>22 ||
           tmp.entity_count!=(size-TRAILER-at-tail)/ENTITY_BYTES ||
           (size-TRAILER-at-tail)%ENTITY_BYTES)goto fail;
        if(tmp.entity_count) {
            tmp.entities=calloc(tmp.entity_count,sizeof(*tmp.entities));if(!tmp.entities)goto fail;
        }
        for(size_t i=0;i<tmp.entity_count;++i) {
            tmp.entities[i]=(CoopEntity){u32(p+at),u32(p+at+4),u32(p+at+8),u32(p+at+12),
                u32(p+at+16),u32(p+at+20),u32(p+at+24)};
            at+=ENTITY_BYTES;
        }
    }
    if(version>=4) {
        if(size-TRAILER-at!=FOCUS_BYTES || memcmp(p+at,"CAM1",4) ||
           u32(p+at+20)>count || u32(p+at+24)>1 || u32(p+at+28)>1)goto fail;
        tmp.focus_x=(int32_t)u32(p+at+4);tmp.focus_y=(int32_t)u32(p+at+8);
        tmp.focus_center_x=(int32_t)u32(p+at+12);tmp.focus_center_y=(int32_t)u32(p+at+16);
        tmp.focus_count=u32(p+at+20);tmp.focus_initialized=u32(p+at+24)!=0;
        tmp.focus_hold=u32(p+at+28)!=0;at+=FOCUS_BYTES;
        if(tmp.focus_x<0 || tmp.focus_x>65535 || tmp.focus_y<0 || tmp.focus_y>65535 ||
           tmp.focus_center_x<0 || tmp.focus_center_x>65535 ||
           tmp.focus_center_y<0 || tmp.focus_center_y>65535)goto fail;
    }
    if(at!=size-TRAILER || !entities_valid(&tmp))goto fail;
    coop_machine_destroy(m);*m=tmp;return true;
fail:
    coop_machine_destroy(&tmp);return false;
}
