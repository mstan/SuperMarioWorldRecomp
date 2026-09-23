#include "coop_runtime.h"
#include "coop_simulation.h"
#include "common_rtl.h"
#include "snes/interp_bridge.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COOP_ROOT "saves/native-coop-v1"
#define SELECTION_FILE COOP_ROOT "/last-mode.txt"
enum { PRODUCT_PLAYERS=2, RTS_PC=0x00c592, FILE_ENTER=0x009cb0 };
static bool enabled, choosing, files_chosen, disconnected;
static unsigned selected_count;
static CoopMachine machine, staged;
static size_t staged_size;
static bool staged_consumed;

/* Existing shared SMW extras precede the native roster. */
extern size_t SmwStateSharedExtraSize(void);
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}

void SmwCoopEnable(bool on) {
    coop_machine_destroy(&machine);coop_machine_destroy(&staged);
    enabled=on;selected_count=0;choosing=files_chosen=disconnected=false;
    staged_size=0;staged_consumed=false;
}
bool SmwCoopEnabled(void) {return enabled;}
bool SmwCoopActive(void) {return enabled && selected_count>1;}
CoopMachine *SmwCoopMachine(void) {return SmwCoopActive()?&machine:NULL;}
bool SmwCoopPersistenceReady(void) {return !enabled || (selected_count && !choosing);}
bool SmwCoopFrozen(void) {return enabled && disconnected;}

static bool choose_storage(unsigned count,bool remember) {
    if(count!=1 && count!=PRODUCT_PLAYERS)return false;
    CoopMachine next={0};
    if(count>1 && !coop_machine_init(&next,count))return false;
    /* Flush only SRAM that already belongs to the previous namespace. The
     * guest decides when progress enters SRAM; this adds no save opportunity. */
    if(selected_count)RtlWriteSram();
    RtlSetSaveRoot(count>1?COOP_ROOT:"saves");
    RtlEnsureSaveDir();
    if(g_sram && g_sram_size>0)memset(g_sram,0,(size_t)g_sram_size);
    RtlReadSram();
    coop_machine_destroy(&machine);machine=next;selected_count=count;
    if(remember) {
        MkDir("saves");MkDir(COOP_ROOT);
        FILE *f=fopen(SELECTION_FILE,"wb");
        if(f){fputc((int)('0'+count),f);fclose(f);}
    }
    return true;
}
bool SmwCoopPrepareStorage(void) {
    if(!enabled)return true;
    FILE *f=fopen(SELECTION_FILE,"rb");int count=f?fgetc(f):0;if(f)fclose(f);
    if(count=='1' || count=='2')return choose_storage((unsigned)(count-'0'),false);
    /* Pending selection has no campaign data and never reads normal SRAM. */
    RtlSetSaveRoot("saves/native-coop-menu");
    return false;
}
void SmwCoopResetSession(void) {
    if(!enabled)return;
    files_chosen=choosing=disconnected=false;
    if(selected_count>1) {
        CoopMachine next={0};
        if(coop_machine_init(&next,selected_count)) {coop_machine_destroy(&machine);machine=next;}
    }
}

bool SmwCoopHostFrame(uint32_t inputs) {
    if(!enabled)return true;
    bool was_disconnected=disconnected;
    disconnected=false;
    if(SmwCoopActive() && g_ram[0x100]>=0x0b) {
        for(size_t i=0;i<machine.actor_count;++i) {
            CoopActor *a=&machine.actors[i];
            CoopPlayer *p=coop_player(&machine.session,a->player);
            /* Product host has two input seats; the core and state format do
             * not. Expansion changes this adapter and input UI together. */
            bool connected=a->input_seat<PRODUCT_PLAYERS && (inputs&(1u<<(30+a->input_seat)))!=0;
            if(p->connected!=connected) {
                p->held_input=p->pressed_input=0;p->action_latch=0xfff;
            }
            p->connected=connected;
            if(!connected)disconnected=true;
        }
    }
    if(disconnected && !was_disconnected)
        fprintf(stderr,"[co-op] Waiting for both controller/keyboard assignments.\n");
    return !disconnected;
}

uint32_t SmwCoopGuestHook(CpuState *cpu,uint32_t pc) {
    if(!enabled)return 0;
    switch(pc&0x7fffff) {
    case FILE_ENTER:
        if(!files_chosen) {
            /* Reuse the ROM's player-count stripe and cursor. Its ordinary
             * menu code remains in charge of drawing, navigation and sound. */
            choosing=true;g_ram[0x100]=9;g_ram[0x1b92]=0;
            return 0x009d22; /* SEP #$10; LDY #$12; INC GameMode; show stripe */
        }
        break;
    case 0x009e0d:
        if(choosing) {
            unsigned count=(cpu->X&0xff)+1;
            if(!choose_storage(count,true))return RTS_PC;
            choosing=false;files_chosen=true;g_ram[0x100]=7;
            return FILE_ENTER;
        }
        break;
    case 0x009d22:
        if(files_chosen) {
            /* FileSelected has completed its normal checksum/repair/copy.
             * Skip the now-redundant count menu. One overworld party prevents
             * the original alternating-turn machinery from taking ownership. */
            g_ram[0xdb2]=0;g_ram[0xdb3]=0;
            cpu->P|=0x10;cpu_p_to_mirrors(cpu);cpu->X&=0xff;cpu->Y&=0xff;
            return 0x009e10; /* CopyFromSaveBuffer; DecompressOverworldL2 */
        }
        break;
    }
    return SmwCoopSimulationHook(cpu,pc);
}
static void interpreted_hook(CpuState *cpu,uint32_t pc) {
    uint32_t target=SmwCoopGuestHook(cpu,pc);
    if(target)interp_bridge_pre_opcode_redirect(target);
}
bool SmwCoopInstallHooks(void) {
    if(!enabled)return true;
#define COOP_HOOK(pc) if(!interp_bridge_add_pre_opcode_hook(pc,interpreted_hook))return false;
#define COOP_HOOK_INSIDE(pc,owner) COOP_HOOK(pc)
#include "coop_hooks.def"
#undef COOP_HOOK_INSIDE
#undef COOP_HOOK
    return true;
}

const char *SmwCoopSnapshotIdentity(void) {return SmwCoopActive()?"smw.us.native-coop.v1":NULL;}
bool SmwCoopSnapshotPreflight(const void *data,size_t size) {
    coop_machine_destroy(&staged);staged_size=0;staged_consumed=false;
    if(!SmwCoopActive())return !enabled || SmwCoopPersistenceReady();
    const uint8_t *p=data;if(size<16 || memcmp(p+size-4,"CNRE",4))return false;
    uint32_t version=0;size_t guest=RtlSnapshotGuestSize(&version);
    size_t extra=SmwStateSharedExtraSize(),n=read32(p+size-8);
    if(!guest || read32(p+4)!=version || guest>size || extra>size-guest ||
        size-guest-extra<8 || n!=size-guest-extra-8)return false;
    if(read32(p+guest)!=0x31434d53u || read32(p+guest+4)!=0x53574d53u ||
        read32(p+guest+8)!=1u)return false;
    if(!coop_machine_load(&staged,p+guest+extra,n))return false;
    if(staged.actor_count!=selected_count) {coop_machine_destroy(&staged);return false;}
    staged_size=n;return true;
}
void SmwCoopSaveExtra(SaveLoadInfo *sli) {
    if(!SmwCoopActive())return;
    size_t n=coop_machine_save_size(&machine);
    uint8_t *p=n && n<=UINT32_MAX?malloc(n):NULL;
    uint8_t footer[8]={0};memcpy(footer+4,"CNRE",4);
    if(!p || !coop_machine_save(&machine,p,n)) {
        free(p);RtlStateStreamFail(sli);return;
    }
    sli->func(sli,p,n);put32(footer,(uint32_t)n);
    sli->func(sli,footer,sizeof(footer));free(p);
}
void SmwCoopLoadExtra(SaveLoadInfo *sli) {
    if(!SmwCoopActive() || !staged_size)return;
    uint8_t discard[256];size_t left=staged_size+8;
    while(left) {size_t n=left>sizeof(discard)?sizeof(discard):left;sli->func(sli,discard,n);left-=n;}
    staged_consumed=true;
}
void SmwCoopStateLoaded(void) {
    if(SmwCoopActive() && staged_consumed) {
        coop_machine_destroy(&machine);machine=staged;memset(&staged,0,sizeof(staged));
        choosing=disconnected=false;files_chosen=true;
        fprintf(stderr,"[co-op] restored %zu players with native actor state\n",machine.actor_count);
    }
    staged_size=0;staged_consumed=false;
}
