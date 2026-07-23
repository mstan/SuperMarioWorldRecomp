#ifndef SMW_SMW_RTL_H_
#define SMW_SMW_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

void RunOneFrameOfGame_Internal();

void SmwDrawPpuFrame(void);
void RunOneFrameOfGame(void);

/* Clear process-lifetime LLE / frame gates before rematch SnesInit.
 * Soft-return leaves g_did_reset set; without this the next session skips
 * I_RESET on a fresh chip and black-screens under the interpreter floor. */
void SmwSessionReset(void);

#endif  // SMW_SMW_RTL_H_