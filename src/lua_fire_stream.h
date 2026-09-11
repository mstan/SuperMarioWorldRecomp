#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct Ppu Ppu;
void smw_fire_stream_init(uint8_t *ram, const uint8_t *rom, uint32_t rom_size);
void smw_fire_stream_tick(void);
void smw_fire_stream_draw(Ppu *ppu, uint8_t *pixels, size_t pitch, int width, int height);
int smw_fire_stream_command(const char *name, const char *args, char *out, size_t capacity);
