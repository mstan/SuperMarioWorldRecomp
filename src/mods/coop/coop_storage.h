#ifndef SMW_COOP_STORAGE_H
#define SMW_COOP_STORAGE_H
#include <stdbool.h>
/* Main-thread services. Workers only receive copied paths and SRAM images. */
bool SmwCoopQueueStorage(void);
bool SmwCoopReadQueuedStorage(void);
/* Wait for writes, retry the newest failed image per path, retain on failure. */
bool SmwCoopFinishStorage(void);
/* Called before SDL/guest teardown; actual write failures offer retry/quit. */
void SmwCoopStorageShutdown(bool capture_current);
#endif
