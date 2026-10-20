/* Real SDL worker, with a gated writer: enqueue/readback cannot wait for disk.
 * The separate framework harness tests actual durable file publication. */
#include "desktop/sdl_compat.h"
#include "mods/coop/coop_storage.h"
#include "common_rtl.h"
#include <assert.h>

static uint8 live[4];
uint8 *g_sram=live;
int g_sram_size=sizeof(live);
static char current_path[128]="normal/save.srm";
static SDL_mutex *gate;
static SDL_cond *gate_changed;
static bool entered,release_first,fail_latest;
static unsigned calls;
static char paths[16][128];
static uint8 values[16];

void RtlEnsureSaveDir(void) {}
void RtlSramFilePath(char *path,size_t size) {snprintf(path,size,"%s",current_path);}
void Die(const char *message) {fprintf(stderr,"%s\n",message);abort();}
int RtlWriteSramImage(const char *path,const uint8 *bytes,size_t size) {
    assert(size==4);
    SDL_LockMutex(gate);
    unsigned index=calls++;assert(index<16);
    if(index==0) {
        entered=true;SDL_CondSignal(gate_changed);
        while(!release_first)SDL_CondWait(gate_changed,gate);
    }
    snprintf(paths[index],sizeof(paths[index]),"%s",path);
    values[index]=bytes[0];
    for(unsigned i=1;i<4;++i)assert(bytes[i]==bytes[0]);
    bool ok=bytes[0]!=0x11 && !(fail_latest && bytes[0]==0x44);
    SDL_UnlockMutex(gate);return ok;
}
static void queue(const char *path,uint8 value) {
    snprintf(current_path,sizeof(current_path),"%s",path);
    memset(live,value,sizeof(live));assert(SmwCoopQueueStorage());
}
static void restore(const char *path,uint8 expected) {
    snprintf(current_path,sizeof(current_path),"%s",path);
    memset(live,0xee,sizeof(live));
    assert(SmwCoopReadQueuedStorage());
    for(unsigned i=0;i<4;++i)assert(live[i]==expected);
}
int main(void) {
    assert(snesrecomp_sdl_init(0));
    gate=SDL_CreateMutex();gate_changed=SDL_CreateCond();assert(gate && gate_changed);
    queue("normal/save.srm",0x11);
    /* A bounded poll observes worker entry without halting a game runtime. */
    uint64_t deadline=SDL_GetTicks()+5000;
    for(;;) {
        SDL_LockMutex(gate);bool ready=entered;SDL_UnlockMutex(gate);
        if(ready)break;
        assert(SDL_GetTicks()<deadline);SDL_Delay(1);
    }
    /* The worker remains blocked. Main-thread submission and latest-image
     * readback must complete anyway; no current-global pointer may escape. */
    queue("normal/save.srm",0x22);
    queue("coop/save.srm",0x33);
    restore("normal/save.srm",0x22);
    restore("coop/save.srm",0x33);
    SDL_LockMutex(gate);
    assert(calls==1);release_first=true;SDL_CondSignal(gate_changed);
    SDL_UnlockMutex(gate);
    assert(SmwCoopFinishStorage());
    assert(calls==3);
    assert(values[0]==0x11 && values[1]==0x22 && values[2]==0x33);
    assert(!strcmp(paths[0],"normal/save.srm") && !strcmp(paths[1],paths[0]));
    assert(!strcmp(paths[2],"coop/save.srm"));
    assert(!SmwCoopReadQueuedStorage()); /* successful shutdown releases copies */

    fail_latest=true;queue("normal/save.srm",0x44);
    assert(!SmwCoopFinishStorage()); /* worker fails, shutdown retry fails */
    assert(calls==5);
    restore("normal/save.srm",0x44); /* latest unsaved bytes still recoverable */
    fail_latest=false;
    assert(SmwCoopFinishStorage());assert(calls==6);
    assert(values[5]==0x44 && !strcmp(paths[5],"normal/save.srm"));
    assert(!SmwCoopReadQueuedStorage());
    snprintf(current_path,sizeof(current_path),"coop/save.srm");
    memset(live,0x55,sizeof(live));
    SmwCoopStorageShutdown(true);
    assert(calls==7 && values[6]==0x55 && !strcmp(paths[6],"coop/save.srm"));
    assert(!SmwCoopReadQueuedStorage());
    SDL_DestroyCond(gate_changed);SDL_DestroyMutex(gate);SDL_Quit();
    puts("storage queue: nonblocking submission, immutable images, FIFO, latest readback and failure retry passed");
    return 0;
}
