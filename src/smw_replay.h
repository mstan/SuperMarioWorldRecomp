#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Host-only diagnostics. Never part of guest saves or rollback state. */
void SmwReplayRomLoaded(const uint8_t *rom, unsigned size);
void SmwReplaySetConfigPath(const char *path);
void SmwReplayStateLoaded(void);
void SmwReplayShutdown(void);
bool SmwReplayBegin(uint32_t *inputs);
void SmwReplayEnd(void);
void SmwReplayRendered(void);
#include "smw_renderer.h"
void SmwReplayViewport(SmwViewport *view);
bool SmwReplayPlayback(const char *path);
bool SmwReplayPlaying(void);
bool SmwReplayFinished(void);
bool SmwReplayFailed(void);

void SmwReplayEnable(bool enabled);
void SmwReplayClose(void);
const char *SmwReplayPath(void);
