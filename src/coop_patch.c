#include "coop_patch.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "crc32.h"

enum {
  kStockRomSize = 512 * 1024,
  kCoopRomSize = 1024 * 1024,
};

static const uint32_t kStockRomCrc32 = 0xB19ED489u;
static const uint32_t kCoopRomCrc32 = 0x05D84AE1u;

/*
 * The hack replaces ProcessGameMode with a Lunar Magic ExecutePtrLong
 * trampoline.  That trampoline deliberately constructs two nested return
 * frames before tail-dispatching through $80:86DF.  Replaying the trampoline
 * literally makes those guest frames straddle several generated C functions,
 * which obscures the paired-host-call boundary and can make an RTL continuation
 * restore the wrong stack watermark.
 *
 * Preserve the exact guest-visible operation with explicit paired calls: read
 * the stock game-mode vector selected by the wrapper, run that handler with an
 * ordinary two-byte return frame, then enter the hack's post-dispatch routine
 * using the original ProcessGameMode caller frame already on the stack.  The
 * latter routine performs the simultaneous-player update and consumes that
 * original frame itself, just as the ROM's PLA/PHY/PHA/RTL adapter does.
 */
RecompReturn HleCoopGameModeWrapper(CpuState *cpu) {
  /* ProcessGameMode and the Lunar Magic wrapper both have an M1X1 ABI. The
   * wrapper's synthetic stack trampoline made this implicit to the analyzer;
   * establish it explicitly before reading the 8-bit mode and dispatching. */
  cpu->P = (uint8)(cpu->P | 0x30u);
  cpu->m_flag = 1;
  cpu->x_flag = 1;
  cpu->X &= 0x00FFu;
  cpu->Y &= 0x00FFu;

  const uint8 mode = cpu_read8(cpu, cpu->DB, 0x0100);
  const uint16 handler =
      cpu_read16(cpu, 0x80, (uint16)(0x9329u + (uint16)mode * 2u));
  const uint8 saved_pb = cpu->PB;

  cpu->PB = 0x80;
  RecompReturn result =
      cpu_dispatch_call_pc(cpu, 0x800000u | handler, 0x8084CCu);
  if (result != RECOMP_RETURN_NORMAL) {
    cpu->PB = saved_pb;
    return result;
  }

  /* Every stock game-mode handler returns M1X1. Reassert that contract at the
   * synthetic continuation so its immediates and one-byte PHY are decoded as
   * the ROM intends even after a conservative interpreter-tier call. */
  cpu->P = (uint8)(cpu->P | 0x30u);
  cpu->m_flag = 1;
  cpu->x_flag = 1;
  cpu->X &= 0x00FFu;
  cpu->Y &= 0x00FFu;
  cpu->PB = 0x90;
  result = cpu_dispatch_pc_paired(cpu, 0x908051u, 2);
  if (result != RECOMP_RETURN_NORMAL) {
    cpu->PB = saved_pb;
    return result;
  }

  /* The ROM adapter returns through the $80 FastROM mirror of bank $00. */
  cpu->PB = 0x00;
  return RECOMP_RETURN_NORMAL;
}

/*
 * The co-op patch installs a JSL target at $10:8008 which waits for both
 * H-blank and V-blank to end before enabling NMI/auto-joypad.  A recompiled
 * tight loop cannot observe the PPU status changing while it owns the host
 * thread, so reproduce the state at the loop's eventual exit and its RTL.
 */
RecompReturn HleCoopWaitForBlank(CpuState *cpu) {
  const uint8 value = 0x81;
  const uint8 hvbjoy_at_exit =
      (uint8)(cpu_read8(cpu, cpu->DB, 0x4212) & 0x3Fu);

  cpu_write_a_m(cpu, value);
  cpu->_flag_N = 0;
  cpu->_flag_V = 0;
  cpu->_flag_Z = ((value & hvbjoy_at_exit) == 0) ? 1 : 0;
  cpu->P = (uint8)((cpu->P & (uint8)~0xC2u) |
                   (cpu->_flag_Z ? 0x02u : 0u));
  cpu_write8(cpu, cpu->DB, 0x4200, value);

  /* The generated HLE forwarding stub replaces the routine's RTL, so it
   * must consume the three-byte JSL return frame itself. */
  cpu->S = (uint16)(cpu->S + 3u);
  return RECOMP_RETURN_NORMAL;
}

/*
 * $90:8094 is the wrapper's five-byte JSR-to-RTL return adapter:
 *
 *   PHY / PHA / SEP #$20 / RTL
 *
 * The preceding code has already pulled the original two-byte JSR return
 * address into A and advanced S.  On hardware the adapter pushes that address
 * back as an $80-bank long frame and immediately consumes it with RTL.  At the
 * C boundary those stack operations cancel, so apply the lasting CPU effects
 * and return to the paired host caller directly. The hardware target is the
 * $80 FastROM mirror of bank $00; normalize it to $00 so SMW's LLE frame
 * scheduler recognizes its canonical $00:806B yield address.
 */
RecompReturn HleCoopReturnToHost(CpuState *cpu) {
  cpu->P = (uint8)(cpu->P | 0x20u);
  cpu->m_flag = 1;
  cpu->PB = 0x00;
  return RECOMP_RETURN_NORMAL;
}

static RecompReturn HleCoopLevelInitConditional(CpuState *cpu,
                                                 uint32 continuation) {
  const uint8 value = (uint8)(cpu_read8(cpu, cpu->DB, 0x1928u) & 0x10u);
  cpu_write_a_m(cpu, value);
  cpu->_flag_Z = value == 0;
  cpu->_flag_N = 0;
  cpu->P = (uint8)((cpu->P & (uint8)~0x82u) |
                   (cpu->_flag_Z ? 0x02u : 0u));

  if (value != 0) {
    /* This is the helper's RTL through the parent routine's JSL frame. */
    cpu->P = (uint8)(cpu->P | 0x30u);
    cpu->m_flag = 1;
    cpu->x_flag = 1;
    cpu->X &= 0x00FFu;
    cpu->Y &= 0x00FFu;
    cpu->S = (uint16)(cpu->S + 3u);
    return RECOMP_RETURN_NORMAL;
  }

  /* The original branch falls through with the parent JSL frame still on the
   * guest stack. Pair that existing frame with the generated continuation. */
  const uint8 saved_pb = cpu->PB;
  cpu->PB = 0x1Fu;
  RecompReturn result = cpu_dispatch_pc_paired(cpu, continuation, 3);
  cpu->PB = saved_pb;
  return result;
}

RecompReturn HleCoopLevelInitConditionalAfterA7D9(CpuState *cpu) {
  return HleCoopLevelInitConditional(cpu, 0x1FAC9Bu);
}

RecompReturn HleCoopLevelInitConditionalAfterA84D(CpuState *cpu) {
  return HleCoopLevelInitConditional(cpu, 0x1FB04Au);
}

/*
 * The patched VRAM upload loop is a cross-bank JML cycle:
 *   $00:8726 -> $1F:A485 -> $00:8726
 * Native code represents those JMLs as tail calls, which would recurse once
 * per DMA row. The interpreter-tail bridge owns the cycle and converts a
 * nested trip back to $00:8726 into an unwind/resume instead.
 */
RecompReturn HleCoopVramUploadLoop(CpuState *cpu) {
  return interp_tier_dispatch_tail(cpu, 0x008726u, 0x008726u,
                                   cpu->S, cpu->host_return_valid);
}

RecompReturn HleCoopProcessNormalSprites(CpuState *cpu) {
  return interp_tier_run_call_frame(cpu, 0x01808Cu, 0x00986Cu, 3, NULL);
}

RecompReturn HleCoopInitializeLevelTilemaps(CpuState *cpu) {
  /* These Lunar Magic dispatch roots contain inline pointer tables and their
   * selected handlers RTL through the caller's frame. Keeping the roots in
   * one interpreter frame prevents a handler return from bouncing into the
   * published $05 continuation as a new nested host call. */
  static const uint32 dispatch_roots[] = {
      0x1FA5CFu,
      0x1FA7D9u,
      0x1FA84Du,
  };
  interp_bridge_set_lle_bounce_exclusions(
      dispatch_roots, sizeof(dispatch_roots) / sizeof(dispatch_roots[0]));
  return interp_tier_run_call_frame(cpu, 0x05809Eu, 0x00A5ABu, 3, NULL);
}

static void SetError(char *error, size_t error_size, const char *format, ...) {
  if (!error || error_size == 0) return;
  va_list args;
  va_start(args, format);
  vsnprintf(error, error_size, format, args);
  va_end(args);
}

static uint8_t *ReadFile(const char *path, size_t *size_out) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  uint8_t *data = (uint8_t *)malloc((size_t)length ? (size_t)length : 1);
  if (!data) { fclose(file); return NULL; }
  if ((size_t)length != fread(data, 1, (size_t)length, file)) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *size_out = (size_t)length;
  return data;
}

static int EnsureCapacity(uint8_t **rom, size_t *capacity, size_t needed) {
  if (needed <= *capacity) return 1;
  if (needed > 16u * 1024u * 1024u) return 0;
  uint8_t *grown = (uint8_t *)realloc(*rom, needed);
  if (!grown) return 0;
  memset(grown + *capacity, 0, needed - *capacity);
  *rom = grown;
  *capacity = needed;
  return 1;
}

static int ApplyIps(uint8_t **rom, size_t *rom_size,
                    const uint8_t *ips, size_t ips_size,
                    char *error, size_t error_size) {
  if (ips_size < 8 || memcmp(ips, "PATCH", 5) != 0) {
    SetError(error, error_size, "Co-op patch is not a valid IPS file");
    return 0;
  }

  size_t pos = 5;
  while (1) {
    if (pos + 3 > ips_size) {
      SetError(error, error_size, "Co-op IPS ended before its EOF marker");
      return 0;
    }
    if (memcmp(ips + pos, "EOF", 3) == 0) break;
    if (pos + 5 > ips_size) {
      SetError(error, error_size, "Co-op IPS has a truncated record header");
      return 0;
    }
    size_t offset = ((size_t)ips[pos] << 16) |
                    ((size_t)ips[pos + 1] << 8) | ips[pos + 2];
    size_t length = ((size_t)ips[pos + 3] << 8) | ips[pos + 4];
    pos += 5;

    if (length == 0) {
      if (pos + 3 > ips_size) {
        SetError(error, error_size, "Co-op IPS has a truncated RLE record");
        return 0;
      }
      length = ((size_t)ips[pos] << 8) | ips[pos + 1];
      uint8_t value = ips[pos + 2];
      pos += 3;
      if (!length || offset > SIZE_MAX - length ||
          !EnsureCapacity(rom, rom_size, offset + length)) {
        SetError(error, error_size, "Co-op IPS contains an invalid RLE range");
        return 0;
      }
      memset(*rom + offset, value, length);
    } else {
      if (pos + length > ips_size || offset > SIZE_MAX - length ||
          !EnsureCapacity(rom, rom_size, offset + length)) {
        SetError(error, error_size, "Co-op IPS contains an invalid data range");
        return 0;
      }
      memcpy(*rom + offset, ips + pos, length);
      pos += length;
    }
  }
  return 1;
}

static int IsValidCoopOutput(const char *path) {
  size_t size = 0;
  uint8_t *data = ReadFile(path, &size);
  if (!data) return 0;
  int valid = size == kCoopRomSize && crc32_compute(data, size) == kCoopRomCrc32;
  free(data);
  return valid;
}

int CoopPreparePatchedRom(const char *stock_path, const char *ips_path,
                          const char *output_path,
                          char *error, size_t error_size) {
  if (error && error_size) error[0] = '\0';
  if (!stock_path || !ips_path || !output_path) {
    SetError(error, error_size, "Missing co-op patch path");
    return 0;
  }
  if (IsValidCoopOutput(output_path)) {
    printf("[Co-op] Reusing patched ROM: %s\n", output_path);
    return 1;
  }

  size_t raw_size = 0;
  uint8_t *raw = ReadFile(stock_path, &raw_size);
  if (!raw) {
    SetError(error, error_size, "Unable to read stock ROM: %s", stock_path);
    return 0;
  }
  size_t header = raw_size == kStockRomSize + 512 ? 512 : 0;
  if (raw_size - header != kStockRomSize ||
      crc32_compute(raw + header, raw_size - header) != kStockRomCrc32) {
    free(raw);
    SetError(error, error_size,
             "The selected ROM is not Super Mario World (USA), CRC32 B19ED489");
    return 0;
  }

  uint8_t *rom = (uint8_t *)malloc(kStockRomSize);
  if (!rom) { free(raw); SetError(error, error_size, "Out of memory"); return 0; }
  memcpy(rom, raw + header, kStockRomSize);
  free(raw);
  size_t rom_size = kStockRomSize;

  size_t ips_size = 0;
  uint8_t *ips = ReadFile(ips_path, &ips_size);
  if (!ips) {
    free(rom);
    SetError(error, error_size, "Unable to read bundled co-op patch: %s", ips_path);
    return 0;
  }
  int patched = ApplyIps(&rom, &rom_size, ips, ips_size, error, error_size);
  free(ips);
  if (!patched) { free(rom); return 0; }
  if (rom_size != kCoopRomSize || crc32_compute(rom, rom_size) != kCoopRomCrc32) {
    free(rom);
    SetError(error, error_size, "Bundled co-op patch produced an unexpected ROM");
    return 0;
  }

  char temporary[1200];
  if (snprintf(temporary, sizeof(temporary), "%s.tmp", output_path) >=
      (int)sizeof(temporary)) {
    free(rom);
    SetError(error, error_size, "Co-op output path is too long");
    return 0;
  }
  FILE *out = fopen(temporary, "wb");
  if (!out) {
    free(rom);
    SetError(error, error_size, "Unable to create co-op ROM: %s", output_path);
    return 0;
  }
  size_t written = fwrite(rom, 1, rom_size, out);
  int close_ok = fclose(out) == 0;
  int wrote = written == rom_size && close_ok;
  free(rom);
  if (!wrote) {
    remove(temporary);
    SetError(error, error_size, "Unable to finish writing co-op ROM: %s", output_path);
    return 0;
  }
  remove(output_path);
  if (rename(temporary, output_path) != 0) {
    remove(temporary);
    SetError(error, error_size, "Unable to install co-op ROM: %s", output_path);
    return 0;
  }
  printf("[Co-op] Patched stock ROM -> %s\n", output_path);
  return 1;
}
