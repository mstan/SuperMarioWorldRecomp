#include "mods/coop/coop_session.h"
#include "mods/coop/coop_guest.h"
#include "mods/coop/coop_machine.h"
#include "mods/coop/coop_terrain.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void event(CoopSession *s, CoopEventKind k, uint32_t p, uint32_t entity,
                  uint32_t value, uint32_t flags, uint64_t d) {
    assert(coop_session_event(s,(CoopEvent){k,p,entity,value,flags,d}));
}
static size_t actions(const CoopSession *s, CoopActionKind k) {
    size_t n=0; for(size_t i=0;i<s->action_count;++i) n += s->actions[i].kind==k; return n;
}
static bool safe(const CoopPlayer *p,const CoopPlayer *a,int32_t *x,int32_t *y,void *ctx) {
    (void)p; (void)ctx;
    if(!a->grounded && !a->swimming && !a->supported_flight) return false;
    *x=a->x; *y=a->y; return true;
}

static void roster(size_t count) {
    CoopSession s={0}; assert(coop_session_init(&s,count,60));
    for(size_t i=0;i<count;++i) {
        s.players[i].x=64+(int32_t)i*16; s.players[i].y=100;
        s.players[i].grounded=true;
    }
    s.players[count-1].reserve=4;
    assert(coop_session_begin_frame(&s,true));
    for(size_t i=0;i<count-1;++i) event(&s,COOP_EVENT_DEATH,(uint32_t)i,0,0,0,0);
    assert(coop_session_resolve(&s));
    assert(s.outcome==COOP_CONTINUE && s.lives==5 && coop_active_count(&s)==1);
    assert(coop_nearest_player(&s,0,0,false,false)==count-1);
    assert(coop_session_begin_frame(&s,true));
    for(size_t i=0;i<count-1;++i) event(&s,COOP_EVENT_DEATH_FINISHED,(uint32_t)i,0,0,0,0);
    assert(coop_session_resolve(&s));
    for(unsigned tick=0;tick<179;++tick) {
        assert(coop_session_begin_frame(&s,true));
        assert(coop_session_recover(&s,safe,NULL));
        assert(coop_active_count(&s)==1);
    }
    assert(coop_session_begin_frame(&s,true));
    assert(coop_session_recover(&s,safe,NULL));
    assert(coop_active_count(&s)==count);
    assert(s.players[0].protection_ticks==120);
    assert(coop_session_begin_frame(&s,true));
    for(size_t i=0;i<count;++i) event(&s,COOP_EVENT_DEATH,(uint32_t)i,0,0,0,0);
    assert(coop_session_resolve(&s));
    assert(s.outcome==COOP_RETRY && s.lives==4);
    assert(coop_session_resolve(&s) && s.lives==4);
    coop_session_restart(&s);
    assert(s.players[count-1].reserve==4 && coop_active_count(&s)==count);
    coop_session_destroy(&s);
}

static void arbitration(void) {
    CoopSession a={0},b={0}; assert(coop_session_init(&a,4,60)); assert(coop_session_init(&b,4,60));
    CoopEvent ev[]={
        {COOP_EVENT_PICKUP,0,91,2,0,16}, {COOP_EVENT_PICKUP,3,91,2,0,4},
        {COOP_EVENT_PICKUP,2,91,2,0,4},
        {COOP_EVENT_ENEMY_CONTACT,0,5,1,COOP_EVENT_STOMP,0},
        {COOP_EVENT_ENEMY_CONTACT,1,5,1,COOP_EVENT_STOMP,0},
        {COOP_EVENT_ON_OFF,0,4,0,0,0}, {COOP_EVENT_ON_OFF,2,8,0,0,0}
    };
    assert(coop_session_begin_frame(&a,true)); assert(coop_session_begin_frame(&b,true));
    size_t n=sizeof(ev)/sizeof(*ev);
    for(size_t i=0;i<n;++i) {assert(coop_session_event(&a,ev[i]));assert(coop_session_event(&b,ev[n-1-i]));}
    assert(coop_session_resolve(&a));assert(coop_session_resolve(&b));
    assert(actions(&a,COOP_ACTION_PICKUP)==1 && a.actions[0].player==2);
    assert(actions(&a,COOP_ACTION_ENEMY_DAMAGE)==1 && actions(&a,COOP_ACTION_STOMP_BOUNCE)==2);
    assert(actions(&a,COOP_ACTION_ON_OFF)==1);
    size_t committed=a.action_count;
    assert(coop_session_resolve(&a) && a.action_count==committed);
    assert(!coop_session_event(&a,ev[0]));
    assert(a.action_count==b.action_count && !memcmp(a.actions,b.actions,a.action_count*sizeof(*a.actions)));
    coop_session_destroy(&a);coop_session_destroy(&b);
}

static void priorities(void) {
    CoopSession s={0};assert(coop_session_init(&s,3,60));
    s.players[0].reserve=4;s.players[0].power=COOP_FIRE;
    assert(coop_session_begin_frame(&s,true));
    event(&s,COOP_EVENT_DAMAGE,0,0,0,0,0);
    event(&s,COOP_EVENT_DEATH,0,0,0,0,0);
    event(&s,COOP_EVENT_EXIT,0,12,10,0,0);
    event(&s,COOP_EVENT_EXIT,2,13,20,COOP_EVENT_SECRET,0);
    event(&s,COOP_EVENT_TIMEOUT,COOP_NO_PLAYER,0,0,0,0);
    assert(coop_session_resolve(&s));
    assert(s.outcome==COOP_CLEAR && s.lives==5 && s.players[0].reserve==4);
    assert(actions(&s,COOP_ACTION_DROP_RESERVE)==0);
    CoopAction last=s.actions[s.action_count-1];
    assert(last.kind==COOP_ACTION_EXIT && last.player==2 && (last.value>>8)==20);
    coop_session_destroy(&s);

    /* Native boss/switch scenes can finish while gameplay clocks are frozen.
     * Only their explicit terminal event is accepted; recovery stays frozen. */
    assert(coop_session_init(&s,3,60));
    s.players[1].life=COOP_DEATH_BUBBLE;s.players[1].recovery_ticks=60;
    s.players[2].protection_ticks=100;
    assert(coop_session_begin_frame(&s,false));
    assert(coop_session_resolve(&s) && s.outcome==COOP_CONTINUE);
    assert(!coop_session_event(&s,(CoopEvent){COOP_EVENT_TIMEOUT,COOP_NO_PLAYER,0,0,0,0}));
    event(&s,COOP_EVENT_EXIT,0,COOP_NO_ENTITY,0,0,0);
    assert(coop_session_resolve(&s));
    assert(s.outcome==COOP_CLEAR && s.frame==0 && s.lives==5);
    assert(s.players[1].recovery_ticks==60 && s.players[2].protection_ticks==100);
    assert(actions(&s,COOP_ACTION_EXIT)==1);
    assert(coop_session_resolve(&s) && actions(&s,COOP_ACTION_EXIT)==1);
    assert(!coop_session_event(&s,(CoopEvent){COOP_EVENT_EXIT,0,COOP_NO_ENTITY,0,0,0}));
    coop_session_destroy(&s);
}

static void camera_and_recovery(void) {
    CoopSession s={0};assert(coop_session_init(&s,4,60));
    for(size_t i=0;i<4;++i) {s.players[i].x=1000; s.players[i].y=100;}
    s.players[1].x=10; s.players[1].power=COOP_CAPE;
    s.players[1].mount=99;s.players[1].held_object=77;
    assert(coop_session_begin_frame(&s,true));
    assert(coop_camera_update(&s,256,224,4096,512,false));
    assert(s.players[1].separation_ticks==0);
    for(int i=0;i<60;++i) {
        assert(coop_session_begin_frame(&s,true));
        assert(coop_camera_update(&s,256,224,4096,512,false));
    }
    assert(s.players[1].life==COOP_CATCHUP_BUBBLE && s.players[1].held_object==COOP_NO_ENTITY);
    assert(s.players[1].mount==99 && s.players[1].power==COOP_CAPE);
    assert(actions(&s,COOP_ACTION_DROP_OBJECT)==1);
    assert(coop_session_recover(&s,safe,NULL));
    assert(s.players[1].life==COOP_CATCHUP_BUBBLE);
    s.players[3].swimming=true;
    for(int i=0;i<30;++i)assert(coop_session_begin_frame(&s,true));
    assert(coop_session_recover(&s,safe,NULL));
    assert(s.players[1].life==COOP_PLAYING && s.players[1].protection_ticks==0);
    coop_session_destroy(&s);
}

static void checkpoint_disconnect_input(void) {
    CoopSession s={0};assert(coop_session_init(&s,3,60));
    s.players[1].life=COOP_DEATH_BUBBLE;s.players[1].recovery_ticks=60;
    s.players[1].star_ticks=10;s.players[2].power=COOP_FIRE;
    s.players[2].connected=false;
    assert(coop_session_begin_frame(&s,true));
    assert(!s.advancing && s.players[1].recovery_ticks==60 && s.players[1].star_ticks==10);
    s.players[2].connected=true; assert(coop_session_begin_frame(&s,true));
    assert(s.players[1].recovery_ticks==59 && s.players[1].star_ticks==9);
    event(&s,COOP_EVENT_CHECKPOINT,0,0,7,0,0);assert(coop_session_resolve(&s));
    assert(s.checkpoint==7 && s.players[0].power==COOP_BIG && s.players[1].checkpoint_upgrade);
    assert(s.players[2].power==COOP_FIRE);
    CoopPlayer *p=&s.players[1];
    coop_player_input(p,8,0x80);assert(p->pressed_input==8);
    coop_player_input(p,8,0x80);assert(p->pressed_input==0);
    coop_player_input(p,0x81,0x80);p->life=COOP_PLAYING;
    coop_player_input(p,0x81,0x80);assert(p->held_input==1 && p->pressed_input==0);
    coop_player_input(p,1,0x80);coop_player_input(p,0x81,0x80);assert(p->pressed_input==0x80);
    coop_session_destroy(&s);
}

static void states(void) {
    CoopSession s={0},t={0};assert(coop_session_init(&s,17,60));
    s.players[16].id=1001; s.players[16].x=-500;s.players[16].reserve=4;
    s.players[16].mount=104; s.players[16].life=COOP_CATCHUP_BUBBLE;
    assert(coop_session_begin_frame(&s,true));
    event(&s,COOP_EVENT_PICKUP,1001,9,2,0,4);
    size_t n=coop_session_save_size(&s);uint8_t *data=malloc(n),*again=malloc(n);assert(data&&again);
    assert(coop_session_save(&s,data,n));assert(coop_session_load(&t,data,n));
    assert(t.player_count==17 && t.players[16].id==1001 && t.players[16].x==-500);
    assert(t.players[16].mount==104 && t.event_count==1);
    assert(coop_session_save(&t,again,n));assert(!memcmp(data,again,n));
    CoopPlayer *original=t.players;
    for(size_t i=0;i<n;++i) {data[i]^=0x80;assert(!coop_session_load(&t,data,n));assert(t.players==original);data[i]^=0x80;}
    for(size_t i=0;i<n;++i) assert(!coop_session_load(&t,data,i));
    assert(t.players==original && t.players[16].reserve==4);
    free(data);free(again);coop_session_destroy(&s);coop_session_destroy(&t);
}

static void guest_ownership(void) {
    uint8_t *a=malloc(0x20000),*b=malloc(0x20000),*owned=calloc(0x20000,1);
    assert(a&&b&&owned);
    memset(a,0x53,0x20000);memset(b,0xa7,0x20000);
    CoopGuestPlayer image;
    size_t n;const CoopGuestField *fields=coop_guest_fields(&n);
    for(size_t i=0;i<n;++i) for(unsigned j=0;j<fields[i].size;++j) {
        unsigned address=fields[i].address+j;assert(address<0x2000 && !owned[address]);
        owned[address]=1;
    }
    coop_guest_capture(&image,a);coop_guest_bind(&image,b);
    for(size_t i=0;i<0x20000;++i) assert(b[i]==(owned[i]?0x53:0xa7));
    assert(!owned[0x9d] && !owned[0x148b] && !owned[0x14ad] && !owned[0x1a]);
    coop_guest_set_input(b,0x8180,0x0080);
    assert(b[0x15]==0x81 && b[0x16]==0x80 && b[0x17]==0x80 && b[0x18]==0x80);
    memset(b,0,0x20000);b[0x94]=16;b[0x96]=0x60;b[0x97]=1;
    CoopPlayer p={0};coop_guest_read_player(&p,b);
    assert(p.x==24 && p.y==378 && p.height==12 && p.half_width==6);
    p.y=400;coop_guest_place_player(&p,b);coop_guest_read_player(&p,b);
    assert(p.y==400 && b[0x96]==0x76 && b[0x97]==1);
    b[0x19]=1;coop_guest_read_player(&p,b);assert(p.height==26 && p.y==393);
    b[0x187a]=1;coop_guest_read_player(&p,b);assert(p.height==32 && p.y==406);
    free(a);free(b);free(owned);
}

static void repair_native_crc(uint8_t *p,size_t n) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n-4;++i) {
        crc^=p[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    crc=~crc;for(unsigned i=0;i<4;++i)p[n-4+i]=(uint8_t)(crc>>(8*i));
}
static void native_states(size_t players) {
    CoopMachine a={0},b={0};assert(coop_machine_init(&a,players));
    a.room=123;a.previous_mode=0x14;a.room_initialized=true;
    /* Non-contiguous identities and a different actor storage order must not
     * conflate character, player, and input-seat identities. */
    a.session.players[players-1].id=2000;a.actors[players-1].player=2000;
    a.actors[players-1].input_seat=100;
    CoopActor swap=a.actors[0];a.actors[0]=a.actors[players-1];a.actors[players-1]=swap;
    for(size_t i=0;i<players;++i)memset(a.actors[i].guest.bytes,(int)(i+4),COOP_GUEST_BYTES);
    /* Pending/visible pictures are distinct across the guest's NMI boundary. */
    a.actors[0].pending.count=1;
    CoopVisualPiece *piece=&a.actors[0].pending.pieces[0];
    piece->x=-7;piece->y=211;piece->width=piece->height=16;
    piece->priority=2;piece->slot=68;piece->pixels[15]=0x83e0;
    a.actors[0].visible=a.actors[0].pending;
    a.actors[0].visible.pieces[0].x=87;
    a.level=0x106;
    coop_machine_focus(&a,300,378,(uint32_t)players);
    coop_machine_focus(&a,240,378,(uint32_t)players-1);
    assert(a.focus_x==300 && a.focus_hold);
    coop_machine_focus(&a,243,378,(uint32_t)players-1);
    assert(a.focus_x==303);
    coop_machine_focus(&a,4000,5000,(uint32_t)players);
    assert(a.focus_x==307 && a.focus_y==382 && !a.focus_hold);
    CoopEntity *held=coop_machine_spawn_entity(&a,COOP_ENTITY_NORMAL,3,0x80);assert(held);
    held->owner=2000;held->flags=COOP_ENTITY_HELD;
    coop_player(&a.session,2000)->held_object=held->id;
    CoopEntityId held_id=held->id;
    CoopEntity *mount=coop_machine_spawn_entity(&a,COOP_ENTITY_NORMAL,8,0x35);assert(mount);
    mount->owner=0;mount->flags=COOP_ENTITY_RIDDEN;mount->target=2000;
    coop_player(&a.session,0)->mount=mount->id;
    mount->mount_valid=true;mount->mount[5]=73; /* independent swallow timer */
    mount->pending.count=mount->visible.count=1;
    mount->pending.pieces[0]=mount->visible.pieces[0]=*piece;
    mount->visible.pieces[0].x=111;
    CoopYoshiSource *source=coop_yoshi_source(&a,0x106,0,640,336,0x12d);assert(source);
    source->uses=1;a.mounts_initialized=true;
    size_t n=coop_machine_save_size(&a);uint8_t *data=malloc(n),*again=malloc(n);assert(data&&again);
    assert(coop_machine_save(&a,data,n));assert(coop_machine_load(&b,data,n));
    assert(b.actor_count==players && b.room==123 && b.room_initialized);
    assert(coop_machine_actor(&b,2000)->input_seat==100);
    assert(b.actors[0].pending.pieces[0].x==-7 && b.actors[0].visible.pieces[0].x==87);
    assert(b.actors[0].visible.pieces[0].pixels[15]==0x83e0);
    assert(b.level==0x106 && b.entity_count==2);
    assert(b.focus_x==307 && b.focus_y==382 && b.focus_count==players);
    assert(coop_machine_entity(&b,held_id)->owner==2000);
    assert(coop_machine_entity_slot(&b,COOP_ENTITY_NORMAL,8)->mount[5]==73);
    assert(coop_machine_entity_slot(&b,COOP_ENTITY_NORMAL,8)->visible.pieces[0].x==111);
    assert(b.mounts_initialized && b.source_count==1 && b.sources[0].uses==1);
    assert(coop_machine_save(&b,again,n));assert(!memcmp(data,again,n));
    CoopActor *original=b.actors;
    for(size_t i=0;i<n;++i) {
        data[i]^=0x80;assert(!coop_machine_load(&b,data,n));assert(b.actors==original);data[i]^=0x80;
    }
    for(size_t i=0;i<n;++i)assert(!coop_machine_load(&b,data,i));
    assert(b.actors==original);
    /* Reusable ROM slots cannot transfer an old owner's identity. */
    CoopEntity *replacement=coop_machine_spawn_entity(&b,COOP_ENTITY_NORMAL,3,0x3e);
    assert(replacement && replacement->id!=held_id && replacement->owner==COOP_NO_PLAYER);
    assert(coop_player(&b.session,2000)->held_object==COOP_NO_ENTITY);
    assert(!coop_machine_entity(&b,held_id) && b.entity_count==2);
    assert(coop_machine_entity_slot(&b,COOP_ENTITY_NORMAL,8)->target==2000);
    assert(!coop_machine_spawn_entity(&b,COOP_ENTITY_NORMAL,12,1));
    assert(!coop_machine_spawn_entity(&b,COOP_ENTITY_EXTENDED,10,1));
    /* These are structurally well-formed blobs with correct checksums. */
    size_t visual=64+coop_session_save_size(&a.session)+16+COOP_GUEST_BYTES;
    data[visual+8]=17;repair_native_crc(data,n); /* unsupported piece width */
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    memcpy(data,again,n);
    size_t actor=64+coop_session_save_size(&a.session);
    data[actor+8]=COOP_BODY_PIECES+1;repair_native_crc(data,n);
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    memcpy(data,again,n);
    size_t entities=actor+players*(16+COOP_GUEST_BYTES)+2*540+16;
    data[entities+28]=data[entities];repair_native_crc(data,n); /* duplicate identity */
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    memcpy(data,again,n);
    data[entities+16]=0;data[entities+17]=0;repair_native_crc(data,n); /* incorrect holder */
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    memcpy(data,again,n);
    size_t mounts=entities+2*28+32;
    data[mounts+16+4]=COOP_MOUNT_PIECES+1;repair_native_crc(data,n);
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    memcpy(data,again,n);
    data[n-8]=(uint8_t)(players+1);repair_native_crc(data,n); /* excessive grants */
    assert(!coop_machine_load(&b,data,n) && b.actors==original);
    a.actors[1].player=a.actors[0].player;
    assert(coop_machine_save(&a,data,n));assert(!coop_machine_load(&b,data,n));
    assert(b.actors==original);
    free(data);free(again);coop_machine_destroy(&a);coop_machine_destroy(&b);
}

static void terrain(void) {
    uint8_t *r=calloc(0x20000,1),*rom=calloc(0x80000,1),*before=malloc(0x20000);
    assert(r && rom && before);
    CoopTerrain t={r,rom,0x80000,NULL};
    /* Two horizontal screen pointers, then a separate layer-2 pointer. */
    rom[0xba60-0x8000]=0;rom[0xba9c-0x8000]=0xc8;
    rom[0xba61-0x8000]=0xb0;rom[0xba9d-0x8000]=0xc9;
    rom[0xba70-0x8000]=0;rom[0xbaac-0x8000]=0xe3;
    memset(r+0xc800,0x25,0x360);memset(r+0xe300,0x25,0x1b0);
    r[0x5d]=2;
    for(unsigned x=0;x<16;++x) {r[0xc980+x]=0; r[0x1c980+x]=1;}
    uint16_t block=0;
    assert(coop_terrain_block(&t,0,24,384,&block) && block==0x100);
    assert(!coop_terrain_block(&t,0,-1,384,&block));
    assert(!coop_terrain_block(&t,0,512,384,&block));
    assert(!coop_terrain_block(&t,0,24,432,&block));
    assert(coop_terrain_block(&t,0,256,0,&block) && block==0x25);
    CoopPlayer anchor={.id=0,.life=COOP_PLAYING,.x=24,.y=371,.height=26,.grounded=true};
    CoopPlayer returning={.id=1,.life=COOP_DEATH_BUBBLE,.power=COOP_SMALL,.mount=COOP_NO_ENTITY};
    int32_t x=0,y=0;memcpy(before,r,0x20000);
    assert(coop_terrain_safe(&t,&returning,&anchor,&x,&y) && x==48 && y==378);
    assert(!memcmp(before,r,0x20000));
    memset(r+0xc980,0x2f,16); /* Munchers never become safe due to protection. */
    returning.protection_ticks=120;
    assert(!coop_terrain_safe(&t,&returning,&anchor,&x,&y));
    memset(r+0xc980,0,16);
    r[0x14c8]=8;r[0xe4]=40;r[0xd8]=0x70;r[0x14d4]=1;
    assert(!coop_terrain_safe(&t,&returning,&anchor,&x,&y));r[0x14c8]=0;
    r[0x5b]=0x80;memset(r+0xe470,0x30,16);memset(r+0x1e470,1,16); /* layer-2 obstruction */
    assert(!coop_terrain_safe(&t,&returning,&anchor,&x,&y));r[0x5b]=0;
    memset(r+0xc970,0x2b,16);r[0x14ad]=1;
    assert(coop_terrain_block(&t,0,24,372,&block) && block==0x132);
    assert(!coop_terrain_safe(&t,&returning,&anchor,&x,&y));
    r[0x14ad]=0;assert(coop_terrain_safe(&t,&returning,&anchor,&x,&y));
    t.rom_size=16;assert(!coop_terrain_safe(&t,&returning,&anchor,&x,&y));
    free(before);free(rom);free(r);
}

int main(void) {
    roster(2);roster(3);roster(4);roster(17);
    arbitration();priorities();camera_and_recovery();checkpoint_disconnect_input();states();guest_ownership();
    native_states(2);native_states(3);native_states(4);native_states(17);
    terrain();
    puts("co-op: roster 2/3/4/17, recovery, arbitration, camera, input, checkpoint and atomic state validation passed");
    return 0;
}
