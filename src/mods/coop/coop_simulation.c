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
static CoopActor *bound_actor;
static bool level_frame;
static bool timer_had_time;
static bool gameplay_stage_started;
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
static void initialize_room(CoopMachine *m) {
    if(m->session.outcome==COOP_GAME_OVER) {
        for(size_t i=0;i<m->session.player_count;++i)m->session.players[i].reserve=0;
        coop_session_restart(&m->session);
    } else if(m->session.outcome==COOP_RETRY)coop_session_restart(&m->session);
    CoopGuestPlayer entry;coop_guest_capture(&entry,g_ram);
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        coop_guest_bind(&entry,g_ram);
        /* Entry movement/pose belongs to the new room; equipment belongs to
         * the persistent actor. Everyone uses the original entrance point. */
        if(m->room_initialized || a->player!=m->session.primary) {
            g_ram[0x19]=(uint8_t)p->power;g_ram[0xdc2]=(uint8_t)p->reserve;
        }
        capture(m,a);
        a->pending.count=a->visible.count=0;
    }
    m->room_initialized=true;
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
void SmwCoopSimulationBegin(void) {
    level_frame=gameplay_stage_started=false;
    CoopMachine *m=SmwCoopMachine();if(!m)return;
    unsigned mode=g_ram[0x100];
    if(mode!=0x14) {m->previous_mode=mode;return;}
    if(!m->room_initialized || m->previous_mode!=0x14)initialize_room(m);
    m->previous_mode=mode;level_frame=true;
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
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
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
        if(!interp_bridge_run(cpu,0x00c569))Die("Native co-op player routine failed to return");
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
    unsigned slot=cpu->X&0xff;if(slot>=12 || !g_ram[0x14c8+slot])return false;
    CoopActor *primary=primary_actor(m);capture(m,primary);
    int x=(g_ram[0xe4+slot]|(g_ram[0x14e0+slot]<<8))+8;
    int y=(g_ram[0xd8+slot]|(g_ram[0x14d4+slot]<<8))+8;
    CoopPlayerId target=coop_nearest_player(&m->session,x,y,false,false);
    CoopActor *a=coop_machine_actor(m,target);
    if(!a || a==primary)return false;
    CpuState caller=*cpu;uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    coop_guest_bind(&a->guest,g_ram);
    bound_actor=a;
    ++sprite_call_depth;cpu_push_jsr_return_frame(cpu);
    if(!interp_bridge_run(cpu,0x018127) || cpu->S!=caller.S)
        Die("Native co-op sprite routine failed its guest return contract");
    --sprite_call_depth;bound_actor=NULL;capture(m,a);
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
        g_ram[0x148f]=g_ram[0x1470];g_ram[0x1470]=0;
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
    if(!level_frame)return 0;
    CoopMachine *m=SmwCoopMachine();if(!m)return 0;
    pc&=0x7fffff;
    if(pc==0x00a21b) {party_pause(m);return 0x00a242;}
    if(pc==0x00a28a)begin_gameplay_stage(m);
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
    if(!coop_session_resolve(&m->session))Die("Native co-op event resolution failed");
    if(m->session.outcome==COOP_CONTINUE)recover_and_frame(m);
    if(m->session.outcome==COOP_RETRY || m->session.outcome==COOP_GAME_OVER) {
        g_ram[0xdbe]=(uint8_t)(m->session.lives-1u);
        g_ram[0xdc1]=0; /* no mount carried out of the failed attempt */
        g_ram[0x9d]=0;g_ram[0x0daf]=1;
        if(m->session.outcome==COOP_RETRY) {
            /* The primary entrance loader (05:D796) reads the shared
             * overworld checkpoint flag when SublevelCount is zero. */
            g_ram[0x141a]=g_ram[0x141d]=g_ram[0x1b93]=0;
            g_ram[0x100]=0x0f;
        } else {
            g_ram[0x1dfb]=10;g_ram[0x143b]=0x14;
            g_ram[0x143c]=0xc0;g_ram[0x143d]=0xff;g_ram[0x100]=0x15;
        }
    }
    /* Read-only running trace for proving per-actor motion and world cadence. */
    const char *path=getenv("SMW_COOP_TRACE");
    static FILE *trace;
    if(path && *path && !trace) {
        trace=fopen(path,"w");
        if(trace)fputs("frame,world_frame,player,x,y,power,animation,input,stack,life,recovery,lives,lock,camera_x,camera_y,protection,separation\n",trace);
    }
    if(trace) {
        for(size_t i=0;i<m->actor_count;++i) {
            CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
            /* Actor animation is reported by reading its own image without
             * binding it into the running game's WRAM. */
            uint8_t animation=0;
            coop_guest_peek(&a->guest,0x71,&animation);
            fprintf(trace,"%llu,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                (unsigned long long)m->session.frame,g_ram[0x14],p->id,p->x,p->y,
                (unsigned)p->power,animation,p->held_input,g_cpu.S,(unsigned)p->life,
                p->recovery_ticks,m->session.lives,g_ram[0x9d],
                g_ram[0x1a]|(g_ram[0x1b]<<8),g_ram[0x1c]|(g_ram[0x1d]<<8),
                p->protection_ticks,p->separation_ticks);
        }
        fflush(trace);
    }
    level_frame=false;
}
