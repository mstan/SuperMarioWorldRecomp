#pragma once
#include "types.h"

typedef struct Dsp Dsp;
typedef struct SpcPlayer SpcPlayer;

typedef void SpcPlayer_Initialize_Func(SpcPlayer *p);
typedef void SpcPlayer_Upload_Func(SpcPlayer *p, const uint8_t *data);


typedef struct SpcPlayer {
  Dsp *dsp;
  uint8 *ram;
  uint8 input_ports[4];
  uint8 port_to_snes[4];

  SpcPlayer_Initialize_Func *initialize;
  SpcPlayer_Upload_Func *upload;
} SpcPlayer;


SpcPlayer *SmwSpcPlayer_Create(void);

