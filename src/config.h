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
  // Optional widescreen (16:9). Off by default = authentic 256-wide SNES.
  // Renderer extends the PPU; SMW game-logic widescreen behaviour (spawn
  // window, off-screen culling, HUD) is supplied by the override layer and
  // also gated on this flag.
  bool widescreen;
  // Split the status bar to the widescreen edges (lives cluster left,
  // TIME/coins/score right, item box centered). Defaults on; only takes
  // effect with widescreen active. `WidescreenHud = 0` keeps the
  // authentic centered HUD.
  bool widescreen_hud;
  bool display_perf_title;

  // Skip the per-frame SDL_Delay pacing. Off by default (pacing on) so SPC +
  // MSU-1 audio stays in sync; cfg-only escape hatch (DisableFrameDelay = 1)
  // for users on an exactly-60 Hz / vsync-correct display who want the perf.
  bool disable_frame_delay;

  // Gamepad analog-stick deadzone, per player, 0..100 % of full stick range.
  // Applied where the left stick is mapped to the d-pad. Default 30.
  uint8 deadzone[2];

  // Boot straight to the game, skipping the GUI launcher, on subsequent runs.
  // Set from the launcher's dashboard checkbox. Force the launcher back with the
  // --launcher argument or by setting SkipLauncher = 0 in config.ini.
  bool skip_launcher;

  // MSU-1 streamed audio. Off by default (authentic SPC audio). When enabled
  // and msu1_dir holds a pack, the launcher exports SNESRECOMP_MSU1 so the
  // runtime streams the .pcm tracks. Persisted to config.ini [Sound].
  bool msu1_enabled;
  char msu1_dir[512];

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
// Persist the launcher-editable settings back into `filename` (or config.ini)
// with a surgical, comment-preserving in-place update. Called after the GUI
// launcher returns PLAY so the chosen settings survive the next boot.
void WriteConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);
