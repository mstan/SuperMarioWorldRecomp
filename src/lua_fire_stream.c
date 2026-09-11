/* Lua playground projectile pool. Each projectile executes SMW's original
 * extended-sprite routine on a private RAM bus and private CPU/stack. This
 * preserves native slopes, gravity and enemy-hit behavior without advancing
 * the main CPU/PPU clocks or putting extra objects in SNES OAM. Only persistent
 * enemy/score/SFX consequences are copied back; scratch, stack, OAM, and the
 * borrowed native slot never touch the live game. Rendering uses ROM-owned
 * tiles and palettes from the current PPU in a separate host overlay. */
#include "lua_fire_stream.h"
#include "snes/interp816.h"
#include "snes/ppu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL_SIZE 256
#define WRAM_SIZE 0x20000
#define FIELD_COUNT 12
static const unsigned fields[FIELD_COUNT] = {
    0x170b,0x1715,0x171f,0x1729,0x1733,0x173d,0x1747,
    0x1751,0x175b,0x1765,0x176f,0x1779
};
typedef struct Projectile {
    uint8_t field[FIELD_COUNT];
    uint8_t tile, attr, size, sx, sy, drawn;
    unsigned age;
} Projectile;
static Projectile pool[POOL_SIZE];
static uint8_t shadow[WRAM_SIZE], dirty[WRAM_SIZE];
static uint8_t *live;
static const uint8_t *cart;
static uint32_t cart_size;
static unsigned rate, credit, peak, active, ticks;
static unsigned long long spawned, dropped, collisions, opcodes;
static int failed;
static char error[192];

static void fault(const char *operation, uint32_t address) {
    if (!failed) snprintf(error, sizeof(error), "unsupported native fireball %s at $%06X", operation, address);
    failed = 1;
}
static int ram_address(uint32_t a) {
    if (a >= 0x7e0000 && a <= 0x7fffff) return (int)(a - 0x7e0000);
    if ((a & 0x7f0000) < 0x400000 && (a & 0xffff) < 0x2000) return (int)(a & 0x1fff);
    return -1;
}
static uint8_t read_bus(void *unused, uint32_t address) {
    (void)unused;
    int a = ram_address(address);
    if (a >= 0) return shadow[a];
    if ((address & 0xffff) >= 0x8000) {
        uint32_t offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff);
        if (offset < cart_size) return cart[offset];
    }
    fault("read", address); return 0;
}
static void write_bus(void *unused, uint32_t address, uint8_t value) {
    (void)unused;
    int a = ram_address(address);
    if (a < 0) { fault("write", address); return; }
    shadow[a] = value; dirty[a] = 1;
}
static int persistent(unsigned a) {
    /* Normal sprite state, score sprites, player score, and sound requests.
     * All other writes belong to the private execution context. */
    return (a >= 0x9e && a <= 0xef) ||
        (a >= 0x14c8 && a < 0x1692) ||
        (a >= 0x16e1 && a < 0x170b) ||
        (a >= 0x187b && a < 0x1887) ||
        (a >= 0x190f && a < 0x191b) ||
        (a >= 0x1fd6 && a < 0x1fee) ||
        (a >= 0x0f34 && a < 0x0f3a) ||
        (a >= 0x1df9 && a <= 0x1dfc);
}
static void reset(void) {
    memset(pool, 0, sizeof(pool));
    rate = credit = peak = active = ticks = 0;
    spawned = dropped = collisions = opcodes = 0;
    failed = 0; error[0] = 0;
}
void smw_fire_stream_init(uint8_t *ram, const uint8_t *rom, uint32_t size) {
    live = ram; cart = rom; cart_size = size; reset();
}
static unsigned word(const uint8_t *ram, unsigned a) { return ram[a] | ((unsigned)ram[a+1] << 8); }
static void spawn(unsigned subframe, unsigned count) {
    for (unsigned i = 0; i < POOL_SIZE; ++i) if (!pool[i].field[0]) {
        Projectile *p = &pool[i]; memset(p, 0, sizeof(*p));
        int right = live[0x76] != 0;
        int offset = (int)(subframe * 3 / count);
        unsigned x = (word(live,0x94) + (right ? 8+offset : -offset)) & 0xffff;
        unsigned y = (word(live,0x96)+8) & 0xffff;
        p->field[0] = 5;
        p->field[1] = y; p->field[2] = x; p->field[3] = y >> 8; p->field[4] = x >> 8;
        p->field[5] = 0x30; p->field[6] = right ? 3 : 0xfd;
        p->field[11] = live[0x13f9];
        ++spawned; return;
    }
    ++dropped;
}
static void update(Projectile *p, unsigned index) {
    for (unsigned j = 0; j < FIELD_COUNT; ++j) shadow[fields[j]+9] = p->field[j];
    shadow[0x15e9] = 9;
    /* Collision work in SMW is staggered modulo four using X xor $13.
     * Distinct virtual slots get distinct phases, while preserving frequency. */
    shadow[0x13] = (uint8_t)(live[0x13] + index);
    shadow[0x02fd] = 0xf0; /* no draw this tick unless native code emits OAM */
    Interp816 cpu; memset(&cpu, 0, sizeof(cpu));
    cpu.read = read_bus; cpu.write = write_bus;
    cpu.k = cpu.db = 2; cpu.pc = 0x9b16; cpu.x = 9;
    cpu.mf = cpu.xf = cpu.i = true; cpu.sp = 0x1fd;
    shadow[0x1fe] = 0xff; shadow[0x1ff] = 0x7f; /* balanced RTS sentinel */
    unsigned instructions;
    for (instructions = 0; instructions < 20000 && !failed; ++instructions) {
        interp816_runOpcode(&cpu);
        if (cpu.sp == 0x1ff && cpu.pc == 0x8000 && cpu.k == 2) break;
        if (cpu.waiting || cpu.stopped) { fault("wait/stop", ((uint32_t)cpu.k<<16)|cpu.pc); break; }
    }
    opcodes += instructions+1;
    if (instructions == 20000) fault("instruction limit", ((uint32_t)cpu.k<<16)|cpu.pc);
    for (unsigned j = 0; j < FIELD_COUNT; ++j) p->field[j] = shadow[fields[j]+9];
    p->sx = shadow[0x02fc]; p->sy = shadow[0x02fd];
    p->tile = shadow[0x02fe]; p->attr = shadow[0x02ff]; p->size = shadow[0x045f];
    p->drawn = p->field[0] && p->sy < 0xf0;
    if (++p->age > 600) p->field[0] = p->drawn = 0;
}
void smw_fire_stream_tick(void) {
    if (!live || failed) return;
    if (live[0x100] != 0x14 || live[0x109] != 0) {
        memset(pool, 0, sizeof(pool)); active = credit = 0; return;
    }
    if (live[0x9d] || live[0x13d4]) return;
    if (!rate && !active) return;
    ++ticks;
    if (rate && !live[0x71]) {
        credit += rate;
        unsigned count = credit / 60; credit %= 60;
        for (unsigned i = 0; i < count; ++i) spawn(i, count);
        if (count) {
            live[0x19] = 3; live[0x149c] = 10;
            if (ticks % 6 == 0) live[0x1dfc] = 6;
        }
    }
    memcpy(shadow, live, sizeof(shadow)); memset(dirty, 0, sizeof(dirty));
    active = 0;
    for (unsigned i = 0; i < POOL_SIZE; ++i) if (pool[i].field[0]) {
        update(&pool[i], i);
        if (failed) { rate = 0; fprintf(stderr,"[lua fire stream] %s\n",error); return; }
        if (pool[i].field[0]) ++active;
    }
    for (unsigned a = 0; a < WRAM_SIZE; ++a) if (dirty[a] && persistent(a)) {
        if (a >= 0x9e && a < 0xaa && live[a] != 0x21 && shadow[a] == 0x21) ++collisions;
        live[a] = shadow[a];
    }
    if (active > peak) peak = active;
}
int smw_fire_stream_command(const char *name, const char *args, char *out, size_t capacity) {
    if (!strcmp(name,"__reset") || !strcmp(name,"fire_stream_reset")) { reset(); snprintf(out,capacity,"reset"); return 1; }
    if (!strcmp(name,"fire_stream")) {
        char *end; long requested = strtol(args,&end,10);
        if (end == args || *end || requested < 0 || requested > 1000) { snprintf(out,capacity,"rate must be 0..1000 fireballs/second"); return 0; }
        if (failed) { snprintf(out,capacity,"%s; reset the stream first",error); return 0; }
        rate = (unsigned)requested; credit = 0;
    } else if (strcmp(name,"fire_stream_status")) { snprintf(out,capacity,"unknown SMW command: %s",name); return 0; }
    snprintf(out,capacity,"rate=%u spawned=%llu active=%u peak=%u dropped=%llu hits=%llu ticks=%u opcodes=%llu error=%s",
        rate,spawned,active,peak,dropped,collisions,ticks,opcodes,error);
    return 1;
}
void smw_fire_stream_draw(Ppu *ppu, uint8_t *pixels, size_t pitch, int width, int height) {
    if (!live || failed || live[0x100] != 0x14 || !pixels) return;
    for (unsigned i = 0; i < POOL_SIZE; ++i) {
        const Projectile *p = &pool[i];
        if (!p->drawn) continue;
        int size = (p->size & 2) ? 16 : 8;
        int x = p->sx + (width-256)/2, y = p->sy;
        unsigned base = p->attr & 1 ? PPU_objTileAdr2(ppu) : PPU_objTileAdr1(ppu);
        unsigned palette = 128 + ((p->attr >> 1) & 7)*16;
        for (int dy = 0; dy < size; ++dy) for (int dx = 0; dx < size; ++dx) {
            int sx = x+dx, sy = y+dy;
            if (sx < 0 || sx >= width || sy < 0 || sy >= height) continue;
            int tx = (p->attr & 0x40) ? size-1-dx : dx;
            int ty = (p->attr & 0x80) ? size-1-dy : dy;
            unsigned tile = (p->tile & 0xf0) | ((p->tile + tx/8) & 15);
            tile = (tile + (ty/8)*16) & 255;
            unsigned at = (base + tile*16 + (ty & 7)) & 0x7fff;
            unsigned a = PpuRenderVram(ppu)[at], b = PpuRenderVram(ppu)[(at+8)&0x7fff];
            unsigned bit = 7-(tx&7);
            unsigned color = ((a>>bit)&1) | (((a>>(bit+8))&1)<<1) | (((b>>bit)&1)<<2) | (((b>>(bit+8))&1)<<3);
            if (!color) continue;
            unsigned rgb = ppu->cgram[palette+color];
            unsigned red = (rgb&31)*255/31, green = ((rgb>>5)&31)*255/31, blue = ((rgb>>10)&31)*255/31;
            ((uint32_t*)(pixels+(size_t)sy*pitch))[sx] = 0xff000000u | (red<<16) | (green<<8) | blue;
        }
    }
}
