#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "debug_server.h"
#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"
#include "snes/interp_bridge.h"

#include "types.h"
#include "smw_rtl.h"
#include "common_cpu_infra.h"
#include "framedump.h"
#include "config.h"
#include "util.h"
#include "smw_spc_player.h"

#include "snes/snes.h"
#ifdef __SWITCH__
#include "switch_impl.h"
#endif

#include "launcher.h"
#if defined(SNES_LAUNCHER) || defined(RECOMP_LAUNCHER)
#if defined(RECOMP_LAUNCHER)
/* Shared recomp-ui launcher (F:\Projects\recomp-ui) — the console-agnostic
 * extraction of launcher_ng, consumed as a junction/submodule. recomp_ui.cmake
 * defines RECOMP_LAUNCHER. SMW drives it as the SNES profile
 * (launcher_profile_apply("snes", ...)). */
#include "recomp_launcher.h"   /* recomp_launcher_run_window() */
#include "launcher_profile.h"  /* launcher_profile_apply("snes", &gi) — SNES identity */
#elif defined(SNES_LAUNCHER)
#include "launcher/launcher_capi.h"
#endif
#endif
#include "keybinds.h"
#include "host_report.h"

typedef struct GamepadInfo {
  uint32 modifiers;
  SDL_JoystickID joystick_id;
  uint8 index;
  uint8 axis_buttons;
  uint16 last_cmd[kGamepadBtn_Count];
  Sint16 last_axis_x, last_axis_y;
} GamepadInfo;


static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len);
static void EnsureConfigIni(void);
static void RenderNumber(uint8 *dst, size_t pitch, int n, uint8 big);
static void OpenOneGamepad(int i);
static uint32 GetActiveControllers(void);
static void HandleVolumeAdjustment(int volume_adjustment);
static void HandleGamepadAxisInput(GamepadInfo *gi, int axis, Sint16 value);
static int RemapSdlButton(int button);
static void HandleGamepadInput(GamepadInfo *gi, int button, bool pressed);
static void HandleInput(int keyCode, int keyMod, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
void OpenGLRenderer_Create(struct RendererFuncs *funcs);

bool g_new_ppu = true;

struct SpcPlayer *g_spc_player;

// Sized for the widescreen capacity (256 + 2*kPpuExtraLeftRight). With
// widescreen off the PPU only writes the leading 256 columns, so this is
// authentic-identical; the extra capacity is just unused tail.
static uint8_t g_my_pixels[kPpuBufWidth * 4 * 240];

// Widescreen border, in PPU columns per side (0 = authentic 256-wide).
// Computed once from config at startup; re-applied to the PPU every frame
// (ppu_reset zeroes the PPU's copy). g_snes_width = 256 + 2*g_ws_extra.
// Non-static: the game-logic override layer externs this to widen the sprite
// spawn/cull window to match the extended view.
int g_ws_extra = 0;

// Runtime widescreen master switch, read by the game-logic override layer
// (overrides/widescreen/*.c) and the dispatch prologues apply_overrides.py
// injects into the generated banks. Canonical definition lives here so the
// override sources can just `extern bool g_ws_active;` without a mandatory
// new translation unit. False = authentic SMW behaviour.
bool g_ws_active = false;

// Hard cap on the border width, from the SNES 9-bit OAM x space: the PPU
// wrap threshold is 256+extra, and left-margin tiles (down to screen-x
// -(64+extra), 64 = widest-sprite cushion) live at 512-(64+extra)..511,
// which must stay >= the threshold: 2*extra <= 192. Beyond this, sprites
// in the outer margin are unrepresentable (the 16:9 -> ~2.0:1 range is
// covered; true 21:9 is not). The game-logic override snippets clamp to
// the same value.
enum { kWsExtraMax = 95 };


enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

/* Release stamp baked in by make_release.ps1 via
 * /p:SnesRecompBuildVersion=<ver> (vcxproj turns the MSBuild property
 * into this define). Local/IDE builds report "dev"; the post-mortem
 * report's build.pe_timestamp still uniquely identifies those. */
#ifndef SNESRECOMP_BUILD_VERSION
#define SNESRECOMP_BUILD_VERSION "dev"
#endif

static const char kWindowTitle[] = "Super Mario World (Recompiled)";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_cursor = true;
static uint8 g_current_window_scale;
static uint32 g_input_state;
/* Gamepad-driven SNES controller bits, kept separate from g_input_state
 * (keyboard) so the per-frame keybinds.ini polling at the top of the
 * main loop doesn't clear bits the gamepad just set. OR'd into `inputs`
 * once per frame alongside g_input_state and axis_buttons. */
static uint32 g_pad_buttons;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;

static GamepadInfo g_gamepad[2];

extern Snes *g_snes;

// --- Scripted input ---
typedef struct {
  uint32 mask;      // button bits to hold
  int hold_frames;  // frames to hold mask (0 = release)
  int wait_frames;  // frames to wait after hold ends before next entry
} ScriptEntry;

static ScriptEntry *g_script_entries;
static int g_script_count;
static int g_script_index;    // current entry
static int g_script_phase;    // 0=holding, 1=waiting
static int g_script_counter;  // frames left in current phase

static uint32 ParseButtonMask(const char *name) {
  if (strcmp(name, "start")  == 0) return 0x0008;
  if (strcmp(name, "select") == 0) return 0x0004;
  if (strcmp(name, "up")     == 0) return 0x0010;
  if (strcmp(name, "down")   == 0) return 0x0020;
  if (strcmp(name, "left")   == 0) return 0x0040;
  if (strcmp(name, "right")  == 0) return 0x0080;
  if (strcmp(name, "a")      == 0) return 0x0100;
  if (strcmp(name, "b")      == 0) return 0x0001;
  if (strcmp(name, "x")      == 0) return 0x0200;
  if (strcmp(name, "y")      == 0) return 0x0002;
  if (strcmp(name, "l")      == 0) return 0x0400;
  if (strcmp(name, "r")      == 0) return 0x0800;
  fprintf(stderr, "script: unknown button '%s'\n", name);
  return 0;
}

static void LoadScript(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { fprintf(stderr, "script: cannot open '%s'\n", path); return; }

  // Two-pass: count then fill
  int cap = 64;
  g_script_entries = (ScriptEntry *)malloc(cap * sizeof(ScriptEntry));
  g_script_count = 0;

  char line[256];
  // pending wait accumulates between press commands
  int pending_wait = 0;
  while (fgets(line, sizeof(line), f)) {
    // strip comment and newline
    char *c = strchr(line, '#'); if (c) *c = 0;
    char cmd[64], arg1[64];
    int n = 0;
    if (sscanf(line, "%63s %63s %d", cmd, arg1, &n) < 1) continue;
    if (strcmp(cmd, "wait") == 0) {
      int frames = (sscanf(line, "%*s %d", &n) == 1) ? n : 0;
      pending_wait += frames;
    } else if (strcmp(cmd, "loadstate") == 0) {
      // loadstate N — load savestate slot N (0-indexed, F1=0)
      int slot = 0;
      sscanf(line, "%*s %d", &slot);
      if (g_script_count >= cap) {
        cap *= 2;
        g_script_entries = (ScriptEntry *)realloc(g_script_entries, cap * sizeof(ScriptEntry));
      }
      ScriptEntry *e = &g_script_entries[g_script_count++];
      e->mask = 0x80000000 | (slot & 0xF);  // special flag: high bit = loadstate
      e->hold_frames = 1;
      e->wait_frames = pending_wait;
      pending_wait = 0;
    } else if (strcmp(cmd, "press") == 0) {
      int hold = (sscanf(line, "%*s %*s %d", &n) == 1) ? n : 1;
      if (g_script_count >= cap) {
        cap *= 2;
        g_script_entries = (ScriptEntry *)realloc(g_script_entries, cap * sizeof(ScriptEntry));
      }
      ScriptEntry *e = &g_script_entries[g_script_count++];
      e->mask = ParseButtonMask(arg1);
      e->hold_frames = hold;
      e->wait_frames = pending_wait;
      pending_wait = 0;
    }
  }
  fclose(f);

  if (g_script_count > 0) {
    g_script_index = 0;
    g_script_phase = 1; // start with the wait_frames of first entry
    g_script_counter = g_script_entries[0].wait_frames;
    fprintf(stderr, "script: loaded %d entries from '%s'\n", g_script_count, path);
  }
}

static uint32 TickScript(void) {
  if (!g_script_entries || g_script_index >= g_script_count)
    return 0;

  ScriptEntry *e = &g_script_entries[g_script_index];

  if (g_script_phase == 1) {
    // waiting
    if (g_script_counter > 0) { g_script_counter--; return 0; }
    // done waiting — start hold
    g_script_phase = 0;
    g_script_counter = e->hold_frames;
  }

  if (g_script_phase == 0) {
    if (g_script_counter > 0) {
      g_script_counter--;
      if (e->mask & 0x80000000) {
        // loadstate command
        RtlSaveLoad(kSaveLoad_Load, e->mask & 0xF);
        return 0;
      }
      return e->mask;
    }
    // hold done — advance
    g_script_index++;
    if (g_script_index < g_script_count) {
      e = &g_script_entries[g_script_index];
      g_script_phase = 1;
      g_script_counter = e->wait_frames;
    }
    return 0;
  }
  return 0;
}

void NORETURN Die(const char *error) {
  /* Record the message before exiting: the atexit post-mortem dump
   * includes it and preserves a timestamped crash copy (see
   * host_report_has_fatal in post_mortem.c). */
  host_report_fatal(error);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

static GamepadInfo *GetGamepadInfo(SDL_JoystickID id) {
  return (g_gamepad[0].joystick_id == id) ? &g_gamepad[0] :
    (g_gamepad[1].joystick_id == id) ? &g_gamepad[1] : NULL;
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
      (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
      (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

void RtlDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  // Re-apply the widescreen border every frame (ppu_reset zeroes the PPU's
  // copy on soft reset / load-state). Bounded screens — overworld, title,
  // menus, transitions — have no valid BG past the authentic 256-wide view,
  // so the border would show garbage. Restrict the widescreen border to actual
  // in-level gameplay (misc_game_mode == 0x14) and pillarbox everything else.
  size_t row_bytes = (size_t)g_snes_width * 4;
  if (g_ws_extra > 0) {
    bool in_level = (g_ram[0x100] == 0x14);  // misc_game_mode: level main routine
    if (in_level) {
      PpuSetExtraSpace(g_ppu, (uint8_t)g_ws_extra);
      // HUD split: SMW's status bar lives on BG3 rows 1-4 (scanlines < 40).
      // Left chunk = MARIO + lives (cols 2-6, x < 56) anchored to the left
      // edge. Center chunk = the naturally adjacent middle group — yoshi
      // coins (cols 8-11), bonus stars (9-13), item box frame (14-17; the
      // boxed item is an OAM sprite and stays with it), TIME label + timer
      // (19-21) — x 56..183, kept centered. Right chunk = score (23-28) +
      // coin counter (25-29), x >= 184, anchored to the right edge. Both
      // outer chunks keep their authentic 16px inset from the screen edge.
      // Message boxes render lower on BG3 and are unaffected.
      PpuSetWidescreenHudSplit(g_ppu, g_config.widescreen_hud ? 40 : 0, 56, 184);
      // The status bar occupies BG3 scanlines < 40; below it, level content on
      // BG3 (water surface/body tiles) must fill the widescreen margins like
      // BG1/BG2 instead of stopping at the authentic 256-wide edge.
      PpuSetWidescreenBg3Widen(g_ppu, 40);
    } else {
      // Center the authentic 256 view and black out the side margins.
      PpuSetExtraSpaceCentered(g_ppu, (uint8_t)g_ws_extra);
      memset(g_my_pixels, 0, row_bytes * g_snes_height);
    }
  }
  g_rtl_game_info->draw_ppu_frame();
  for (size_t y = 0, y_end = g_snes_height; y < y_end; y++)
    memcpy((uint8 *)pixel_buffer + y * pitch, g_my_pixels + y * row_bytes, row_bytes);
}

static void DrawPpuFrameWithPerf(void) {
  int render_scale = PpuGetCurrentRenderScale(g_ppu, g_ppu_render_flags);
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    RtlDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    RtlDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
  }
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);

  g_renderer_funcs.EndDraw();
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;
static SDL_AudioDeviceID g_audio_device;

void RtlApuLock(void) {
  SDL_LockMutex(g_audio_mutex);
}

void RtlApuUnlock(void) {
  SDL_UnlockMutex(g_audio_mutex);
}

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  /* Boot-stage marker: proves the audio thread reached the mixer at
   * least once (the "crashed before the first sound" class of report). */
  static SDL_atomic_t first_cb;
  if (SDL_AtomicCAS(&first_cb, 0, 1))
    host_report_breadcrumb("first audio callback (len=%d)", len);
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      SDL_UnlockMutex(g_audio_mutex);
      RtlRenderAudio((int16 *)g_audiobuffer, g_frames_per_block, g_audio_channels);
      if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }
  SDL_UnlockMutex(g_audio_mutex);
}


// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;

static bool SdlRenderer_Init(SDL_Window *window) {
  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (Uint32 i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = 1;
  // Texture at the full widescreen capacity so dynamic resizes never need a
  // texture recreate; BeginDraw locks the current-width subrect.
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                (256 + 2 * kPpuExtraLeftRight) * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void SdlRenderer_Destroy(void) {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_GetOutputSize(int *width, int *height) {
  if (SDL_GetRendererOutputSize(g_renderer, width, height) != 0) {
    *width = 0;
    *height = 0;
  }
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw(void) {
  //  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
  //  uint64 after = SDL_GetPerformanceCounter();
  //  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
  //  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

// Dynamic widescreen: derive the border width from the actual window
// aspect so the game view follows resizes (and fullscreen on wide
// monitors), clamped to the 9-bit OAM budget (kWsExtraMax) and the PPU's
// compiled capacity. Every consumer is already per-frame parameterized on
// g_ws_extra (PPU margins, window edges, and the game-logic spawn/cull
// snippets read it on each call), so all that changes here is the
// presentation plumbing: the PPU draw pitch and the SDL logical size.
// No-op when widescreen is off or the width is already right.
static void WidescreenUpdateForWindow(int win_w, int win_h) {
  if (!g_config.widescreen || win_w <= 0 || win_h <= 0)
    return;
  int target_w = (win_w * g_snes_height + win_h / 2) / win_h;
  int extra = (target_w - 256) / 2;
  int cap = IntMin(kPpuExtraLeftRight, kWsExtraMax);
  extra = IntMax(0, IntMin(extra, cap));
  if (extra == g_ws_extra)
    return;
  g_ws_extra = extra;
  g_snes_width = 256 + 2 * extra;
  g_ws_active = (extra > 0);
  PpuBeginDrawing(g_ppu, g_my_pixels, (size_t)g_snes_width * 4, 0);
  // Apply immediately: RtlDrawPpuFrame's per-frame re-apply is gated on
  // g_ws_extra > 0, so shrinking to exactly 4:3 would otherwise leave the
  // PPU's old margins live.
  PpuSetExtraSpace(g_ppu, (uint8_t)extra);
  if (g_renderer && !g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(g_renderer, g_snes_width, g_snes_height);
}

static const struct RendererFuncs kSdlRendererFuncs = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_GetOutputSize,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};


void MkDir(const char *s) {
#if defined(_WIN32)
  _mkdir(s);
#else
  mkdir(s, 0755);
#endif
}

#include <signal.h>
#include "cpu_state.h"
#include "cpu_trace.h"
#include "post_mortem.h"
extern uint8_t g_ram[0x20000];
static void dump_sprite_state(void) {
  // Dump SMW sprite-state arrays so dispatch-OOB crashes name the offending slot.
  fprintf(stderr, "Sprite state at crash:\n");
  fprintf(stderr, "  $9E (sprite type)   :");
  for (int k = 0; k < 12; k++) fprintf(stderr, " %02x", g_ram[0x9e + k]);
  fprintf(stderr, "\n  $14C8 (status)      :");
  for (int k = 0; k < 12; k++) fprintf(stderr, " %02x", g_ram[0x14c8 + k]);
  fprintf(stderr, "\n  $0100 (GameMode)    : %02x\n", g_ram[0x100]);
  fprintf(stderr, "  $7F:8000 (init sig) : %02x %02x\n", g_ram[0x18000], g_ram[0x18001]);
  fprintf(stderr, "  v2 CpuState: A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X PB=%02X "
                  "P=%02X m=%u x=%u e=%u\n",
                  g_cpu.A, g_cpu.X, g_cpu.Y, g_cpu.S, g_cpu.D, g_cpu.DB, g_cpu.PB,
                  g_cpu.P, g_cpu.m_flag, g_cpu.x_flag, g_cpu.emulation);
}
static void crash_handler(int sig) {
  extern const char *g_last_recomp_func;
  extern void RecompStackDump(void);
  fprintf(stderr, "\n*** CRASH (signal %d) in recomp func: %s ***\n",
          sig, g_last_recomp_func ? g_last_recomp_func : "(unknown)");
  dump_sprite_state();
  RecompStackDump();
  cpu_trace_dump_dbpb("CRASH — DB/PB mutations");
  cpu_trace_dump_recent("CRASH — main trace ring", 256);
  fflush(stderr);
  recomp_post_mortem_dump("signal", NULL);
  _exit(128 + sig);
}

#ifdef _WIN32
#include <windows.h>
static LONG WINAPI seh_handler(EXCEPTION_POINTERS* info) {
  extern const char *g_last_recomp_func;
  extern void RecompStackDump(void);
  DWORD code = info->ExceptionRecord->ExceptionCode;
  void* addr = info->ExceptionRecord->ExceptionAddress;
  fprintf(stderr, "\n*** SEH CRASH code=0x%08lX at %p, last recomp func: %s ***\n",
          code, addr, g_last_recomp_func ? g_last_recomp_func : "(unknown)");
  if (code == EXCEPTION_ACCESS_VIOLATION) {
    ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
    ULONG_PTR fault_addr = info->ExceptionRecord->ExceptionInformation[1];
    fprintf(stderr, "    access violation: %s at 0x%p\n",
            kind == 0 ? "read" : (kind == 1 ? "write" : "execute"),
            (void*)fault_addr);
  }
  dump_sprite_state();
  RecompStackDump();
  cpu_trace_dump_dbpb("SEH CRASH — DB/PB mutations");
  cpu_trace_dump_recent("SEH CRASH — main trace ring", 256);
  fflush(stderr);
  recomp_post_mortem_dump("seh", info);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void post_mortem_atexit(void) {
  recomp_post_mortem_dump("atexit", NULL);
}

/* Resolve a relative CLI path against the launch cwd before
 * snesrecomp_anchor_to_exe_dir() redefines what relative means.
 * Returns `buf` on success, the original pointer otherwise. */
static const char *AbsolutizePathArg(const char *path, char *buf, size_t size) {
  extern int snesrecomp_abspath(const char *path, char *out, size_t max_len);
  return (path && snesrecomp_abspath(path, buf, size)) ? buf : path;
}

#undef main
/* Issue #4: bring the selected ROM into the exe directory so the ROM lives
 * alongside the saves/, config.ini and keybinds.ini that are already anchored
 * there. Copies (never moves — the user's original is untouched) the ROM under
 * its own basename into the exe dir and rewrites `rom_path` to the local copy.
 * No-op when the ROM is already in the exe dir or the copy can't be made.
 * Returns 1 if `rom_path` was changed to the local copy. */
static int RelocateRomToExeDir(char *rom_path, size_t cap) {
  if (!rom_path || !rom_path[0]) return 0;
  const char *base = rom_path;
  for (const char *p = rom_path; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  if (!*base) return 0;

  char dst[1024];
  if (!snesrecomp_exe_dir_path(base, dst, sizeof(dst))) return 0;
#ifdef _WIN32
  if (_stricmp(dst, rom_path) == 0) return 0;  /* already in the exe dir */
#else
  if (strcmp(dst, rom_path) == 0) return 0;
#endif

  FILE *in = fopen(rom_path, "rb");
  if (!in) return 0;
  FILE *out = fopen(dst, "wb");
  if (!out) { fclose(in); return 0; }
  char buf[65536];
  size_t n;
  int ok = 1;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
  fclose(in);
  fclose(out);
  if (!ok) { remove(dst); return 0; }

  snprintf(rom_path, cap, "%s", dst);
  printf("[Launcher] Copied ROM into the game directory: %s\n", dst);
  return 1;
}

int main(int argc, char** argv) {
#ifndef _WIN32
  /* On Windows, do NOT install a SIGSEGV handler: the MSVC CRT's signal
   * shim intercepts access violations BEFORE the SetUnhandledExceptionFilter
   * SEH filter, so crashes would reach crash_handler with no
   * EXCEPTION_POINTERS — no exception record in the minidump/report. With
   * SIGSEGV left uninstalled, AVs reach seh_handler with full fault
   * context. SIGABRT (abort/assert) stays handled on all platforms. */
  signal(SIGSEGV, crash_handler);
#endif
  signal(SIGABRT, crash_handler);
#ifdef _WIN32
  SetUnhandledExceptionFilter(seh_handler);
  /* Suppress the Windows error dialog so SEH unwinds straight to our
   * filter and we can write the post-mortem report without the user
   * having to dismiss a popup first. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
  atexit(post_mortem_atexit);
  host_report_init("Super Mario World", SNESRECOMP_BUILD_VERSION);
  /* ARM the backwards watcher BEFORE any recompiled code runs. Without
   * this, the trace ring records but no tripwires fire. With this:
   * - DB-watch on every byte SMW shouldn't legitimately use as DB
   * - PB-watch on every non-zero PB
   * - S-watch when stack leaves $0100-$1FFF
   * - Func-watch on the bank03.cfg empty stub
   * - Off-rails dumps (rate-limited) from RomPtr/cart_readLorom soft fails
   * Each tripwire dumps the trace BACKWARDS so we see the chain that
   * birthed the bad state, not just where it died. */
  /* Heap-allocate the cpu trace ring before any tripwire arms. The
   * default 64M entries cover ~64K frames at typical block rates,
   * which means the ring no longer rolls over within any realistic
   * investigation window. Override via SNESRECOMP_CPU_TRACE_RING_ENTRIES. */
  cpu_trace_init();
  cpu_trace_arm_default_watches();
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#ifdef __SWITCH__
  SwitchImpl_Init();
#endif
  argc--, argv++;
  /* Path-carrying args are resolved against the LAUNCH cwd; the anchor
   * below changes what relative paths mean, so absolutize them first. */
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    static char config_abs[1024];
    config_file = AbsolutizePathArg(argv[1], config_abs, sizeof(config_abs));
    argc -= 2, argv += 2;
  }
  int start_paused = 0;
  if (argc >= 1 && strcmp(argv[0], "--paused") == 0) {
    start_paused = 1;
    argc -= 1, argv += 1;
  }
  const char *script_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--script") == 0) {
    static char script_abs[1024];
    script_file = AbsolutizePathArg(argv[1], script_abs, sizeof(script_abs));
    argc -= 2, argv += 2;
  }
  const char *framedump_dir = NULL;
  if (argc >= 2 && strcmp(argv[0], "--framedump") == 0) {
    static char framedump_abs[1024];
    framedump_dir = AbsolutizePathArg(argv[1], framedump_abs, sizeof(framedump_abs));
    argc -= 2, argv += 2;
  }
  /* Force the GUI launcher even when SkipLauncher = 1 is set in config.ini.
   * (The other way back is to set SkipLauncher = 0 in config.ini.) */
  int force_launcher = 0;
  if (argc >= 1 && strcmp(argv[0], "--launcher") == 0) {
    force_launcher = 1;
    argc -= 1, argv += 1;
  }
  if (argc >= 1 && argv[0] && argv[0][0] != '-' && argv[0][0] != '\0') {
    /* Positional ROM path. */
    static char rom_abs[1024];
    argv[0] = (char *)AbsolutizePathArg(argv[0], rom_abs, sizeof(rom_abs));
  }

  /* The config is config.ini next to the executable — nothing else,
   * no directory walking. Anchoring cwd to the exe dir also pins
   * keybinds.ini, rom.cfg and saves/ there, however the process was
   * launched. (On read-only installs the anchor declines and cwd
   * stays authoritative; see launcher.h.) */
  {
    extern int snesrecomp_anchor_to_exe_dir(void);
    int anchored = snesrecomp_anchor_to_exe_dir();
    host_report_breadcrumb("exe-dir anchor: %s",
                           anchored ? "ok" : "declined (cwd stays authoritative)");
  }

  /* Carry a legacy saves/smw.srm forward to the generic saves/save.srm BEFORE the
   * launcher runs, so its SAVES panel shows the migrated save on the first launch
   * after upgrading. Idempotent; RtlReadSram also calls it on boot as a fallback. */
  RtlMigrateLegacySram("smw");
  if (!config_file)
    EnsureConfigIni();
  /* Pin config.ini to the exe directory for all reads/writes, regardless of the
   * current working directory (defense in depth with snesrecomp_anchor_to_exe_dir
   * above + OFN_NOCHANGEDIR on the file dialogs). EnsureConfigIni already created
   * it next to the exe; this keeps a stray chdir from relocating later writes. */
  static char config_exe_path[1024];
  if (!config_file &&
      snesrecomp_exe_dir_path("config.ini", config_exe_path, sizeof(config_exe_path)))
    config_file = config_exe_path;
  ParseConfigFile(config_file);
  // Apply local overrides if present (gitignored). Lets a developer
  // mute audio etc. without touching the checked-in config.ini. Last
  // parser to set a key wins, so local overrides take precedence.
  {
    FILE *f_local = fopen("config.local.ini", "rb");
    if (f_local) {
      fclose(f_local);
      ParseConfigFile("config.local.ini");
    }
  }
  host_report_breadcrumb(
      "config parsed: output=%d new_renderer=%d scale=%d fullscreen=%d "
      "audio=%d freq=%d samples=%d skip_launcher=%d",
      g_config.output_method, g_config.new_renderer, g_config.window_scale,
      g_config.fullscreen, g_config.enable_audio, g_config.audio_freq,
      g_config.audio_samples, g_config.skip_launcher);

  /* Resolve the SNES ROM path: argv[0] -> rom.cfg cache -> file picker.
   * On success, replace argv so the existing ReadWholeFile + oracle init
   * paths below pick up the resolved path without further changes.
   *
   * CRC32 of Super Mario World (USA), unheadered 512KB. The launcher
   * skips a 512-byte SMC copier header automatically before hashing,
   * so headered and unheadered dumps both verify against this value. */
  static char rom_path_buf[1024];
  static const uint32_t kSmwUsaCrc32 = 0xB19ED489u;
  int rom_resolved_by_launcher = 0;

#if defined(SNES_LAUNCHER) || defined(RECOMP_LAUNCHER)
  /* GUI launcher: pick/verify ROM + tune settings before boot. Skipped for
   * headless/scripted paths (--paused/--script/--framedump), an explicit
   * positional ROM, or SNESRECOMP_NO_LAUNCHER. On UNAVAILABLE it falls through
   * to the console resolver below. */
  {
    int headless = start_paused || (script_file != NULL) || (framedump_dir != NULL);
    int have_positional = (argc >= 1 && argv[0] && argv[0][0] != '-' && argv[0][0] != '\0');
    const char *no_launcher = getenv("SNESRECOMP_NO_LAUNCHER");
    int want_launcher = !headless && !have_positional && !(no_launcher && *no_launcher);

    /* SkipLauncher (issue #5): boot straight from the cached ROM, skipping the
     * GUI, unless --launcher forces it back. If the cache is missing/unreadable
     * we fall through and show the launcher so a fresh user can still pick a ROM. */
    if (want_launcher && g_config.skip_launcher && !force_launcher) {
      char cached[1024]; cached[0] = '\0';
      FILE *rc = fopen("rom.cfg", "r");
      if (rc) {
        if (fgets(cached, sizeof(cached), rc)) {
          size_t l = strlen(cached);
          while (l && (cached[l-1] == '\n' || cached[l-1] == '\r')) cached[--l] = '\0';
        }
        fclose(rc);
      }
      if (cached[0]) {
        FILE *probe = fopen(cached, "rb");
        if (probe) {
          fclose(probe);
          snprintf(rom_path_buf, sizeof(rom_path_buf), "%s", cached);
          rom_resolved_by_launcher = 1;
          want_launcher = 0;
          host_report_breadcrumb("launcher skipped (SkipLauncher=1, cached rom)");
        }
      }
    }

    if (want_launcher) {
      host_report_breadcrumb("launcher: opening GUI");
#if defined(RECOMP_LAUNCHER)
      RecompLauncherCSettings ls;   /* recomp-ui ABI: same base fields as SnesLauncher, plus additive */
#else
      SnesLauncherCSettings ls;
#endif
      memset(&ls, 0, sizeof(ls));
      ls.output_method = g_config.output_method;
      ls.window_scale  = g_config.window_scale ? g_config.window_scale : 2;
      ls.fullscreen    = g_config.fullscreen;
      ls.ignore_aspect = g_config.ignore_aspect_ratio;
      ls.linear_filter = g_config.linear_filtering;
      ls.widescreen    = g_config.widescreen;
      ls.widescreen_hud= g_config.widescreen_hud;
      ls.enable_audio  = g_config.enable_audio;
      ls.audio_freq    = g_config.audio_freq;
      ls.volume        = 100;
      ls.player_src[0] = g_config.enable_gamepad[0] ? 2 : ((g_config.has_keyboard_controls & 1) ? 1 : 1);
      ls.player_src[1] = g_config.enable_gamepad[1] ? 2 : ((g_config.has_keyboard_controls & 2) ? 1 : 0);
      ls.deadzone[0] = g_config.deadzone[0];
      ls.deadzone[1] = g_config.deadzone[1];
      ls.skip_launcher = g_config.skip_launcher;
      ls.msu1_enabled = g_config.msu1_enabled;
      snprintf(ls.msu1_dir, sizeof(ls.msu1_dir), "%s", g_config.msu1_dir);

      char init_rom[1024]; init_rom[0] = '\0';
      {
        FILE *rc = fopen("rom.cfg", "r");
        if (rc) {
          if (fgets(init_rom, sizeof(init_rom), rc)) {
            size_t l = strlen(init_rom);
            while (l && (init_rom[l-1] == '\n' || init_rom[l-1] == '\r')) init_rom[--l] = '\0';
          }
          fclose(rc);
        }
      }

#if defined(RECOMP_LAUNCHER)
      RecompLauncherCGameInfo gi;
      memset(&gi, 0, sizeof(gi));
      /* SNES system identity (theme=CRT, platform="SUPER NINTENDO", rom_noun
       * "ROM", widescreen_supported=1); SMW overrides the per-game
       * specifics below. One profile call keeps the identity from drifting
       * across SNES titles, exactly as the PSX host does for its. */
      launcher_profile_apply("snes", &gi);
#else
      SnesLauncherCGameInfo gi;
      memset(&gi, 0, sizeof(gi));
#endif
      gi.name = "Super Mario World";
      gi.region = "(USA)";
      gi.sram_path = "saves/save.srm";  /* generic SRAM path (RtlReadSram migrates legacy) */
      gi.num_players = 1;               /* single active controller (no P2 row) */
      gi.expected_crc = kSmwUsaCrc32;
      gi.has_expected_crc = 1;
      gi.widescreen_supported = 1;
      /* MSU-1: the shipped build is recompiled from Conn's audio-only SMW
       * MSU-1 patch (driver baked into bank $04 — see recomp/msu1/). It runs
       * on the stock ROM: with no pack present the driver replays native SPC
       * audio (authentic); drop a matching PCM pack + enable MSU-1 in Settings
       * to stream it. No runtime ROM patching needed (driver is in the C). */
      gi.msu1_supported = 1;
      gi.msu1_note = "Uses the audio-only \"SMW MSU-1\" patch (zeldix t1436). "
                     "PCM packs must be built for THIS patch \xE2\x80\x94 packs for "
                     "SMW MSU+ or Plus Ultra will not line up.";
      gi.config_path = config_file;  /* hotkey editor targets the live config */

#if defined(RECOMP_LAUNCHER)
      /* cwd is anchored to the exe dir (snesrecomp_anchor_to_exe_dir above),
       * and recomp_ui.cmake stages assets to <exe>/assets, so "." resolves
       * assets correctly. */
      int act = recomp_launcher_run_window("Super Mario World \xE2\x80\x94 Launcher",
                                         &ls, &gi, ".", init_rom,
                                         rom_path_buf, sizeof(rom_path_buf));
#else
      int act = snes_launcher_run_window("Super Mario World \xE2\x80\x94 Launcher",
                                         &ls, &gi, "launcher", init_rom,
                                         rom_path_buf, sizeof(rom_path_buf));
#endif
      host_report_breadcrumb("launcher: action=%d rom=%s", act,
                             rom_path_buf[0] ? rom_path_buf : "(none)");
      if (act == 1) return 0;  /* user closed the launcher */
      if (act == 0) {
        g_config.output_method       = (uint8)ls.output_method;
        g_config.window_scale        = (uint8)ls.window_scale;
        g_config.fullscreen          = (uint8)ls.fullscreen;
        g_config.ignore_aspect_ratio = ls.ignore_aspect != 0;
        g_config.linear_filtering    = ls.linear_filter != 0;
        g_config.widescreen          = ls.widescreen != 0;
        g_config.widescreen_hud      = ls.widescreen_hud != 0;
        g_config.enable_audio        = true;  /* audio is always on; MSU-1 vs SPC is the toggle */
        g_config.audio_freq          = (uint16)ls.audio_freq;
        g_config.enable_gamepad[0]   = ls.player_src[0] == 2;
        g_config.enable_gamepad[1]   = ls.player_src[1] == 2;
        g_config.has_keyboard_controls =
            (uint8)((ls.player_src[0] == 1 ? 1 : 0) | (ls.player_src[1] == 1 ? 2 : 0));
        g_config.deadzone[0] = (uint8)ls.deadzone[0];
        g_config.deadzone[1] = (uint8)ls.deadzone[1];
        g_config.skip_launcher = ls.skip_launcher != 0;
        g_config.msu1_enabled = ls.msu1_enabled != 0;
        snprintf(g_config.msu1_dir, sizeof(g_config.msu1_dir), "%s", ls.msu1_dir);
        if (g_config.msu1_enabled && g_config.msu1_dir[0]) {
#ifdef _WIN32
          static char msu_env[600];
          snprintf(msu_env, sizeof(msu_env), "SNESRECOMP_MSU1=%s", g_config.msu1_dir);
          _putenv(msu_env);
#else
          setenv("SNESRECOMP_MSU1", g_config.msu1_dir, 1);
#endif
        }
        /* Persist the launcher's choices so they're remembered next boot. */
        WriteConfigFile(config_file);
        /* The launcher's Hotkeys editor writes [KeyMap] straight into the
         * config file, which was parsed before the launcher ran — re-apply
         * so rebinds work on THIS boot, not the next one. (WriteConfigFile
         * above preserves [KeyMap] lines, so order is safe.) */
        ConfigReloadKeyMap(config_file);
        if (rom_path_buf[0]) {
          FILE *rc = fopen("rom.cfg", "w");
          if (rc) { fprintf(rc, "%s\n", rom_path_buf); fclose(rc); }
          rom_resolved_by_launcher = 1;
        }
      }
      /* act == 2 (unavailable) -> console resolver below */
    }
  }
#endif

  /* Resolve the SNES ROM path: argv[0] -> rom.cfg cache -> file picker. */
  if (!rom_resolved_by_launcher) {
    char *la_argv[2] = {
      (char *)"smw",
      (char *)((argc >= 1 && argv[0]) ? argv[0] : "")
    };
    int la_argc = (la_argv[1][0] != '\0') ? 2 : 1;
    if (!snesrecomp_launcher_resolve_rom(la_argc, la_argv, rom_path_buf,
                                         sizeof(rom_path_buf), kSmwUsaCrc32)) {
      /* User cancelled the picker or repeatedly chose a non-matching ROM. */
      return 1;
    }
  }
  /* Issue #4: co-locate the ROM with the exe (interactive launches only — leave
   * explicit scripted/headless paths exactly where the caller pointed them). */
  if (!start_paused && script_file == NULL && framedump_dir == NULL) {
    if (RelocateRomToExeDir(rom_path_buf, sizeof(rom_path_buf))) {
      FILE *rc = fopen("rom.cfg", "w");
      if (rc) { fprintf(rc, "%s\n", rom_path_buf); fclose(rc); }
    }
  }

  static char *resolved_argv[2];
  resolved_argv[0] = rom_path_buf;
  resolved_argv[1] = NULL;
  argv = resolved_argv;
  argc = 1;
  host_report_breadcrumb("rom resolved: %s", rom_path_buf);

  /* Honor the persisted MSU-1 choice on every boot path — launcher, SkipLauncher,
   * positional ROM, SNESRECOMP_NO_LAUNCHER. The launcher exports this when it runs;
   * doing it here too means a launcher-skipping boot still streams MSU-1 if the
   * user enabled it. An existing env value (set by the launcher or the shell) wins. */
  if (g_config.msu1_enabled && g_config.msu1_dir[0] && !getenv("SNESRECOMP_MSU1")) {
    static char msu_env[600];
    snprintf(msu_env, sizeof(msu_env), "SNESRECOMP_MSU1=%s", g_config.msu1_dir);
#ifdef _WIN32
    _putenv(msu_env);
#else
    setenv("SNESRECOMP_MSU1", g_config.msu1_dir, 1);
#endif
  }

  /* Let MSU-1 derive its pack base from the ROM name when SNESRECOMP_MSU1=auto
   * (the launcher normally passes the pack folder explicitly, which msu1.c also
   * resolves). Harmless when MSU-1 is disabled. */
  { extern void msu1_set_rom_path(const char *); msu1_set_rom_path(rom_path_buf); }

  // Initialize debug server
  {
    extern int debug_server_init(int port);
    extern void debug_server_set_ram(uint8_t *ram, uint32_t ram_size);
    if (debug_server_init(4377) == 0) {
      fprintf(stderr, "[main] Debug server ready on port 4377\n");
    }
    if (start_paused) {
      debug_server_start_paused();
      fprintf(stderr, "[main] Started paused — send 'step N' or 'continue' via TCP\n");
    }
  }

  g_gamepad[0].joystick_id = g_gamepad[1].joystick_id = -1;
  g_snes_width = 256;
  g_snes_height = 224;// (g_config.extend_y ? 240 : 224);

  // Widescreen (optional, default off -> authentic 256-wide). Render enough
  // extra PPU columns that the square-pixel image fills 16:9 at the current
  // height: width = round(height * 16/9), split evenly into a left/right
  // border, clamped to the PPU's compiled capacity (kPpuExtraLeftRight/side).
  // Env var SNESRECOMP_WIDESCREEN overrides config (1/0) for quick toggling.
  {
    const char *ws_env = getenv("SNESRECOMP_WIDESCREEN");
    if (ws_env && *ws_env)
      g_config.widescreen = (atoi(ws_env) != 0);
  }
  if (g_config.widescreen) {
    int target_w = (g_snes_height * 16 + 4) / 9;   // round to nearest
    g_ws_extra = IntMin((target_w - 256) / 2, kPpuExtraLeftRight);
    if (g_ws_extra < 0) g_ws_extra = 0;
    g_snes_width = 256 + g_ws_extra * 2;
    g_ws_active = (g_ws_extra > 0);
  }
  extern void SmwWidescreenInterpPreOpcode(CpuState *cpu, uint32_t pc24);
  interp_bridge_set_pre_opcode_hook(
      0x02A828u,
      g_config.widescreen ? SmwWidescreenInterpPreOpcode : NULL);
  // A wider viewport can expose more sprites on one scanline than the SNES
  // could see at 256px. Keep authentic caps configurable at 4:3, but lift them
  // whenever widescreen is active so sprites do not disappear prematurely.
  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
    g_config.extend_y * kPpuRenderFlags_Height240 |
    (g_config.no_sprite_limits || g_ws_active) *
      kPpuRenderFlags_NoSpriteLimits;

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    host_report_breadcrumb("SDL_Init FAILED: %s", SDL_GetError());
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }
  host_report_breadcrumb("SDL init ok: video=%s audio=%s",
                         SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)",
                         SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");

  /* Load (or generate) keybinds.ini next to the executable. */
  keybinds_init(NULL);

  bool custom_size = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width = custom_size ? g_config.window_width : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

  if (g_config.output_method == kOutputMethod_OpenGL) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs);
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  /* Load the SNES ROM. argv[0] is the launcher-resolved path (always
   * non-NULL after snesrecomp_launcher_resolve_rom returned success). */
  uint8 *kRom = NULL;
  uint32 kRom_SIZE = 0;
  if (argv[0]) {
    size_t size;
    kRom = ReadWholeFile(argv[0], &size);
    kRom_SIZE = (uint32)size;
    if (!kRom)
      goto error_reading;
  }
  host_report_breadcrumb("rom loaded: %u bytes", kRom_SIZE);

  extern const RtlGameInfo kSmwGameInfo;
  RtlRegisterGame(&kSmwGameInfo);
  Snes *snes = SnesInit(kRom, kRom_SIZE);
  host_report_breadcrumb("SnesInit: %s", snes ? "ok" : "FAILED");
  if (snes == NULL) {
error_reading:;
#ifdef __SWITCH__
    ThrowMissingROM();
#else
    char buf[256];
    snprintf(buf, sizeof(buf), "unable to load rom");
    Die(buf);
#endif
    return 1;
  }

  // Connect debug server to SNES RAM
  {
    extern void debug_server_set_ram(uint8_t *ram, uint32_t ram_size);
    debug_server_set_ram(snes->ram, 0x20000);
  }

#ifdef ENABLE_ORACLE_BACKEND
  // Start the emulator-oracle backend with the same ROM. Gated on the
  // Oracle build configuration only; Release|x64 never sees any of this.
  // The runner typically loads smw.sfc from cwd via the asset pipeline
  // (argv[0] is usually NULL), so we default to "smw.sfc" in cwd when
  // argv[0] was not supplied.
  {
    extern int snes_oracle_init_default(const char *rom_path);
    const char *rom_path = (argv[0] && *argv[0]) ? argv[0] : "smw.sfc";
    int rc = snes_oracle_init_default(rom_path);
    if (rc != 0)
      fprintf(stderr, "[oracle] init failed rc=%d (rom=%s)\n", rc, rom_path);
    else
      fprintf(stderr, "[oracle] backend ready (rom=%s)\n", rom_path);
  }
#endif

  SDL_Window *window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    host_report_breadcrumb("SDL_CreateWindow FAILED: %s", SDL_GetError());
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);
  host_report_breadcrumb("window created: %dx%d flags=0x%x",
                         window_width, window_height, g_win_flags);

  if (!g_renderer_funcs.Initialize(window)) {
    host_report_breadcrumb("renderer init FAILED (output_method=%d)",
                           g_config.output_method);
    return 1;
  }
  host_report_breadcrumb("renderer initialized: %s",
      g_config.output_method == kOutputMethod_OpenGL ? "opengl" :
      g_config.output_method == kOutputMethod_SDLSoftware ? "sdl-software" : "sdl");

  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  g_spc_player = SmwSpcPlayer_Create();

  g_spc_player->initialize(g_spc_player);
  host_report_breadcrumb("SPC player initialized");

  if (g_config.enable_audio) {
    /* Enumerate output devices into the breadcrumb ring: which device
     * SDL picks (and what else was available) is exactly the per-machine
     * variable a non-reproducible audio/boot crash report needs. */
    {
      int ndev = SDL_GetNumAudioDevices(0);
      host_report_breadcrumb("audio outputs: %d device(s)", ndev);
      for (int i = 0; i < ndev && i < 8; i++)
        host_report_breadcrumb("audio output[%d]: %s", i,
                               SDL_GetAudioDeviceName(i, 0));
    }
    SDL_AudioSpec want = { 0 }, have;
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    g_audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio_device == 0) {
      host_report_breadcrumb("audio device open FAILED: %s", SDL_GetError());
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = 2;
    /* One native DSP block is 534 samples at the SPC's true output rate
     * of 32040 Hz (1.024 MHz / 32). The old divisor of 32000 understated
     * the rate, playing everything a constant -2.2 cents flat (measured
     * vs the snes9x oracle, MMX issue #4); the truncating division also
     * undersized the block for non-multiple rates. Round to the nearest
     * frame: 32040->534 (1:1, no resample), 48000->800, 44100->735. */
    g_frames_per_block = (534 * have.freq + 32040 / 2) / 32040;
    g_audiobuffer = (uint8 *)calloc(g_frames_per_block * have.channels * sizeof(int16), 1);
    host_report_breadcrumb(
        "audio device opened: freq=%d (want %d) ch=%d samples=%d frames_per_block=%d",
        have.freq, want.freq, have.channels, have.samples, g_frames_per_block);
  } else {
    host_report_breadcrumb("audio disabled in config");
  }

  PpuBeginDrawing(g_ppu, g_my_pixels, (size_t)g_snes_width * 4, 0);

  // Sync the border width to the window we actually got (config-sized,
  // fullscreen-desktop on a wide monitor, etc.). Later resizes flow
  // through SDL_WINDOWEVENT_SIZE_CHANGED.
  {
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(g_window, &win_w, &win_h);
    WidescreenUpdateForWindow(win_w, win_h);
  }

  MkDir("saves");
    
  RtlReadSram();

  {
    int njs = SDL_NumJoysticks();
    printf("[Gamepad] SDL reports %d joystick(s) at startup. "
           "enable_gamepad=[%d,%d]\n",
           njs, g_config.enable_gamepad[0], g_config.enable_gamepad[1]);
    for (int i = 0; i < njs; i++) {
      const char *name = SDL_JoystickNameForIndex(i);
      int is_gc = SDL_IsGameController(i);
      printf("[Gamepad]   #%d name=%s is_game_controller=%d\n",
             i, name ? name : "(null)", is_gc);
      OpenOneGamepad(i);
    }
    if (njs == 0) {
      printf("[Gamepad] No joysticks detected. "
             "On Windows, plug controller in BEFORE launching, "
             "or check that XInput drivers are installed.\n");
    }
  }

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  if (script_file)
    LoadScript(script_file);

  if (framedump_dir)
    FrameDump_Init(framedump_dir);

  bool running = true;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  uint8 audiopaused = true;
  GamepadInfo *gi;

  host_report_breadcrumb("entering main loop");

  while (running) {
    SDL_Event event;

    /* Inert unless SNESRECOMP_CRASH_TEST is set — support drill for the
     * whole crash-capture pipeline (minidump + report + crash copy). */
    host_report_crash_test_tick();

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERDEVICEREMOVED:
        gi = GetGamepadInfo(event.cdevice.which);
        if (gi) {
          memset(gi, 0, sizeof(GamepadInfo));
          gi->joystick_id = -1;
        }
        break;
      case SDL_CONTROLLERAXISMOTION:
        gi = GetGamepadInfo(event.caxis.which);
        if (gi)
          HandleGamepadAxisInput(gi, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        gi = GetGamepadInfo(event.cbutton.which);
        if (gi) {
          int b = RemapSdlButton(event.cbutton.button);
          if (b >= 0)
            HandleGamepadInput(gi, b, event.type == SDL_CONTROLLERBUTTONDOWN);
        }
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0 ? SDL_TRUE : SDL_FALSE);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
          WidescreenUpdateForWindow(event.window.data1, event.window.data2);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (g_audio_device)
        SDL_PauseAudioDevice(g_audio_device, audiopaused);
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    if (g_input_state & 0xf0)
      g_gamepad[0].axis_buttons = 0;
    if (g_input_state & 0xf0000)
      g_gamepad[1].axis_buttons = 0;
    {
      int ls = debug_server_consume_loadstate();
      if (ls >= 0)
        RtlSaveLoad(kSaveLoad_Load, ls);
    }
    debug_server_wait_if_paused();

    /* Drive the SNES controller bits in g_input_state from keybinds.ini.
     * config.ini's [KeyMap] still owns system commands (state save/load,
     * fullscreen, pause, etc.); the 12 controller buttons per player
     * come from keybinds.ini.
     *
     * Mapping below: keybinds bit layout (see keybinds.h) -> kKeys_Controls
     * index (config.ini [Controls] order: Up Down Left Right Select Start
     * A B X Y L R). HandleCommand is idempotent for set/clear, so calling
     * it every frame is safe. */
    {
      const uint8_t *keys = SDL_GetKeyboardState(NULL);
      uint16_t kb_p1 = keybinds_read_player(keys, 1);
      uint16_t kb_p2 = keybinds_read_player(keys, 2);
      static const uint8 kKb2CtrlsIdx[12] = { 7, 6, 5, 4, 9, 8, 3, 11, 2, 10, 1, 0 };
      for (int i = 0; i < 12; i++) {
        HandleCommand(kKeys_Controls   + i, (kb_p1 >> kKb2CtrlsIdx[i]) & 1);
        HandleCommand(kKeys_ControlsP2 + i, (kb_p2 >> kKb2CtrlsIdx[i]) & 1);
      }
    }

    uint32 inputs = g_input_state | g_pad_buttons | g_gamepad[0].axis_buttons | g_gamepad[1].axis_buttons << 12;
    inputs |= TickScript();
    inputs |= debug_server_get_controller_inputs();
    RtlRunFrame(inputs | GetActiveControllers() | debug_server_get_controller_active_mask());

#ifdef ENABLE_ORACLE_BACKEND
    // Step the oracle emulator with the same input. First-light does
    // not guarantee input-bit parity between the runner's 12-bit-per-
    // player layout and the bridge's SNES-hardware layout — good
    // enough for attract-demo comparison (no input), needs remapping
    // work before live gameplay divergence analysis.
    {
      extern void emu_oracle_run_frame(uint16_t j1, uint16_t j2);
      emu_oracle_run_frame((uint16_t)(inputs & 0xFFF),
                           (uint16_t)((inputs >> 12) & 0xFFF));
    }
#endif

    // Bank validation removed — 100% oracle mode, no banks enabled.

    frameCtr++;
    if (frameCtr == 1)
      host_report_breadcrumb("first frame simulated");
    else if (frameCtr % 3600 == 0)  /* ~once a minute at 60 fps */
      host_report_breadcrumb("heartbeat: frame=%u", frameCtr);
    /* Dev-only headless turbo stress (SNESRECOMP_FORCE_TURBO=1): forces the
     * turbo path every frame so an automated soak can exercise the LLE path's
     * turbo-safety (raster-IRQ-on-render-skip) without a human holding Tab.
     * Env-gated; no ship effect. */
    { static int s_ft = -1;
      if (s_ft < 0) { const char *e = getenv("SNESRECOMP_FORCE_TURBO");
                      s_ft = (e && e[0] && e[0] != '0') ? 1 : 0; }
      if (s_ft) g_turbo = 1; }
    g_snes->disableRender = g_turbo && (frameCtr & 0xf) != 0;

    if (!g_snes->disableRender) {
      DrawPpuFrameWithPerf();
    } else {
      /* Turbo (render skipped): SmwDrawPpuFrame / draw_ppu_frame is NOT purely
       * cosmetic — it also simulates HDMA and fires the raster IRQ (I_IRQ).
       * Under the default LLE scheduler the game runs the
       * REAL NMI/IRQ machinery, so skipping the raster IRQ 15/16 frames would
       * let an IRQ-gated guest path spin, the 5s watchdog longjmp out mid-frame
       * without restoring guest S, and the stack leak until underflow (the MMX
       * turbo garble/freeze). Run the guest-state sim every frame; skip only the
       * host present (BeginDraw/memcpy/EndDraw). Harmless to HLE (it never waits
       * on the raster IRQ). */
      g_rtl_game_info->draw_ppu_frame();
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    /* Frame-delay pacing locks the loop to ~60 fps so audio (SPC + MSU-1, both
     * produced at 1/60 s per frame) stays in sync with the sound device. On by
     * default. Power users on an exactly-60 Hz / vsync-correct display can set
     * DisableFrameDelay = 1 in config.ini (cfg-only, no UI) to skip it for
     * slightly better perf — at the risk of audio desync on other displays. */
    if (!g_snes->disableRender && !g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
        //        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }

  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  RtlWriteSram();

  // clean sdl
  SDL_PauseAudioDevice(g_audio_device, 1);
  SDL_CloseAudioDevice(g_audio_device);
  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();

#ifdef __SWITCH__
  SwitchImpl_Exit();
#endif

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst + pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}


static void RenderNumber(uint8 *dst, size_t pitch, int n, uint8 big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand(uint32 j, bool pressed) {
  static const uint8 kKbdRemap[] = { 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
  if (j < kKeys_Controls)
    return;

  if (j <= kKeys_Controls_Last) {
    uint32 m = 1 << kKbdRemap[j - kKeys_Controls];
    g_input_state = pressed ? (g_input_state | m) : (g_input_state & ~m);
    return;
  }

  if (j <= kKeys_ControlsP2_Last) {
    uint32 m = 0x1000 << kKbdRemap[j - kKeys_ControlsP2];
    g_input_state = pressed ? (g_input_state | m) : (g_input_state & ~m);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    RtlSaveLoad(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    RtlSaveLoad(kSaveLoad_Save, j - kKeys_Save);
  } else {
    switch (j) {
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      RtlReset(1);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer:
      g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer;
      printf("New renderer = %x\n", g_ppu_render_flags & kPpuRenderFlags_NewRenderer);
      g_new_ppu = (g_ppu_render_flags & kPpuRenderFlags_NewRenderer) != 0;
      break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, (SDL_Keymod)keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static uint32 GetActiveControllers() {
  uint32 ctrl = g_config.has_keyboard_controls;
  ctrl |= g_gamepad[0].joystick_id != -1 ? 1 : 0;
  ctrl |= g_gamepad[1].joystick_id != -1 ? 2 : 0;
  return ctrl << 30;
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller) {
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
      return;
    }

    uint32 joystick_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    if (GetGamepadInfo(joystick_id))
      return;

    uint8 scan_order[3] = { SDL_GameControllerGetPlayerIndex(controller), 0, 1 };

    int found_idx = -1;
    for (int i = 0; i < 3; i++) {
      uint8 j = scan_order[i];
      if (j < 2 && g_config.enable_gamepad[j] && (i == 0 || g_gamepad[j].joystick_id == -1)) {
        found_idx = j;
        break;
      }
    }

    printf("Found controller '%s' assigning to player %d\n", SDL_GameControllerName(controller), found_idx + 1);
    if (found_idx >= 0) {
      GamepadInfo *gi = &g_gamepad[found_idx];
      memset(gi, 0, sizeof(GamepadInfo));
      gi->index = found_idx;
      gi->joystick_id = joystick_id;
    }
  }
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

/* Set/clear a SNES controller bit from a gamepad source. Mirrors
 * HandleCommand's kKeys_Controls / kKeys_ControlsP2 logic but writes
 * to g_pad_buttons so the per-frame keyboard polling can't clobber
 * gamepad-set bits. Non-controller commands (system shortcuts bound
 * via config.ini [GamepadMap]) fall through to HandleCommand so things
 * like state save/load on a gamepad button still work. */
static void SetPadButtonOrFallthrough(uint32 j, bool pressed) {
  static const uint8 kKbdRemap[] = { 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
  if (j >= kKeys_Controls && j <= kKeys_Controls_Last) {
    uint32 m = 1u << kKbdRemap[j - kKeys_Controls];
    g_pad_buttons = pressed ? (g_pad_buttons | m) : (g_pad_buttons & ~m);
    return;
  }
  if (j >= kKeys_ControlsP2 && j <= kKeys_ControlsP2_Last) {
    uint32 m = 0x1000u << kKbdRemap[j - kKeys_ControlsP2];
    g_pad_buttons = pressed ? (g_pad_buttons | m) : (g_pad_buttons & ~m);
    return;
  }
  HandleCommand(j, pressed);
}

static void HandleGamepadInput(GamepadInfo *gi, int button, bool pressed) {
  if (!!(gi->modifiers & (1 << button)) == pressed)
    return;
  gi->modifiers ^= 1 << button;
  if (pressed)
    gi->last_cmd[button] = FindCmdForGamepadButton(button + gi->index * kGamepadBtn_Count, gi->modifiers);
  if (gi->last_cmd[button] != 0)
    SetPadButtonOrFallthrough(gi->last_cmd[button], pressed);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static void HandleGamepadAxisInput(GamepadInfo *gi, int axis, Sint16 value) {
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &gi->last_axis_x : &gi->last_axis_y) = value;
    int buttons = 0;
    /* Deadzone radius from config (per player), as a fraction of full stick
     * range; below it the stick reports no direction. Replaces a hardcoded
     * 10000 (~30%) that ignored the Configure Controllers value (issue #3). */
    int dz_player = (int)(gi - g_gamepad);
    if (dz_player < 0 || dz_player > 1) dz_player = 0;
    int dz_radius = g_config.deadzone[dz_player] * 32767 / 100;
    if (gi->last_axis_x * gi->last_axis_x + gi->last_axis_y * gi->last_axis_y >= dz_radius * dz_radius) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(gi->last_axis_y, gi->last_axis_x) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    gi->axis_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(gi, axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

/* Default config.ini content written next to the executable when no
 * config.ini exists there on launch. Mirrors the repo-root config.ini
 * but stripped of dev-only comments; keep them in lock-step when
 * adding new tunables that should be user-discoverable. The
 * [GamepadMap] section gives a plugged-in Xbox controller working
 * defaults out of the box. */
static const char kDefaultConfigIniContent[] =
  "[General]\n"
  "# Automatically save state on quit and reload on start\n"
  "Autosave = 0\n"
  "\n"
  "# Disable the SDL_Delay that paces each frame (slightly better perf if your\n"
  "# display is set to exactly 60hz; may desync audio on other displays)\n"
  "DisableFrameDelay = 0\n"
  "\n"
  "[Graphics]\n"
  "# Window size (Auto or WidthxHeight)\n"
  "WindowSize = Auto\n"
  "\n"
  "# Fullscreen mode (0=windowed, 1=desktop fullscreen, 2=fullscreen w/mode change)\n"
  "Fullscreen = 0\n"
  "\n"
  "# Window scale (1=100%, 2=200%, 3=300%, etc.)\n"
  "WindowScale = 3\n"
  "\n"
  "# Use the optimized SNES PPU implementation\n"
  "NewRenderer = 1\n"
  "\n"
  "# Don't keep the aspect ratio\n"
  "IgnoreAspectRatio = 0\n"
  "\n"
  "# Remove the sprite limits per scan line\n"
  "NoSpriteLimits = 1\n"
  "\n"
  "[Sound]\n"
  "EnableAudio = 1\n"
  "AudioFreq = 32040\n"
  "AudioChannels = 2\n"
  "AudioSamples = 512\n"
  "\n"
  "[KeyMap]\n"
  "# This section is for system-level shortcuts (save/load state,\n"
  "# fullscreen, pause, etc.). The 12 SNES controller buttons live\n"
  "# in keybinds.ini next to the executable.\n"
  "Fullscreen = Alt+Return\n"
  "Reset = Ctrl+r\n"
  "Pause = Shift+p\n"
  "PauseDimmed = p\n"
  "Turbo = Tab\n"
  "WindowBigger = Ctrl+Up\n"
  "WindowSmaller = Ctrl+Down\n"
  "VolumeUp = Shift+=\n"
  "VolumeDown = Shift+-\n"
  "Load =      F1,     F2,     F3,     F4,     F5,     F6,     F7,     F8,     F9,     F10\n"
  "Save = Shift+F1,Shift+F2,Shift+F3,Shift+F4,Shift+F5,Shift+F6,Shift+F7,Shift+F8,Shift+F9,Shift+F10\n"
  "\n"
  "[GamepadMap]\n"
  "# Enable each player's gamepad slot. SDL_GameController-compatible\n"
  "# controllers (Xbox, PlayStation, Switch Pro, etc.) auto-detect\n"
  "# when plugged in. Set to false to force keyboard-only.\n"
  "EnableGamepad1 = true\n"
  "EnableGamepad2 = true\n"
  "\n"
  "# Default Xbox-layout mapping. Order matches kKeys_Controls:\n"
  "#   Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R\n"
  "# Edit + restart to rebind. Shoulder = L1/Lb (top), trigger = L2.\n"
  "Controls =   DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n"
  "ControlsP2 = DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb\n";

/* Ensure config.ini exists next to the executable (cwd after
 * snesrecomp_anchor_to_exe_dir). First launch from a clean release
 * directory writes the default so the config the user can edit is
 * always sitting right beside the exe. */
static void EnsureConfigIni(void) {
  FILE *f = fopen("config.ini", "rb");
  if (f) {
    fclose(f);
    return;
  }
  f = fopen("config.ini", "w");
  if (!f) {
    fprintf(stderr, "Warning: could not write default config.ini\n");
    return;
  }
  fputs(kDefaultConfigIniContent, f);
  fclose(f);
  printf("[config.ini] Generated default config next to the executable\n");
}
