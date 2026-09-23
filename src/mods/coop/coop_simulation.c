#include "coop_simulation.h"
#include "coop_runtime.h"
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
static bool level_frame;

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
    }
    m->room_initialized=true;
    coop_guest_bind(&primary_actor(m)->guest,g_ram);
}
void SmwCoopSimulationBegin(void) {
    level_frame=false;
    CoopMachine *m=SmwCoopMachine();if(!m)return;
    unsigned mode=g_ram[0x100];
    if(mode!=0x14) {m->previous_mode=mode;return;}
    if(!m->room_initialized || m->previous_mode!=0x14)initialize_room(m);
    m->previous_mode=mode;level_frame=true;
    for(size_t i=0;i<m->actor_count;++i) {
        CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
        uint32_t actions=SNES_PAD_A|SNES_PAD_B|SNES_PAD_X|SNES_PAD_Y|SNES_PAD_SELECT;
        coop_player_input(p,RtlGetPadState((int)a->input_seat),actions);
    }
    bool advances=!g_ram[0x13d4] && !g_ram[0x1426] && !g_ram[0x9d] && !g_ram[0x13fb];
    if(!coop_session_begin_frame(&m->session,advances))Die("Unable to begin native co-op frame");
    capture(m,primary_actor(m));
}

static void tick_secondary_timers(void) {
    /* The world routine immediately preceding C569 already advanced the
     * primary actor and the shared timers. Only audited actor timer fields
     * are advanced here. Mount/global timers are not included. */
    if(g_ram[0x9d])return;
    for(unsigned a=0x1496;a<=0x14a2;++a)if(g_ram[a])--g_ram[a];
    for(unsigned a=0x14a4;a<=0x14a6;++a)if(g_ram[a])--g_ram[a];
    if(!(g_ram[0x14]&3)) {
        if(g_ram[0x14a9])--g_ram[0x14a9];
        if(g_ram[0x14aa])--g_ram[0x14aa];
    }
}
static void run_player_routines(CpuState *cpu,CoopMachine *m) {
    CpuState caller=*cpu;
    uint8_t scratch[16];memcpy(scratch,g_ram,sizeof(scratch));
    uint8_t turn=g_ram[0xdb3];
    CoopActor *primary=primary_actor(m);
    /* Primary timers and animation graphics changed since frame begin. */
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
        g_ram[0xdb3]=(uint8_t)(p->character==1);
        coop_guest_set_input(g_ram,SwapInputBits((uint16_t)p->held_input),
                             SwapInputBits((uint16_t)p->pressed_input));
        restore_registers(cpu,&caller);
        memcpy(g_ram,scratch,sizeof(scratch));
        cpu_push_jsr_return_frame(cpu);
        if(!interp_bridge_run(cpu,0x00c569))Die("Native co-op player routine failed to return");
        if(cpu->S!=caller.S)Die("Native co-op player routine unbalanced the guest stack");
        capture(m,a);
        if(g_ram[0x100]!=0x14)break;
    }
    --player_call_depth;
    restore_registers(cpu,&caller);
    memcpy(g_ram,scratch,sizeof(scratch));g_ram[0xdb3]=turn;
    coop_guest_bind(&primary->guest,g_ram);
}
uint32_t SmwCoopSimulationHook(CpuState *cpu,uint32_t pc) {
    if(!level_frame || player_call_depth)return 0;
    CoopMachine *m=SmwCoopMachine();if(!m)return 0;
    if((pc&0x7fffff)==0x00c569) {
        run_player_routines(cpu,m);
        return 0x00c592; /* original RTS, with the original return frame */
    }
    return 0;
}
void SmwCoopSimulationEnd(void) {
    CoopMachine *m=SmwCoopMachine();if(!m || !level_frame)return;
    capture(m,primary_actor(m));
    m->session.lives=g_ram[0xdbe]+1u;m->session.coins=g_ram[0xdbf];
    /* Read-only running trace for proving per-actor motion and world cadence. */
    const char *path=getenv("SMW_COOP_TRACE");
    static FILE *trace;
    if(path && *path && !trace) {
        trace=fopen(path,"w");
        if(trace)fputs("frame,world_frame,player,x,y,power,animation,input,stack\n",trace);
    }
    if(trace) {
        for(size_t i=0;i<m->actor_count;++i) {
            CoopActor *a=&m->actors[i];CoopPlayer *p=coop_player(&m->session,a->player);
            /* Actor animation is reported by reading its own image without
             * binding it into the running game's WRAM. */
            uint8_t animation=0;
            coop_guest_peek(&a->guest,0x71,&animation);
            fprintf(trace,"%llu,%u,%u,%d,%d,%u,%u,%u,%u\n",
                (unsigned long long)m->session.frame,g_ram[0x14],p->id,p->x,p->y,
                (unsigned)p->power,animation,p->held_input,g_cpu.S);
        }
        fflush(trace);
    }
    level_frame=false;
}
