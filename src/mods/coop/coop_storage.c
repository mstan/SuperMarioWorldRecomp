#include "desktop/sdl_compat.h"
#include "coop_storage.h"
#include "common_rtl.h"

typedef struct StorageImage {
    struct StorageImage *next;
    char path[128];
    size_t size;
    bool success;
    uint8 bytes[];
} StorageImage;
static SDL_mutex *mutex;
static SDL_cond *condition;
static SDL_Thread *worker;
static StorageImage *first,*last,*pending;
static bool stopping;

static int storage_worker(void *unused) {
    (void)unused;
    SDL_LockMutex(mutex);
    for(;;) {
        while(!pending && !stopping)SDL_CondWait(condition,mutex);
        if(!pending)break;
        StorageImage *image=pending;pending=image->next;
        SDL_UnlockMutex(mutex);
        /* No guest memory, active-root, SDL or runtime state is read here. */
        image->success=RtlWriteSramImage(image->path,image->bytes,image->size)!=0;
        SDL_LockMutex(mutex);
    }
    SDL_UnlockMutex(mutex);return 0;
}
static bool start_worker(void) {
    if(worker)return true;
    if(!mutex)mutex=SDL_CreateMutex();
    if(!condition)condition=SDL_CreateCond();
    if(!mutex || !condition)return false;
    stopping=false;
    worker=SDL_CreateThread(storage_worker,"smw-coop-sram",NULL);
    return worker!=NULL;
}
static void wait_worker(void) {
    if(!worker)return;
    SDL_LockMutex(mutex);stopping=true;SDL_CondSignal(condition);
    SDL_UnlockMutex(mutex);
    SDL_WaitThread(worker,NULL);worker=NULL;
}
static void release_images(void) {
    while(first) {StorageImage *next=first->next;free(first);first=next;}
    last=pending=NULL;
    if(condition)SDL_DestroyCond(condition);
    if(mutex)SDL_DestroyMutex(mutex);
    condition=NULL;mutex=NULL;
}
bool SmwCoopQueueStorage(void) {
    if(!g_sram || g_sram_size<=0)return true;
    size_t size=(size_t)g_sram_size;
    if(size>SIZE_MAX-sizeof(StorageImage))return false;
    StorageImage *image=calloc(1,sizeof(*image)+size);
    if(!image)return false;
    image->size=size;memcpy(image->bytes,g_sram,size);
    RtlEnsureSaveDir();RtlSramFilePath(image->path,sizeof(image->path));
    if(!start_worker()) {free(image);return false;}
    SDL_LockMutex(mutex);
    if(last)last->next=image;else first=image;
    last=image;if(!pending)pending=image;
    SDL_CondSignal(condition);SDL_UnlockMutex(mutex);
    return true;
}
bool SmwCoopReadQueuedStorage(void) {
    char path[128];RtlSramFilePath(path,sizeof(path));
    StorageImage *latest=NULL;
    /* Only the main thread changes the retained list or image bytes. The
     * worker changes success, which is inspected only after joining it. */
    for(StorageImage *image=first;image;image=image->next)
        if(!strcmp(image->path,path))latest=image;
    if(!latest)return false;
    if(!g_sram || g_sram_size<0 || latest->size!=(size_t)g_sram_size)
        Die("Co-op pending SRAM does not match the current cartridge size");
    memcpy(g_sram,latest->bytes,latest->size);return true;
}
bool SmwCoopFinishStorage(void) {
    wait_worker();
    bool ok=true;
    for(StorageImage *image=first;image;image=image->next) {
        bool superseded=false;
        for(StorageImage *later=image->next;later;later=later->next)
            if(!strcmp(image->path,later->path)) {superseded=true;break;}
        if(superseded || image->success)continue;
        image->success=RtlWriteSramImage(image->path,image->bytes,image->size)!=0;
        if(!image->success)ok=false;
    }
    if(ok)release_images();
    return ok;
}
void SmwCoopStorageShutdown(bool capture_current) {
    for(;;) {
        bool queued=!capture_current || SmwCoopQueueStorage();
        if(queued)capture_current=false;
        if(queued && SmwCoopFinishStorage())return;
        const SDL_MessageBoxButtonData buttons[]={
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Retry"},
            {0,0,"Quit without saving"}
        };
        const SDL_MessageBoxData dialog={
            SDL_MESSAGEBOX_ERROR,NULL,"Super Mario World",
            "Could not save one or more save files. Your progress is still in memory.\n"
            "Check available disk space and file permissions, then retry.",
            2,buttons,NULL
        };
        int choice=1;
#if SNESRECOMP_SDL3
        bool shown=SDL_ShowMessageBox(&dialog,&choice);
#else
        bool shown=SDL_ShowMessageBox(&dialog,&choice)==0;
#endif
        if(!shown)Die("Could not save co-op SRAM or display the retry dialog");
        if(choice==0) {wait_worker();release_images();return;}
    }
}
