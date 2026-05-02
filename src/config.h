#pragma once
#include "types.h"
#include <SDL_keycode.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,

  kKeys_ControlsP2,
  kKeys_ControlsP2_Last = kKeys_ControlsP2 + 11,

  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  bool extend_y;
  bool no_sprite_limits;
  bool display_perf_title;
  bool disable_frame_delay;

  // [Debug] CanaryMode in smw.ini. 0 = off, 1 = frame.
  // Default 1 in Oracle build (canary auto-records every frame),
  // 0 in Release. See canary.h. Setting only takes effect in Oracle —
  // non-Oracle builds compile out the canary entirely.
  uint8 canary_mode;

  // [Debug] CanaryConverge in smw.ini OR --converge-on-diff CLI flag.
  // When non-zero, on every per-frame WRAM divergence the canary
  // records the event AND copies oracle bytes into recomp's g_ram[]
  // for the diverging region. Forces frame-by-frame WRAM resync so
  // cascading post-divergence noise is suppressed; the event log
  // surfaces every "fresh" seed site. Caveat: register state isn't
  // snapped, so recomp may diverge again the same frame.
  uint8 canary_converge_on_diff;

  // [Debug] CanaryLockstep in smw.ini OR --lockstep CLI flag. Implies
  // CanaryConverge. After every frame, the canary copies oracle's
  // FULL state (WRAM + CPU regs + VRAM + CGRAM + OAM + APU ports)
  // into recomp so each frame starts from a known-good oracle state.
  // The divergence event log then captures only "what bug did
  // recomp's gen code introduce in JUST this frame" — eliminates
  // cascading state drift entirely. PPU control regs, DMA, MMIO
  // shadow are NOT applied (just-written; recomp's gen code re-emits
  // them every frame). Oracle build only.
  uint8 canary_lockstep;

  char *memory_buffer;
  const char *shader;

  bool enable_gamepad[2];

  // Which players have keyboard controls
  uint8 has_keyboard_controls;
} Config;

enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
