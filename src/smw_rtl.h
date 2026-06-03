#ifndef SMW_SMW_RTL_H_
#define SMW_SMW_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

void RunOneFrameOfGame_Internal();

void SmwDrawPpuFrame(void);
void RunOneFrameOfGame(void);

#endif  // SMW_SMW_RTL_H_