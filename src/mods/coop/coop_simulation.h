#ifndef SMW_COOP_SIMULATION_H
#define SMW_COOP_SIMULATION_H
#include "cpu_state.h"
void SmwCoopSimulationBegin(void);
void SmwCoopSimulationEnd(void);
uint32_t SmwCoopSimulationHook(CpuState *cpu,uint32_t pc);
#endif
