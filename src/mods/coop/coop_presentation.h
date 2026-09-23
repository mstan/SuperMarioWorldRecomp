#ifndef SMW_COOP_PRESENTATION_H
#define SMW_COOP_PRESENTATION_H
#include "coop_machine.h"
void SmwCoopCaptureVisual(CoopVisual *visual);
void SmwCoopCaptureMount(CoopMountVisual *visual,int x,int y);
void SmwCoopPresentationLatch(void);
void SmwCoopPresentationPrepare(void);
#endif
