#ifndef SMW_COOP_RUNTIME_H
#define SMW_COOP_RUNTIME_H
#include "coop_machine.h"
#include "cpu_state.h"
#include "snes/saveload.h"

void SmwCoopEnable(bool enabled);
bool SmwCoopEnabled(void);
bool SmwCoopActive(void);
CoopMachine *SmwCoopMachine(void);
bool SmwCoopInstallHooks(void);
/* Before the initial SRAM read; false means no namespace has been selected. */
bool SmwCoopPrepareStorage(void);
bool SmwCoopPersistenceReady(void);
bool SmwCoopFrozen(void);
void SmwCoopResetSession(void);
/* Input assignment/disconnect processing. True advances this gameplay frame. */
bool SmwCoopHostFrame(uint32_t inputs);
/* Zero leaves the guest instruction intact; otherwise tail-transfer to PC. */
uint32_t SmwCoopGuestHook(CpuState *cpu,uint32_t pc);
const char *SmwCoopSnapshotIdentity(void);
bool SmwCoopSnapshotPreflight(const void *data,size_t size);
void SmwCoopSaveExtra(SaveLoadInfo *sli);
void SmwCoopLoadExtra(SaveLoadInfo *sli);
void SmwCoopStateLoaded(void);
#endif
