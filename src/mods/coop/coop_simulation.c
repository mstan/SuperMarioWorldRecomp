#include "coop_simulation.h"
#include "coop_runtime.h"
#include "coop_presentation.h"
#include "coop_terrain.h"
#include "smw_renderer.h"
#include "common_rtl.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These are scoped execution guards, never persistent game state. Snapshots
 * are taken at host frame boundaries, where no guest adapter is on the stack. */
static unsigned player_call_depth;
static unsigned sprite_call_depth;
static unsigned camera_call_depth;
static unsigned room_sprite_depth;
static CoopActor *bound_actor;
static bool level_frame;
static bool timer_had_time;
static bool gameplay_stage_started;
static uint8_t end_timer_at_begin;
static CoopPlayerId scene_request=COOP_NO_PLAYER;
/* Room requests are collected and committed within one host frame. The
 * resolved destination lives in ordinary guest loader state at save boundaries. */
static CoopPlayerId room_request=COOP_NO_PLAYER;
static bool goal_query,goal_commit,goal_contact;
static uint8_t goal_height,goal_stars;
static CoopPlayerId goal_actor=COOP_NO_PLAYER;
static CoopEntityId goal_entity=COOP_NO_ENTITY;
static unsigned read16(unsigned at) {return g_ram[at]|(g_ram[at+1]<<8);}
static void put16(unsigned at,unsigned value) {g_ram[at]=(uint8_t)value;g_ram[at+1]=(uint8_t)(value>>8);}

static void restore_registers(CpuState *cpu,const CpuState *caller) {
    /* Bus clocks and the last driven value belong to the shared machine. */
    uint64_t cycles=cpu->cycles,master=cpu->master_cycles;
    uint64_t coprocessor=cpu->coprocessor_master_cycles;
    uint8_t open_bus=cpu->open_bus;
    *cpu=*caller;
    cpu->cycles=cycles;cpu->master_cycles=master;
    cpu->coprocessor_master_cycles=coprocessor;cpu->open_bus=open_bus;
}

static CoopActor *primary_actor(CoopMachine *m) {
    return coop_machine_actor(m,m->session.primary);
}
static void capture(CoopMachine *m,CoopActor *a) {
    coop_guest_capture(&a->guest,g_ram);
    coop_guest_read_player(coop_player(&m->session,a->player),g_ram);
}
static CoopEntity *normal_entity(CoopMachine *m,unsigned slot,bool reset) {
    CoopEntity *e=reset?NULL:coop_machine_entity_slot(m,COOP_ENTITY_NORMAL,slot);
    if(!e)e=coop_machine_spawn_entity(m,COOP_ENTITY_NORMAL,slot,g_ram[0x9e + slot]);
    if(!e)Die("Native co-op could not allocate a sprite identity");
    e->type=g_ram[0x9e + slot];return e;
}
static void release_owner(CoopMachine *m,CoopEntity *e) {
    CoopPlayer *p=coop_player(&m->session,e->owner);
    if(p && p->held_object==e->id)p->held_object=COOP_NO_ENTITY;
    e->owner=COOP_NO_PLAYER;e->flags=0;
}
static void claim_carried(CoopMachine *m,CoopEntity *e,CoopActor *a) {
    if(e->owner!=COOP_NO_PLAYER && e->owner!=a->player)
        Die("Native co-op attempted to transfer an owned carried sprite");
    CoopPlayer *p=coop_player(&m->session,a->player);
    if(e->type==0x7d) {e->owner=p->id;e->flags=COOP_ENTITY_ATTACHED;return;}
    if(p->held_object!=COOP_NO_ENTITY && p->held_object!=e->id)
        Die("Native co-op actor attempted to carry two exclusive objects");
    e->owner=p->id;e->flags=COOP_ENTITY_HELD;p->held_object=e->id;
}
static void retire_released_objects(CoopMachine *m) {
    for(size_t i=0;i<m->entity_count;) {
        CoopEntity *e=&m->entities[i];
        if(e->kind==COOP_ENTITY_NORMAL) {
            if(!g_ram[0x14c8+e->slot]) {coop_machine_forget_entity(m,e->id);continue;}
            if(g_ram[0x14c8+e->slot]!=0x0b &&
               (e->flags&(COOP_ENTITY_HELD|COOP_ENTITY_ATTACHED)))release_owner(m,e);
        }
        ++i;
    }
}
static void guest_jsr(CpuState *cpu,uint32_t entry) {
    unsigned stack=cpu->S;
    cpu_push_jsr_return_frame(cpu);
    if(!interp_bridge_run(cpu,entry) || cpu->S!=stack)
        Die("Native co-op scoped guest routine failed its return contract");
}
static void carry_through_room(CpuState *cpu,CoopMachine *m) {
    /* 02:ABF2 normally keeps one carried sprite and moves it to slot zero.
     * Preserve every owned carried sprite across that same cleanup, then
     * initialize each exactly as the native carried-sprite entrance does.
     * Twelve is the original normal-sprite capacity, not a roster limit. */
    struct Transport {CoopEntity entity;unsigned x,y;uint8_t palette;} carried[12];
    size_t count=0;
    for(unsigned slot=0;slot<12;++slot)if(g_ram[0x14c8+slot]==0x0b) {
        CoopEntity *e=coop_machine_entity_slot(m,COOP_ENTITY_NORMAL,slot);
        if(!e || e->owner==COOP_NO_PLAYER)
            Die("Native co-op room entrance has an unowned carried sprite");
        carried[count++]=(struct Transport){*e,
            g_ram[0xe4+slot]|(g_ram[0x14e0+slot]<<8),
            g_ram[0xd8+slot]|(g_ram[0x14d4+slot]<<8),g_ram[0x15f6+slot]};
        g_ram[0x14c8+slot]=0;
    }
    ++room_sprite_depth;guest_jsr(cpu,0x02abf2);
    CpuState completed=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    while(m->entity_count)coop_machine_forget_entity(m,m->entities[0].id);
    /* Capacity already held these records before cleanup. Stable identities
     * survive transport even though the native slot numbers change. */
    for(size_t i=0;i<count;++i) {
        struct Transport *t=&carried[i];unsigned slot=(unsigned)i;
        g_ram[0x14c8+slot]=0x0b;g_ram[0x9e + slot]=(uint8_t)t->entity.type;
        g_ram[0xe4+slot]=(uint8_t)t->x;g_ram[0x14e0+slot]=(uint8_t)(t->x>>8);
        g_ram[0xd8+slot]=(uint8_t)t->y;g_ram[0x14d4+slot]=(uint8_t)(t->y>>8);
        restore_registers(cpu,&completed);cpu->X=(uint16_t)slot;
        unsigned stack=cpu->S;cpu_push_jsl_return_frame(cpu);
        if(!interp_bridge_run(cpu,0x07f7d2) || cpu->S!=stack)
            Die("Native co-op carried entrance failed its return contract");
        g_ram[0x15f6+slot]=t->palette;
        t->entity.slot=slot;m->entities[m->entity_count++]=t->entity;
        CoopPlayer *p=coop_player(&m->session,t->entity.owner);
        if(t->entity.flags==COOP_ENTITY_HELD)p->held_object=t->entity.id;
    }
    --room_sprite_depth;restore_registers(cpu,&completed);memcpy(g_ram,scratch,sizeof(scratch));
}
static void collect_goal(CpuState *cpu,CoopMachine *m,uint32_t query) {
    if(!m->session.advancing)return;
    CoopActor *original=bound_actor?bound_actor:primary_actor(m);
    CoopActor *previous=bound_actor;capture(m,original);
    CpuState caller=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    goal_query=true;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];
        if(coop_player(&m->session,a->player)->life!=COOP_PLAYING)continue;
        coop_guest_bind(&a->guest,g_ram);bound_actor=a;
        restore_registers(cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
        /* Run the ROM's crossing/contact tests for every candidate. The
         * accepted-contact observer records before shared effects execute. */
        guest_jsr(cpu,query);
    }
    goal_query=false;bound_actor=previous;
    restore_registers(cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
    coop_guest_bind(&original->guest,g_ram);
}
static void record_goal(CpuState *cpu,CoopMachine *m) {
    unsigned slot=cpu->X&0xff;
    CpuState caller=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    /* Goal tape's tweaker D has bit 7 set: this native interaction call is
     * the contact query, returning before default damage/stomp processing. */
    if(slot>=12 || !(g_ram[0x167a+slot]&0x80))
        Die("Native co-op goal query requires the stock custom-contact sprite");
    guest_jsr(cpu,0x01a7e4);
    uint8_t height=(uint8_t)(g_ram[0x1528+slot]-g_ram[0xd8+slot]);
    uint8_t stars=cpu->_flag_C?g_rom[0x3f1aa+(height>>2)]:0;
    if(cpu->_flag_C && (!goal_contact || stars>goal_stars ||
       (stars==goal_stars && coop_player_precedes(&m->session,bound_actor->player,goal_actor)))) {
        goal_contact=true;goal_height=height;goal_stars=stars;
        goal_actor=bound_actor->player;goal_entity=slot;
    }
    if(!coop_session_event(&m->session,(CoopEvent){COOP_EVENT_EXIT,bound_actor->player,
            slot,stars,(g_ram[0x187b+slot]&4)?COOP_EVENT_SECRET:0,0}))
        Die("Native co-op could not record a goal crossing");
    restore_registers(cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
}
static uint32_t follow_victory(CpuState *cpu,CoopMachine *m) {
    CoopActor *primary=primary_actor(m);
    if(!bound_actor || bound_actor==primary)return 0;
    /* C915 owns shared palette fading, score conversion, spotlight, music,
     * and scene changes. Followers use its actor-only motion/pose routines. */
    g_ram[0x15]=g_ram[0x16]=g_ram[0x17]=g_ram[0x18]=0;
    g_ram[0x18c2]=g_ram[0x13de]=g_ram[0x13ed]=0;
    uint8_t peace=0;coop_guest_peek(&primary->guest,0x1492,&peace);
    if(g_ram[0x1b99] && peace)return 0x00ca31;
    if((g_ram[0x5b]&1) || g_ram[0x13c6] || g_ram[0x13d2])return 0x00c96a;
    if(!g_ram[0x1b99]) {
        if(g_ram[0x1493]>=0x28) {g_ram[0x76]=g_ram[0x15]=1;g_ram[0x7b]=5;}
        if(g_ram[0x72])guest_jsr(cpu,0x00d76b);
    } else g_ram[0x15]=1;
    return 0x00cd24;
}
static void initialize_room(CoopMachine *m) {
    retire_released_objects(m);
    if(m->session.outcome==COOP_GAME_OVER) {
        for(size_t i=0;i<m->session.player_count;++i)m->session.players[i].reserve=0;
        coop_session_restart(&m->session);
    } else if(m->session.outcome==COOP_RETRY)coop_session_restart(&m->session);
    else if(m->session.outcome==COOP_CLEAR) {
        m->session.outcome=COOP_CONTINUE;m->session.camera.initialized=false;
        for(size_t i=0;i<m->session.player_count;++i) {
            CoopPlayer *p=&m->session.players[i];
            p->life=COOP_PLAYING;p->recovery_ticks=p->separation_ticks=0;
            p->checkpoint_upgrade=false;
        }
    }
    if(!g_ram[0x141a])
        m->session.checkpoint=(g_ram[0x1ea2+g_ram[0x13bf]]&0x40)?g_ram[0x13bf]+1u:0;
    CoopGuestPlayer entry;coop_guest_capture(&entry,g_ram);
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        coop_guest_bind(&entry,g_ram);
        /* Entry movement/pose belongs to the new room; equipment belongs to
         * the persistent actor. Everyone uses the original entrance point. */
        if(m->room_initialized || a->player!=m->session.primary) {
            g_ram[0x19]=(uint8_t)p->power;g_ram[0xdc2]=(uint8_t)p->reserve;
        }
        g_ram[0x1470]=g_ram[0x148f]=(uint8_t)(p->held_object!=COOP_NO_ENTITY);
        capture(m,a);
        a->pending.count=a->visible.count=0;
    }
    m->room=(uint32_t)g_ram[0xce]|((uint32_t)g_ram[0xcf]<<8)|((uint32_t)g_ram[0xd0]<<16);
    m->room_initialized=true;
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
void SmwCoopSimulationBegin(void) {
    level_frame=gameplay_stage_started=false;room_request=COOP_NO_PLAYER;
    goal_query=goal_commit=goal_contact=false;goal_height=goal_stars=0;
    goal_actor=COOP_NO_PLAYER;goal_entity=COOP_NO_ENTITY;
    scene_request=COOP_NO_PLAYER;
    CoopMachine *m=SmwCoopMachine();if(!m)return;
    unsigned mode=g_ram[0x100];
    if(mode!=0x14) {m->previous_mode=mode;return;}
    if(!m->room_initialized || m->previous_mode!=0x14)initialize_room(m);
    m->previous_mode=mode;level_frame=true;
    end_timer_at_begin=g_ram[0x1493];
    timer_had_time=(g_ram[0xf31]|g_ram[0xf32]|g_ram[0xf33])!=0;
    uint32_t shared_pressed=0;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        uint32_t actions=SNES_PAD_A|SNES_PAD_B|SNES_PAD_X|SNES_PAD_Y|SNES_PAD_SELECT;
        coop_player_input(p,RtlGetPadState((int)a->input_seat),actions);
        shared_pressed|=p->pressed_input;
    }
    if(g_ram[0x1426]) {
        uint32_t menu=shared_pressed&(SNES_PAD_A|SNES_PAD_B|SNES_PAD_X|SNES_PAD_Y|SNES_PAD_START|SNES_PAD_SELECT);
        uint16_t serial=SwapInputBits((uint16_t)menu);
        coop_guest_set_input(g_ram,serial,serial);
    } else if(shared_pressed&SNES_PAD_START)g_ram[0x16]|=0x10;
    /* The original pause handler runs next. Advance timers only when its
     * actual gameplay branch is reached, including the unpause frame. */
    if(!coop_session_begin_frame(&m->session,false))Die("Unable to begin native co-op frame");
    capture(m,primary_actor(m));
}

static void begin_gameplay_stage(CoopMachine *m) {
    if(gameplay_stage_started)return;
    gameplay_stage_started=true;
    bool scene=true;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        uint8_t animation=0;coop_guest_peek(&a->guest,0x71,&animation);
        if(p->life==COOP_PLAYING && animation<5)scene=false;
    }
    bool advances=!scene && !g_ram[0x1426] && !g_ram[0x9d] && !g_ram[0x13fb];
    if(!coop_session_begin_frame(&m->session,advances))Die("Unable to begin native co-op gameplay stage");
}

static void party_pause(CoopMachine *m) {
    if(!(g_ram[0x16]&0x10) || g_ram[0x1493])return;
    bool eligible=false;
    for(size_t i=0;i<m->actor_count;++i) {
        uint8_t animation=0;CoopActor *a=&m->actors[i];
        coop_guest_peek(&a->guest,0x71,&animation);
        eligible|=coop_player(&m->session,a->player)->life==COOP_PLAYING && animation<9;
    }
    if(!eligible)return;
    g_ram[0x13d3]=0x3c;g_ram[0x13d4]^=1;
    g_ram[0x1df9]=g_ram[0x13d4]?0x11:0x12;
}

static void tick_secondary_timers(void) {
    /* The world routine immediately preceding C569 already advanced the
     * primary actor and the shared timers. Only audited actor timer fields
     * are advanced here. Mount/global timers are not included. */
    if(g_ram[0x9d])return;
    for(unsigned a=0x1496;a<=0x14a2;++a)
        if(g_ram[a] && !(a==0x1496 && g_ram[0x71]==9))--g_ram[a];
    for(unsigned a=0x14a4;a<=0x14a6;++a)if(g_ram[a])--g_ram[a];
    if(!(g_ram[0x14]&3)) {
        if(g_ram[0x14a9])--g_ram[0x14a9];
        if(g_ram[0x14aa])--g_ram[0x14aa];
    }
}
static void draw_secondary_players(CpuState *cpu,CoopMachine *m) {
    CpuState caller=*cpu;
    uint8_t scratch[16],oam[0x2a0];
    memcpy(scratch,g_ram,sizeof(scratch));memcpy(oam,g_ram+0x200,sizeof(oam));
    uint8_t turn=g_ram[0xdb3];
    CoopActor *primary=primary_actor(m);capture(m,primary);
    ++player_call_depth;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        if(a==primary)continue;
        a->pending.count=0;
        if(p->life!=COOP_PLAYING && p->life!=COOP_DYING)continue;
        coop_guest_bind(&a->guest,g_ram);
        memcpy(g_ram,scratch,sizeof(scratch));
        for(unsigned slot=0;slot<128;++slot)g_ram[0x201+slot*4]=0xf0;
        g_ram[0xdb3]=(uint8_t)(p->character==1);
        restore_registers(cpu,&caller);
        /* Enter the body/cape stage after Yoshi and shared star music work.
         * Its bank-save/RTL epilogue requires the original one-byte DB frame. */
        cpu_push_jsl_return_frame(cpu);
        cpu_write8(cpu,0,cpu->S,cpu->DB);--cpu->S;cpu->DB=0;cpu->PB=0;
        unsigned star=g_ram[0x1490];
        uint32_t entry=0x00e314;
        if(g_ram[0x149b])entry=0x00e308;
        else if(star) {
            if(g_ram[0x78]!=0xff && !(g_ram[0x14]&3))--g_ram[0x1490];
            entry=star>0x1e?0x00e30c:0x00e308;
            cpu->A=(cpu->A&0xff00)|g_ram[0x13];
        }
        if(!interp_bridge_run(cpu,entry) || cpu->S!=caller.S)
            Die("Native co-op player draw failed its guest return contract");
        SmwCoopCaptureVisual(&a->pending);capture(m,a);
    }
    --player_call_depth;
    restore_registers(cpu,&caller);
    memcpy(g_ram,scratch,sizeof(scratch));memcpy(g_ram+0x200,oam,sizeof(oam));
    g_ram[0xdb3]=turn;coop_guest_bind(&primary->guest,g_ram);
}
static void run_player_routines(CpuState *cpu,CoopMachine *m) {
    CpuState caller=*cpu;
    uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    uint8_t turn=g_ram[0xdb3];
    CoopActor *primary=primary_actor(m);
    /* Primary timers and animation graphics changed since frame begin. */
    if(coop_player(&m->session,primary->player)->life==COOP_DYING) {
        /* C47E normally cannot tick this timer during death, because stock
         * death freezes the world. The co-op death stage owns its cadence. */
        uint8_t timer=0;coop_guest_peek(&primary->guest,0x1496,&timer);
        g_ram[0x1496]=timer;
    }
    capture(m,primary);
    ++player_call_depth;
    for(size_t i=0;i<=m->actor_count;++i) {
        CoopActor *a=i?&m->actors[i-1]:primary;
        if(i && a==primary)continue;
        CoopPlayer *p=coop_player(&m->session,a->player);
        if(p->life!=COOP_PLAYING && p->life!=COOP_DYING)continue;
        coop_guest_bind(&a->guest,g_ram);
        if(a!=primary) {
            memcpy(g_ram+0xd1,g_ram+0x94,4); /* AdvancePlayerPosition */
            tick_secondary_timers();
        }
        /* This selector also indexes score/progression. Character identity
         * is only bound for graphics; gameplay uses the shared party slot. */
        g_ram[0xdb3]=0;
        coop_guest_set_input(g_ram,SwapInputBits((uint16_t)p->held_input),
                             SwapInputBits((uint16_t)p->pressed_input));
        restore_registers(cpu,&caller);
        memcpy(g_ram,scratch,sizeof(scratch));
        cpu_push_jsr_return_frame(cpu);
        bound_actor=a;
        uint8_t end_timer=g_ram[0x1493];
        if(!interp_bridge_run(cpu,0x00c569))Die("Native co-op player routine failed to return");
        if(!end_timer && g_ram[0x1493])scene_request=a->player;
        bound_actor=NULL;
        if(cpu->S!=caller.S)Die("Native co-op player routine unbalanced the guest stack");
        capture(m,a);
        if(g_ram[0x100]!=0x14)break;
    }
    --player_call_depth;
    restore_registers(cpu,&caller);
    memcpy(g_ram,scratch,sizeof(scratch));g_ram[0xdb3]=turn;
    coop_guest_bind(&primary->guest,g_ram);
}
static bool run_normal_sprite(CpuState *cpu,CoopMachine *m) {
    unsigned slot=cpu->X&0xff;if(slot>=12)return false;
    CoopEntity *e=coop_machine_entity_slot(m,COOP_ENTITY_NORMAL,slot);
    if(!g_ram[0x14c8+slot]) {
        if(e)coop_machine_forget_entity(m,e->id);
        return false;
    }
    e=normal_entity(m,slot,false);
    CoopActor *primary=primary_actor(m);capture(m,primary);
    int x=(g_ram[0xe4+slot]|(g_ram[0x14e0+slot]<<8))+8;
    int y=(g_ram[0xd8+slot]|(g_ram[0x14d4+slot]<<8))+8;
    CoopPlayerId target=e->owner!=COOP_NO_PLAYER?e->owner:
        coop_nearest_player(&m->session,x,y,false,false);
    CoopActor *a=coop_machine_actor(m,target);
    if(!a)a=primary;
    unsigned previous_status=g_ram[0x14c8+slot];
    if(previous_status==0x0b && e->owner==COOP_NO_PLAYER)
        Die("Native co-op encountered a carried sprite without an owner");
    CpuState caller=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    coop_guest_bind(&a->guest,g_ram);
    bound_actor=a;
    ++sprite_call_depth;cpu_push_jsr_return_frame(cpu);
    if(!interp_bridge_run(cpu,0x018127) || cpu->S!=caller.S)
        Die("Native co-op sprite routine failed its guest return contract");
    --sprite_call_depth;bound_actor=NULL;capture(m,a);
    e=normal_entity(m,slot,false);
    if(!g_ram[0x14c8+slot])coop_machine_forget_entity(m,e->id);
    else if(g_ram[0x14c8+slot]==0x0b)claim_carried(m,e,a);
    else if(e->flags&(COOP_ENTITY_HELD|COOP_ENTITY_ATTACHED))release_owner(m,e);
    restore_registers(cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
    coop_guest_bind(&primary->guest,g_ram);
    return true;
}
static void prepare_world_contacts(CoopMachine *m) {
    CoopActor *primary=primary_actor(m);capture(m,primary);
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];if(a==primary)continue;
        coop_guest_bind(&a->guest,g_ram);
        /* Per-actor occupancy bookkeeping at CODE_01808C. World Yoshi-slot
         * bookkeeping is separate and requires the mount ownership adapter. */
        g_ram[0x1470]=g_ram[0x148f];g_ram[0x148f]=0;
        g_ram[0x1471]=0;g_ram[0x18c2]=0;
        capture(m,a);
    }
    coop_guest_bind(&primary->guest,g_ram);
}
static void queue_event(CoopMachine *m,CoopEventKind kind,CoopPlayerId player) {
    if(!coop_session_event(&m->session,(CoopEvent){kind,player,COOP_NO_ENTITY,0,0,0}))
        Die("Native co-op could not record a gameplay event");
}
static uint32_t death_hook(CpuState *cpu,CoopMachine *m,uint32_t pc) {
    CoopActor *a=bound_actor?bound_actor:primary_actor(m);
    CoopPlayer *p=coop_player(&m->session,a->player);
    if(pc==0x00f606 || pc==0x00f60a) {
        if(timer_had_time && !(g_ram[0xf31]|g_ram[0xf32]|g_ram[0xf33]) && m->session.advancing) {
            queue_event(m,COOP_EVENT_TIMEOUT,COOP_NO_PLAYER);
        }
        if(p->life==COOP_PLAYING && g_ram[0x71]!=9 && m->session.advancing) {
            queue_event(m,COOP_EVENT_DEATH,a->player);
            /* Keep the ROM death pose/velocity, but music and SpriteLock are
             * team state. F606 supplies the enemy-death Y velocity;
             * direct F60A callers (pits) intentionally retain fall velocity. */
            if(pc==0x00f606)g_ram[0x7d]=0x90;
            g_ram[0x71]=9;g_ram[0x1496]=0x30;g_ram[0x19]=0;
            g_ram[0x140d]=g_ram[0x1407]=g_ram[0x188a]=0;
            g_ram[0x1490]=g_ram[0x1497]=0;
        }
        return 0x00f628;
    }
    if(pc==0x00d0b6) {
        g_ram[0x19]=0;g_ram[0x13e0]=0x3e;
        if(!m->session.advancing)return 0x00d11c;
        if(!(g_ram[0x13]&3) && g_ram[0x1496])--g_ram[0x1496];
        if(!g_ram[0x1496]) {
            queue_event(m,COOP_EVENT_DEATH_FINISHED,a->player);
            return 0x00d11c; /* death-animation RTS, before team effects */
        }
        cpu->A=(cpu->A&0xff00)|g_ram[0x1496];
        return 0x00d108; /* original falling motion and pose direction */
    }
    return 0;
}
static void run_team_camera(CpuState *cpu,CoopMachine *m) {
    CoopActor *primary=primary_actor(m);capture(m,primary);
    CoopActor *focus=NULL;
    int64_t left=INT32_MAX,right=INT32_MIN,top=INT32_MAX,bottom=INT32_MIN;
    bool grounded=false;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        if(p->life!=COOP_PLAYING)continue;
        if(!focus)focus=a;
        if(p->x-p->half_width<left)left=p->x-p->half_width;
        if(p->x+p->half_width>right)right=p->x+p->half_width;
        if(p->y-p->height/2<top)top=p->y-p->height/2;
        if(p->y+p->height/2>bottom)bottom=p->y+p->height/2;
        grounded|=p->grounded;
    }
    if(focus) {
        const CoopPlayer *leader=coop_player_const(&m->session,m->session.camera.leader);
        if(leader && leader->life==COOP_PLAYING) {
            left=right=leader->x;top=bottom=leader->y;
            focus=coop_machine_actor(m,leader->id);
        }
        coop_guest_bind(&focus->guest,g_ram);
        CoopPlayer proxy=*coop_player(&m->session,focus->player);
        proxy.x=(int32_t)((left+right)/2);proxy.y=(int32_t)((top+bottom)/2);
        proxy.grounded=grounded;
        coop_guest_place_player(&proxy,g_ram);
    }
    CpuState caller=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    ++camera_call_depth;cpu_push_jsl_return_frame(cpu);
    if(!interp_bridge_run(cpu,0x00f6db) || cpu->S!=caller.S)
        Die("Native co-op camera failed its guest return contract");
    --camera_call_depth;
    restore_registers(cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
    coop_guest_bind(&primary->guest,g_ram);
}
uint32_t SmwCoopSimulationHook(CpuState *cpu,uint32_t pc) {
    CoopMachine *m=SmwCoopMachine();if(!m)return 0;
    pc&=0x7fffff;
    if(pc==0x05d8b7) {m->level=read16(0x0e);return 0;}
    if(pc==0x02abf2 && !room_sprite_depth) {
        carry_through_room(cpu,m);return 0x02ac5b;
    }
    if(!level_frame)return 0;
    if(pc==0x07f722 && !room_sprite_depth) {
        unsigned slot=cpu->X&0xff;
        if(slot<12) {
            CoopEntity *e=normal_entity(m,slot,true);
            if(g_ram[0x14c8+slot]==0x0b && bound_actor)claim_carried(m,e,bound_actor);
        }
    }
    if(pc==0x01c0c2 && !goal_query) {collect_goal(cpu,m,pc);return 0x01c12c;}
    if(pc==0x01c0e7 && goal_query) {record_goal(cpu,m);return 0x01c12c;}
    if(pc==0x018773 && !goal_query) {collect_goal(cpu,m,pc);return 0x018788;}
    if(pc==0x018778 && goal_query) {
        if(!coop_session_event(&m->session,(CoopEvent){COOP_EVENT_EXIT,
                bound_actor->player,cpu->X&0xff,0,0,0}))
            Die("Native co-op could not record a goal sphere contact");
        return 0x018788;
    }
    if(pc==0x01c107 && goal_commit) {
        cpu->_flag_C=goal_contact;cpu->P=(uint8_t)((cpu->P&~1u)|(goal_contact?1u:0u));
    }
    if(pc==0x07f252 && goal_commit)g_ram[0x1594+(cpu->X&0xff)]=goal_height;
    if(pc==0x00c915)return follow_victory(cpu,m);
    if(pc==0x00a21b) {party_pause(m);return 0x00a242;}
    if(pc==0x00a28a)begin_gameplay_stage(m);
    if(pc==0x00f2cd && m->session.advancing) {
        CoopActor *a=bound_actor?bound_actor:primary_actor(m);
        if(coop_player(&m->session,a->player)->life==COOP_PLAYING &&
           !coop_session_event(&m->session,(CoopEvent){COOP_EVENT_CHECKPOINT,
               a->player,COOP_NO_ENTITY,g_ram[0x13cd]?g_ram[0x13bf]+1u:0,0,0}))
            Die("Native co-op could not record checkpoint contact");
    }
    if(pc==0x00d273) {
        CoopActor *a=bound_actor?bound_actor:primary_actor(m);
        if(coop_player(&m->session,a->player)->life==COOP_PLAYING &&
           (room_request==COOP_NO_PLAYER || coop_player_precedes(&m->session,a->player,room_request)))
            room_request=a->player;
        return 0x00c592; /* defer the original SublevelCount/GameMode commit */
    }
    if(pc==0x00f606 || pc==0x00f60a || pc==0x00d0b6)return death_hook(cpu,m,pc);
    if(pc==0x00e9a1 && g_ram[0x1411]) {
        /* The original E9A1 edge clamp prevents separation grace. Keep real
         * terrain crushing at E9FB and retain the stock autoscroll path. */
        int x=(int16_t)read16(0x94);
        int max=(g_ram[0x5b]&1)?496:(int)g_ram[0x5d]*256-16;
        if(x<0)put16(0x94,0);
        else if(x>max)put16(0x94,(unsigned)max);
        return 0x00e9fb;
    }
    if(player_call_depth)return 0;
    if(pc==0x00f6db && !camera_call_depth) {
        run_team_camera(cpu,m);return 0x00f628; /* original RTL */
    }
    if((pc&0x7fffff)==0x01808c)prepare_world_contacts(m);
    if((pc&0x7fffff)==0x018127 && !sprite_call_depth && run_normal_sprite(cpu,m))
        return 0x018126; /* original bank-1 RTS; world sprite runs once */
    if((pc&0x7fffff)==0x00e2bd)draw_secondary_players(cpu,m);
    if((pc&0x7fffff)==0x00c569) {
        run_player_routines(cpu,m);
        return 0x00c592; /* original RTS, with the original return frame */
    }
    return 0;
}
static bool safe_recovery(const CoopPlayer *p,const CoopPlayer *anchor,int32_t *x,int32_t *y,void *context) {
    return coop_terrain_safe(context,p,anchor,x,y);
}
static void apply_object_drops(CoopMachine *m) {
    CpuState caller=g_cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    uint8_t sprite=g_ram[0x15e9];
    for(size_t i=0;i<m->session.action_count;++i) {
        const CoopAction *action=&m->session.actions[i];
        if(action->kind!=COOP_ACTION_DROP_OBJECT)continue;
        CoopEntity *e=coop_machine_entity(m,action->entity);
        if(!e || e->owner!=action->player || e->flags!=COOP_ENTITY_HELD)continue;
        unsigned slot=e->slot;CoopActor *a=coop_machine_actor(m,action->player);
        coop_guest_bind(&a->guest,g_ram);bound_actor=a;
        if(g_ram[0x14c8+slot]==0x0b) {
            uint8_t input[4];memcpy(input,g_ram+0x15,sizeof(input));
            g_ram[0x15]=4;g_ram[0x16]=g_ram[0x17]=g_ram[0x18]=0;
            g_ram[0x15e9]=(uint8_t)slot;
            restore_registers(&g_cpu,&caller);g_cpu.P|=0x30;g_cpu.P&=(uint8_t)~8u;
            cpu_p_to_mirrors(&g_cpu);g_cpu.D=0;g_cpu.DB=g_cpu.PB=1;
            g_cpu.X=(uint16_t)slot;g_cpu.Y&=0xff;
            /* Native release with Down held: drop rather than throw/kick. */
            guest_jsr(&g_cpu,0x01a015);
            memcpy(g_ram+0x15,input,sizeof(input));
        }
        g_ram[0x1470]=g_ram[0x148f]=0;capture(m,a);
        e=coop_machine_entity(m,action->entity);if(e)release_owner(m,e);
    }
    bound_actor=NULL;restore_registers(&g_cpu,&caller);
    memcpy(g_ram,scratch,sizeof(scratch));g_ram[0x15e9]=sprite;
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
static void apply_checkpoint(CoopMachine *m) {
    bool activated=false;
    for(size_t i=0;i<m->session.action_count;++i)
        activated|=m->session.actions[i].kind==COOP_ACTION_CHECKPOINT;
    if(!activated)return;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        coop_guest_bind(&a->guest,g_ram);
        /* Death keeps its small pose; checkpoint_upgrade survives in core
         * state and supplies the big form when that actor safely recovers. */
        g_ram[0x19]=(uint8_t)(p->life==COOP_DYING?COOP_SMALL:p->power);
        capture(m,a);
    }
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
static void apply_clear(CoopMachine *m) {
    const CoopAction *exit=NULL;
    for(size_t i=0;i<m->session.action_count;++i)
        if(m->session.actions[i].kind==COOP_ACTION_EXIT)exit=&m->session.actions[i];
    if(!exit)return;
    if(exit->entity!=COOP_NO_ENTITY && exit->entity>=12)
        Die("Native co-op clear has no valid goal sprite");
    CoopActor *winner=coop_machine_actor(m,exit->player);
    CpuState caller=g_cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    uint8_t sprite=g_ram[0x15e9];
    coop_guest_bind(&winner->guest,g_ram);bound_actor=winner;goal_commit=true;
    g_cpu.P|=0x30;g_cpu.P&=(uint8_t)~8u;cpu_p_to_mirrors(&g_cpu);
    g_cpu.Y&=0xff;g_cpu.D=0;g_cpu.DB=g_cpu.PB=1;
    uint8_t type=0;
    if(exit->entity!=COOP_NO_ENTITY) {
        g_cpu.X=exit->entity;g_ram[0x15e9]=(uint8_t)exit->entity;
        type=g_ram[0x9e + exit->entity];
        if(type!=0x7b && type!=0x4a)Die("Native co-op clear has an unsupported source");
        guest_jsr(&g_cpu,type==0x7b?0x01c0e7:0x018778);
    }
    capture(m,winner);
    if(type!=0x7b && goal_contact) {
        /* Exit ownership and a valid tape reward are independent. If a
         * sphere wins the normal-exit tie, execute the tape's accepted-bonus
         * tail once, without replacing the chosen exit's music/type. */
        CoopActor *bonus=coop_machine_actor(m,goal_actor);
        coop_guest_bind(&bonus->guest,g_ram);bound_actor=bonus;
        g_cpu.X=goal_entity;g_cpu.DB=g_cpu.PB=1;
        g_ram[0x15e9]=(uint8_t)goal_entity;++g_ram[0x1602+goal_entity];
        guest_jsr(&g_cpu,0x01c109);capture(m,bonus);
    }
    goal_commit=false;bound_actor=NULL;
    restore_registers(&g_cpu,&caller);memcpy(g_ram,scratch,sizeof(scratch));
    g_ram[0x15e9]=sprite;
    CoopActor *anchor=winner;
    if(coop_player(&m->session,winner->player)->life!=COOP_PLAYING) {
        CoopActor *survivor=NULL;
        for(size_t i=0;i<m->actor_count;++i) {
            CoopActor *a=&m->actors[i];
            if(coop_player(&m->session,a->player)->life==COOP_PLAYING &&
               (!survivor || coop_player_precedes(&m->session,a->player,survivor->player)))
                survivor=a;
        }
        if(survivor)anchor=survivor;
    }
    CoopGuestPlayer entry=anchor->guest;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        CoopPower power=p->power;uint32_t reserve=p->reserve;
        bool returning=p->life!=COOP_PLAYING;
        if(p->life==COOP_DYING || p->life==COOP_DEATH_BUBBLE)power=COOP_SMALL;
        coop_guest_bind(returning?&entry:&a->guest,g_ram);
        g_ram[0x19]=(uint8_t)power;g_ram[0xdc2]=(uint8_t)reserve;
        g_ram[0x71]=g_ram[0x78]=g_ram[0x1490]=g_ram[0x1496]=g_ram[0x1497]=0;
        if(returning) {
            g_ram[0x187a]=g_ram[0x1470]=g_ram[0x1471]=g_ram[0x148f]=0;
            p->mount=p->held_object=COOP_NO_ENTITY;
        }
        p->life=COOP_PLAYING;p->recovery_ticks=p->separation_ticks=0;
        p->checkpoint_upgrade=false;capture(m,a);
    }
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
static void recover_and_frame(CoopMachine *m) {
    CoopSession *s=&m->session;
    bool resized=s->camera.width!=g_smw_viewport.width || s->camera.height!=224;
    bool vertical=(g_ram[0x5b]&1)!=0;
    int width=vertical?512:(int)g_ram[0x5d]*256;
    int height=vertical?(int)g_ram[0x5d]*256:432;
    if(!coop_camera_frame(s,g_smw_viewport.width,224,width,height,!g_ram[0x1411]))
        Die("Native co-op camera framing failed");
    s->camera.x=(int)read16(0x1a)-SmwViewOffset(g_smw_viewport,(int)read16(0x1a),(g_ram[0x5e]+1)*256);
    s->camera.y=(int)read16(0x1c);
    if(!coop_camera_check_separation(s,resized))Die("Native co-op separation check failed");
    /* Drop before recovery binds a new position: objects stay at departure. */
    apply_object_drops(m);
    CoopTerrain terrain={g_ram,g_rom,0x80000}; /* validated stock-US ROM */
    if(!coop_session_recover(s,safe_recovery,&terrain))Die("Native co-op recovery failed");
    for(size_t i=0;i<s->action_count;++i) {
        const CoopAction *action=&s->actions[i];
        if(action->kind!=COOP_ACTION_RECOVER)continue;
        CoopActor *a=coop_machine_actor(m,action->player);
        CoopPlayer *p=coop_player(s,action->player);
        CoopActor *anchor=coop_machine_actor(m,action->value);
        uint8_t star=0;coop_guest_peek(&a->guest,0x1490,&star);
        /* Use a clean pose/movement image from the safe anchor. The returning
         * actor's equipment, reserve and protection remain its own. */
        coop_guest_bind(&anchor->guest,g_ram);
        g_ram[0x187a]=0;g_ram[0x78]=0;g_ram[0x1490]=star;
        g_ram[0x1470]=g_ram[0x1471]=g_ram[0x148f]=0;
        const CoopPlayer *anchor_player=coop_player(s,anchor->player);
        p->grounded=anchor_player->grounded;p->swimming=anchor_player->swimming;
        p->behind_fence=anchor_player->behind_fence;
        coop_guest_place_player(p,g_ram);capture(m,a);
    }
    CoopActor *primary=primary_actor(m);
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(s,a->player);
        if(p->life!=COOP_DEATH_BUBBLE && p->life!=COOP_CATCHUP_BUBBLE)continue;
        coop_guest_bind(&a->guest,g_ram);
        if(s->advancing) {
            if(a!=primary)tick_secondary_timers();
            if(!(g_ram[0x14]&3) && g_ram[0x1490])--g_ram[0x1490];
        }
        g_ram[0x78]=0xff;g_ram[0x71]=0;
        capture(m,a);
    }
    coop_guest_bind(&primary->guest,g_ram);
}
void SmwCoopSimulationEnd(void) {
    CoopMachine *m=SmwCoopMachine();if(!m || !level_frame)return;
    capture(m,primary_actor(m));
    m->session.lives=g_ram[0xdbe]+1u;m->session.coins=g_ram[0xdbf];
    if(!end_timer_at_begin && g_ram[0x1493] && m->session.outcome==COOP_CONTINUE) {
        /* Original boss/switch scripts have already produced their shared
         * scene. Register that transition even when their gameplay is frozen;
         * no script is replayed and no gameplay clocks advance here. */
        CoopPlayerId owner=scene_request==COOP_NO_PLAYER?m->session.primary:scene_request;
        unsigned secret=g_ram[0x141c] || (g_ram[0x13c6] && g_ram[0x13bf]==0x13);
        if(!coop_session_event(&m->session,(CoopEvent){COOP_EVENT_EXIT,owner,
                COOP_NO_ENTITY,0,secret?COOP_EVENT_SECRET:0,0}))
            Die("Native co-op could not record a scripted level clear");
    }
    if(!coop_session_resolve(&m->session))Die("Native co-op event resolution failed");
    apply_object_drops(m);
    apply_checkpoint(m);
    apply_clear(m);
    if(m->session.outcome==COOP_CONTINUE && room_request!=COOP_NO_PLAYER) {
        CoopActor *entrant=coop_machine_actor(m,room_request);
        /* 05:D796 selects ExitTableLow by the entrant's horizontal/vertical
         * screen. Publish only these loader inputs; each actor keeps its own
         * canonical image and equipment through the fade. */
        for(unsigned at=0x94;at<0x98;++at) {
            uint8_t value=0;coop_guest_peek(&entrant->guest,(uint16_t)at,&value);
            g_ram[at]=value;
        }
        ++g_ram[0x141a];g_ram[0x100]=0x0f;
    } else if(m->session.outcome==COOP_CONTINUE) {
        recover_and_frame(m);
    }
    if(m->session.outcome==COOP_RETRY || m->session.outcome==COOP_GAME_OVER) {
        /* Timeout can end the attempt while a survivor is still carrying.
         * No owned entity from that attempt may enter the native room loader. */
        for(size_t i=0;i<m->entity_count;) {
            CoopEntity *e=&m->entities[i];
            if(e->owner==COOP_NO_PLAYER) {++i;continue;}
            if(e->kind==COOP_ENTITY_NORMAL)g_ram[0x14c8+e->slot]=0;
            else g_ram[0x170b+e->slot]=0;
            coop_machine_forget_entity(m,e->id);
        }
        for(size_t i=0;i<m->actor_count;++i) {
            CoopActor *a=&m->actors[i];coop_guest_bind(&a->guest,g_ram);
            g_ram[0x1470]=g_ram[0x148f]=g_ram[0x187a]=0;
            g_ram[0x13f3]=g_ram[0x1891]=0;capture(m,a);
        }
        coop_guest_bind(&primary_actor(m)->guest,g_ram);
        g_ram[0xdbe]=(uint8_t)(m->session.lives-1u);
        g_ram[0xdc1]=0; /* no mount carried out of the failed attempt */
        g_ram[0x9d]=0;g_ram[0x0daf]=1;
        if(m->session.outcome==COOP_RETRY) {
            /* The primary entrance loader (05:D796) reads the shared
             * overworld checkpoint flag when SublevelCount is zero. The
             * stock overworld return (04:8F35) normally publishes this bit;
             * immediate team retry does not visit that overworld stage. */
            if(g_ram[0x13ce])g_ram[0x1ea2+g_ram[0x13bf]]|=0x40;
            g_ram[0x141a]=g_ram[0x141d]=g_ram[0x1b93]=0;
            g_ram[0x100]=0x0f;
        } else {
            g_ram[0x1dfb]=10;g_ram[0x143b]=0x14;
            g_ram[0x143c]=0xc0;g_ram[0x143d]=0xff;g_ram[0x100]=0x15;
        }
    }
    retire_released_objects(m);
    /* Read-only running trace for proving per-actor motion and world cadence. */
    const char *path=getenv("SMW_COOP_TRACE");
    static FILE *trace;
    if(path && *path && !trace) {
        trace=fopen(path,"w");
        if(trace)fputs("frame,world_frame,player,x,y,power,animation,input,stack,life,recovery,lives,lock,camera_x,camera_y,protection,separation,reserve,mode,sublevel,level_data,room_request,checkpoint,checkpoint_upgrade,outcome,end_timer,peace,spotlight,goal_stars,exit_player,exit_flags,held_object,held_slot,held_status,held_x,held_y,level,drop_entity,drop_x,drop_y\n",trace);
    }
    if(trace) {
        for(size_t i=0;i<m->actor_count;++i) {
            CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
            /* Actor animation is reported by reading its own image without
             * binding it into the running game's WRAM. */
            uint8_t animation=0;
            coop_guest_peek(&a->guest,0x71,&animation);
            CoopPlayerId exit_player=COOP_NO_PLAYER;unsigned exit_flags=0;
            for(size_t j=0;j<m->session.action_count;++j)
                if(m->session.actions[j].kind==COOP_ACTION_EXIT) {
                    exit_player=m->session.actions[j].player;exit_flags=m->session.actions[j].value;
                }
            const CoopEntity *held=coop_machine_entity(m,p->held_object);
            const CoopEntity *dropped=NULL;
            for(size_t j=0;j<m->session.action_count;++j) {
                const CoopAction *action=&m->session.actions[j];
                if(action->kind==COOP_ACTION_DROP_OBJECT && action->player==p->id)
                    dropped=coop_machine_entity(m,action->entity);
            }
            unsigned slot=held?held->slot:12;
            fprintf(trace,"%llu,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                (unsigned long long)m->session.frame,g_ram[0x14],p->id,p->x,p->y,
                (unsigned)p->power,animation,p->held_input,g_cpu.S,(unsigned)p->life,
                p->recovery_ticks,m->session.lives,g_ram[0x9d],
                g_ram[0x1a]|(g_ram[0x1b]<<8),g_ram[0x1c]|(g_ram[0x1d]<<8),
                p->protection_ticks,p->separation_ticks,p->reserve,g_ram[0x100],
                g_ram[0x141a],(unsigned)(g_ram[0xce]|(g_ram[0xcf]<<8)|(g_ram[0xd0]<<16)),room_request,
                m->session.checkpoint,p->checkpoint_upgrade?1u:0u,(unsigned)m->session.outcome,
                g_ram[0x1493],g_ram[0x1b99],g_ram[0x1433],g_ram[0x1900],exit_player,exit_flags,
                p->held_object,slot,held?g_ram[0x14c8+slot]:0,
                held?(g_ram[0xe4+slot]|(g_ram[0x14e0+slot]<<8)):0,
                held?(g_ram[0xd8+slot]|(g_ram[0x14d4+slot]<<8)):0,m->level,
                dropped?dropped->id:COOP_NO_ENTITY,
                dropped?(g_ram[0xe4+dropped->slot]|(g_ram[0x14e0+dropped->slot]<<8)):0,
                dropped?(g_ram[0xd8+dropped->slot]|(g_ram[0x14d4+dropped->slot]<<8)):0);
        }
        fflush(trace);
    }
    level_frame=false;
}
