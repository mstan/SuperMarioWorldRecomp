/* Exercise production count/menu routing with observable storage services.
 * Queue acceptance can fail; sentinels distinguish normal and co-op SRAM. */
#include "mods/coop/coop_runtime.h"
#include "common_rtl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8 g_ram[0x20000];
static uint8 live_sram[32],normal_sram[32],coop_sram[32];
uint8 *g_sram=live_sram;
int g_sram_size=sizeof(live_sram);
static char root[96]="saves";
static unsigned reads,writes;
static bool write_ok=true;

static uint8 *stored(void) {
    return !strcmp(root,"saves/native-coop-v1")?coop_sram:normal_sram;
}
bool SmwCoopQueueStorage(void) {
    ++writes;
    if(!write_ok)return 0;
    memcpy(stored(),g_sram,sizeof(live_sram));return 1;
}
bool SmwCoopReadQueuedStorage(void) {return false;}
void RtlSetSaveRoot(const char *value) {snprintf(root,sizeof(root),"%s",value);}
void RtlEnsureSaveDir(void) {}
void RtlReadSram(void) {++reads;memcpy(g_sram,stored(),sizeof(live_sram));}
void MkDir(const char *path) {(void)path;}
uint32_t SmwCoopSimulationHook(CpuState *cpu,uint32_t pc) {
    (void)cpu;(void)pc;return 0;
}
static void enter_menu(CpuState *cpu) {
    SmwCoopResetSession();
    assert(SmwCoopGuestHook(cpu,0x009cb0)==0x009d22);
    assert(!SmwCoopPersistenceReady());
    assert(SmwCoopStorageSelected()); /* exit still preserves this namespace */
}
static uint32_t select_count(CpuState *cpu,unsigned count) {
    cpu->X=(uint16_t)(count-1);
    return SmwCoopGuestHook(cpu,0x009e0d);
}
static void remembered(char expected) {
    FILE *file=fopen("saves/native-coop-v1/last-mode.txt","rb");assert(file);
    assert(fgetc(file)==expected);fclose(file);
}
int main(void) {
    memset(normal_sram,0x11,sizeof(normal_sram));
    memset(coop_sram,0x22,sizeof(coop_sram));
    CpuState cpu={0};
    SmwCoopEnable(true);
    assert(!SmwCoopStorageSelected());
    assert(SmwCoopPrepareStorage());
    assert(reads==1 && writes==0 && SmwCoopActive() && live_sram[0]==0x22);

    /* Same namespace keeps unsaved guest bytes and resets the campaign roster.
     * It must neither discard them by reading nor force a synchronous write. */
    live_sram[0]=0x73;
    enter_menu(&cpu);
    assert(select_count(&cpu,2)==0x009cb0);
    assert(reads==1 && writes==0 && live_sram[0]==0x73);
    assert(SmwCoopActive() && SmwCoopPersistenceReady());
    assert(normal_sram[0]==0x11 && coop_sram[0]==0x22);remembered('2');

    /* Queue rejection leaves the old data/root/mode available for a retry.
     * Worker completion failures are exercised by storage_queue_test.c. */
    enter_menu(&cpu);write_ok=false;
    assert(select_count(&cpu,1)==0x00c592);
    assert(writes==1 && reads==1 && live_sram[0]==0x73);
    assert(SmwCoopActive() && !SmwCoopPersistenceReady());
    assert(!strcmp(root,"saves/native-coop-v1"));
    assert(coop_sram[0]==0x22 && normal_sram[0]==0x11);remembered('2');

    write_ok=true;
    assert(select_count(&cpu,1)==0x009cb0);
    assert(writes==2 && reads==2 && !SmwCoopActive());
    assert(live_sram[0]==0x11 && coop_sram[0]==0x73);
    assert(!strcmp(root,"saves"));remembered('1');

    live_sram[0]=0x46;enter_menu(&cpu);
    assert(select_count(&cpu,2)==0x009cb0);
    assert(writes==3 && reads==3 && SmwCoopActive());
    assert(live_sram[0]==0x73 && normal_sram[0]==0x46);remembered('2');

    enter_menu(&cpu);
    assert(select_count(&cpu,4)==0x00c592);
    assert(writes==3 && reads==3 && SmwCoopActive());
    SmwCoopEnable(false);
    assert(SmwCoopGuestHook(&cpu,0x009cb0)==0);
    assert(SmwCoopGuestHook(&cpu,0x009e0d)==0);
    assert(writes==3 && reads==3 && SmwCoopPersistenceReady());
    puts("storage: same-mode retention, namespace isolation, queue failure/retry and disabled mode passed");
    return 0;
}
