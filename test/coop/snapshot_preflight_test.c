/* Exercise the real game preflight with copied RTLS v9/CNR5 data.
 * Only the host storage/count services are replaced; roster decoding and
 * transactional load staging are the production implementations. */
#include "mods/coop/coop_runtime.h"
#include "common_rtl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8 *g_sram;
int g_sram_size;
static size_t current_guest_size;
static uint32 current_guest_version=10;
int RtlWriteSram(void) {return 1;}
void RtlSetSaveRoot(const char *root) {(void)root;}
void RtlEnsureSaveDir(void) {}
void RtlReadSram(void) {}
void MkDir(const char *path) {(void)path;}
size_t SmwStateSharedExtraSize(void) {return 145;}
size_t RtlSnapshotGuestSize(uint32 *version) {
    *version=current_guest_version;return current_guest_size;
}
static unsigned word(const uint8 *p) {
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
static void put(uint8 *p,unsigned v) {for(unsigned i=0;i<4;i++)p[i]=(uint8)(v>>(8*i));}
static void unchanged(const void *before,size_t size) {
    CoopMachine *m=SmwCoopMachine();
    assert(coop_machine_save_size(m)==size);
    void *after=malloc(size);assert(after);
    assert(coop_machine_save(m,after,size));
    assert(!memcmp(before,after,size));free(after);
}
static void discard(SaveLoadInfo *sli,void *data,size_t size) {
    (void)sli;memset(data,0,size);
}

int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long file_size=ftell(f);assert(file_size>52);
    rewind(f);
    uint8 *file=malloc((size_t)file_size);assert(file);
    assert(fread(file,1,(size_t)file_size,f)==(size_t)file_size);fclose(f);
    assert(!memcmp(file,"RSGUARD\1",8));
    uint8 *v9=file+48;size_t size9=(size_t)file_size-52;
    assert(word(v9)==0x52544c53 && word(v9+4)==9);
    size_t native_size=word(v9+size9-8);
    assert(native_size+153<size9);
    size_t guest9=size9-native_size-153;
    current_guest_size=guest9+1;

    SmwCoopEnable(true);assert(SmwCoopPrepareStorage());assert(SmwCoopActive());
    CoopMachine *live=SmwCoopMachine();
    size_t before_size=coop_machine_save_size(live);
    void *before=malloc(before_size);assert(before);
    assert(coop_machine_save(live,before,before_size));
    assert(SmwCoopSnapshotPreflight(v9,size9));unchanged(before,before_size);
    uint8 *v10=malloc(size9+1);assert(v10);
    memcpy(v10,v9,guest9);v10[guest9]=0;
    memcpy(v10+guest9+1,v9+guest9,size9-guest9);put(v10+4,10);
    assert(SmwCoopSnapshotPreflight(v10,size9+1));unchanged(before,before_size);
    put(v10+4,9);assert(!SmwCoopSnapshotPreflight(v10,size9+1));
    put(v10+4,11);assert(!SmwCoopSnapshotPreflight(v10,size9+1));
    put(v10+4,10);put(v9+4,10);assert(!SmwCoopSnapshotPreflight(v9,size9));put(v9+4,9);
    assert(!SmwCoopSnapshotPreflight(NULL,0));
    for(size_t n=0;n<size9;n++)assert(!SmwCoopSnapshotPreflight(v9,n));
    unchanged(before,before_size);
    /* CRC, length, magic and renderer-version damage must not stage a load. */
    const size_t damage[]={0,guest9,guest9+8,guest9+145+20,size9-8,size9-9};
    for(size_t i=0;i<sizeof(damage)/sizeof(damage[0]);i++) {
        v9[damage[i]]^=1;assert(!SmwCoopSnapshotPreflight(v9,size9));v9[damage[i]]^=1;
        unchanged(before,before_size);
    }
    current_guest_version=11;
    assert(!SmwCoopSnapshotPreflight(v9,size9));
    assert(!SmwCoopSnapshotPreflight(v10,size9+1));
    current_guest_version=10;
    /* A rejected preflight after an accepted one must also discard staging. */
    assert(SmwCoopSnapshotPreflight(v9,size9));
    assert(!SmwCoopSnapshotPreflight(NULL,0));
    SaveLoadInfo sli={.func=discard};
    SmwCoopLoadExtra(&sli);SmwCoopStateLoaded();unchanged(before,before_size);
    assert(SmwCoopSnapshotPreflight(v9,size9));
    SmwCoopLoadExtra(&sli);SmwCoopStateLoaded();
    assert(SmwCoopMachine()->level==0x106);
    assert(SmwCoopSnapshotPreflight(v10,size9+1));
    SmwCoopLoadExtra(&sli);SmwCoopStateLoaded();
    assert(SmwCoopMachine()->level==0x106);
    SmwCoopEnable(false);free(before);free(v10);free(file);
    puts("preflight: v9/v10, every truncated prefix, malformed schemas and atomic staging passed");
    return 0;
}
