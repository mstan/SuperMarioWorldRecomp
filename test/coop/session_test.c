#include "mods/coop/coop_session.h"
#include "mods/coop/coop_guest.h"
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
    free(a);free(b);free(owned);
}

int main(void) {
    roster(2);roster(3);roster(4);roster(17);
    arbitration();priorities();camera_and_recovery();checkpoint_disconnect_input();states();guest_ownership();
    puts("co-op: roster 2/3/4/17, recovery, arbitration, camera, input, checkpoint and atomic state validation passed");
    return 0;
}
