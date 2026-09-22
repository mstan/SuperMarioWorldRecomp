#include "smw_renderer.h"
/* Game-owned bridge for the approved, external Falcon owner cache. */
#include "smw_falcon_presentation_runtime.h"

#include "captain_falcon_foreign.h"
#include "falcon_locomotion.h"
#include "falcon_presentation.h"
#include "smw_falcon_audio.h"
#include "foreign_controller.h"
#include "common_rtl.h"
#include "sha256.h"
#include "variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define SMW_GETPID _getpid
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define SMW_GETPID getpid
#endif

#define FALCON_CACHE_PREFIX "falcon-final-r1-e2929e10fccc0aa84e5776227e798abc07cedabf-"
#define FALCON_RUNTIME_MAX_BYTES (64u * 1024u * 1024u)
#define FALCON_PLAYER_OAM_FIRST 64u /* $0300 / four bytes per OAM entry */
#define FALCON_PLAYER_OAM_COUNT 12u /* $0300..$032f, SMW PlayerGFXRt */
#define FALCON_CARRIED_SHELL_BODY_INSET 5.0f
#define FALCON_CARRIED_SHELL_HAND_RAISE 5.0f

static const uint8_t k_runtime_sha256[32] = {
    0x8a,0x8e,0x0a,0xc0,0x13,0x41,0x58,0x44,
    0x88,0xda,0xd5,0x68,0x1a,0xe7,0x56,0x3f,
    0x31,0x42,0xee,0x15,0x91,0x5f,0xf1,0x54,
    0xf5,0xe3,0x12,0x2c,0x57,0x14,0x6a,0x3e,
};
/* Exact WAV-file hashes from the approved final-cache manifest, in controller
 * cue order (jump, punch, kick, dive). These pin owner PCM before registration. */
static const uint8_t k_audio_sha256[11][32] = {
 {0x37,0xe1,0x8f,0x36,0x80,0x80,0xc4,0x07,0x97,0x2b,0x94,0x1f,0x19,0x87,0x53,0x53,0xff,0x72,0x6a,0xf4,0x1d,0x47,0x68,0x43,0xd5,0x38,0x50,0x3d,0x22,0x29,0x85,0x05},
 {0xf3,0x39,0x55,0x62,0x57,0xe6,0x47,0x32,0x1a,0x82,0x63,0x52,0x17,0xa2,0x92,0x2e,0x9b,0x14,0xb9,0x1a,0x43,0x28,0xd4,0xa7,0xc7,0xbf,0x0e,0xcf,0x08,0xf8,0x2a,0xe4},
 {0x8f,0x21,0x5c,0x9d,0x96,0x31,0x64,0x13,0x4a,0x0b,0x55,0x13,0x89,0x7a,0xf2,0xef,0x8c,0x4f,0x6d,0xe2,0x26,0xae,0xf8,0x47,0x18,0x8f,0x24,0xf4,0x65,0x66,0x21,0x5d},
 {0x46,0x46,0xb3,0x42,0xe5,0xb9,0xd0,0xe6,0xdd,0x99,0x86,0x60,0x42,0x32,0x6b,0x4f,0xb6,0xa9,0xd5,0x34,0xb6,0x07,0x4b,0xff,0x10,0x48,0xc7,0x3c,0xfb,0x1e,0x93,0xf3},
 {0xe4,0x80,0xfa,0xc9,0xbf,0x00,0x1b,0x19,0x2c,0xcd,0x92,0xf6,0xc6,0xf6,0x10,0x39,0x14,0x7f,0xf9,0x08,0xcc,0xdb,0x93,0x25,0x2f,0x81,0x48,0xcc,0x44,0xa3,0x93,0xe3},
 {0xc7,0x98,0xd8,0x31,0x04,0xeb,0x13,0xa8,0x2e,0xee,0xbd,0x1a,0x74,0x5d,0xed,0x7f,0x30,0x9b,0x94,0x87,0x22,0x1a,0xd0,0xc5,0x90,0x06,0x04,0x2f,0x0b,0x90,0xd8,0xfb},
 {0xa8,0xa3,0x1b,0xbc,0x12,0x92,0xc3,0x83,0x4a,0xd7,0x3b,0xc3,0x51,0x2f,0xc9,0x25,0x21,0x0a,0x6b,0xd9,0x09,0x0f,0xa9,0x43,0x34,0xba,0x36,0x82,0x01,0x3e,0x02,0x6b},
 {0xfa,0x8b,0x19,0xf9,0x0b,0xb8,0x52,0x07,0x7e,0x19,0x61,0x9a,0x3c,0xb9,0x1e,0x7a,0xf2,0x78,0x3b,0x04,0xb8,0xa0,0x0e,0x15,0x2e,0x25,0xed,0x78,0xee,0xbb,0x8c,0xe3},
 {0x03,0xb9,0x16,0xd6,0xc7,0x20,0x84,0x6b,0xc6,0x5e,0xe7,0xcd,0xc7,0xee,0xb2,0x78,0xa2,0xeb,0xba,0x5e,0x36,0x6b,0x9b,0x19,0x84,0x7c,0xd3,0x06,0xfc,0x4c,0x08,0x06},
 {0xcd,0x16,0x07,0xe8,0xc8,0xf4,0xbb,0x38,0xb6,0x61,0x37,0xe1,0x80,0x60,0x2e,0x94,0x11,0xd9,0xfc,0x8b,0x82,0x4c,0xb6,0x75,0x93,0xfc,0x7b,0xa3,0xce,0xcb,0x47,0xe3},
 {0xf8,0xa9,0x85,0x9b,0xb6,0x14,0xfb,0x6b,0x52,0xa3,0x32,0xcc,0x42,0xda,0xcf,0xc2,0xa5,0xe5,0xba,0x18,0x5e,0x89,0xc6,0xd4,0x2e,0x87,0xa4,0x23,0x42,0xe7,0x43,0x46},
};
static const char *const k_audio_filenames[11] = {"falcon_jump_effort.wav", "falcon_punch_falcon.wav", "falcon_punch_punch.wav", "falcon_kick.wav", "falcon_punch_impact_fgm.wav", "falcon_kick_swing_fgm.wav", "falcon_kick_start_fgm.wav", "falcon_dive_launch_fgm.wav", "falcon_dive_catch_fgm.wav", "falcon_dive_explosion_fgm.wav", "falcon_dive_voice.wav"};

/* Binding is required for PPU's OBJ RemoveFromGame path. It is intentionally
 * not composited: only slots 64..75 are captured and removed. */
static uint32_t s_obj_scratch[kPpuBufWidth * 240];
static FalconPresentation *s_presentation;
static int s_bound;
static int s_suppression_active;
static int s_suppression_failed;
static int s_mesh_draw_active;
static char s_last_gate[96];
static char s_validated_audio_dir[1024];
static int s_audio_pending;
static int s_audio_attempted;
static int s_death_latched;
static int s_death_hidden;
static unsigned s_death_frame;
static float s_death_anchor_y;
static FalconPresentationPose s_last_pose = { FALCON_PRESENT_IDLE, 0.0f, 1 };

static int falcon_controller_selected(void)
{
    const ForeignController *controller = snes_foreign_active();
    return controller != NULL && strcmp(controller->id, SMW_CAPTAIN_FALCON_ID) == 0;
}

static int death_active(void) { return s_presentation && misc_game_mode == 0x14 && player_current_state == 9; }

static int course_clear_active(void)
{
    if (s_presentation == NULL || !falcon_controller_selected()) return 0;
    /* Presentation-only.  The adapter refuses control while `$1493`/keyhole
     * end timers are live, and native SMW owns all walking, keyhole, score,
     * timer, and mode progression. Keep Falcon drawn through both goal-tape
     * and keyhole outro phases instead of exposing native Mario's player OBJ.
     * PlayerState00_LevelFinished then moves ordinary Course Clear into
     * GameMode $0B, which still draws the player OBJ on the black result
     * screen. */
    if (misc_game_mode == 0x14 && player_current_state == 0 &&
        (timer_end_level != 0 || timer_end_level_via_keyhole != 0)) return 1;
    return misc_game_mode == 0x0b;
}

static int active_pipe_handoff(void)
{
    return flag_about_to_warp_in_pipe != 0 ||
           (player_pipe_action != 0 && player_pipe_action < 4);
}

static int scripted_pipe_active(void)
{
    if (s_presentation == NULL || !falcon_controller_selected()) return 0;
    /* Presentation-only.  Native SMW owns pipe travel, but the parody player
     * must remain Falcon while the pipe pose/slide runs. `$89 >= 4` and
     * `$88=20,$89=06` are persistent pipe-adjacent metadata and are covered by
     * normal controllable play; this predicate covers the scripted side. */
    return misc_game_mode == 0x14 &&
           (active_pipe_handoff() || player_timer_pipe_warping != 0) &&
           timer_end_level == 0 && timer_end_level_via_keyhole == 0;
}

static int scripted_water_active(void)
{
    if (s_presentation == NULL || !falcon_controller_selected()) return 0;
    /* Water levels can be entered through native transitions where ownership
     * is briefly SCRIPTED even though gameplay is already back in ordinary
     * GameMode14. Keep the Falcon presentation/OAM suppression up in that
     * narrow state; the adapter reclaims control at the next playable physics
     * seam and continues to own water movement. `$89 >= 4` is persistent
     * entrance metadata, not a still-active pipe, once the pipe timer is zero. */
    return misc_game_mode == 0x14 && player_current_state == 0 &&
           flag_underwater_level != 0 && !active_pipe_handoff() &&
           timer_end_level == 0 && timer_end_level_via_keyhole == 0;
}

static int powerup_animation_active(void)
{
    if (s_presentation == NULL || !falcon_controller_selected() ||
        misc_game_mode != 0x14) return 0;
    /* Native GameMode14 player-state table:
     * 1 PowerDown, 2 Grow, 3 GotCape, 4 GotFlower.  These are transient native
     * animation states, not control states; keep the Falcon mesh/OAM
     * suppression active at full size while SMW finishes its timer bookkeeping. */
    return player_current_state >= 1 && player_current_state <= 4;
}

/* Opt-in, path-free activation trace for TCP validation. The caller chooses
 * the external output file; no ROM/cache path or owner data is ever logged. */
static void trace(const char *event) {
    const char *path = getenv("SNESRECOMP_FALCON_PRESENTATION_TRACE");
    FILE *file;
    if (!path || !*path || !(file = fopen(path, "ab"))) return;
    fprintf(file, "%s\n", event);
    fclose(file);
}

static void note(const char *message) {
    fprintf(stderr, "Falcon presentation disabled: %s\n", message);
    trace(message);
}

static int absolute_path(const char *path) {
    if (!path || !*path) return 0;
#ifdef _WIN32
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/');
#else
    return path[0] == '/';
#endif
}

static void join_path(char *out, size_t out_size, const char *left,
                      const char *right) {
    const size_t n = strlen(left);
    if (n && (left[n - 1] == '/' || left[n - 1] == '\\'))
        snprintf(out, out_size, "%s%s", left, right);
    else
        snprintf(out, out_size, "%s/%s", left, right);
}

static int valid_cache_name(const char *name) {
    const size_t prefix = strlen(FALCON_CACHE_PREFIX);
    size_t i;
    if (!name || strncmp(name, FALCON_CACHE_PREFIX, prefix) != 0 ||
        strlen(name) != prefix + 16) return 0;
    for (i = prefix; i < prefix + 16; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') ||
              (name[i] >= 'a' && name[i] <= 'f'))) return 0;
    return 1;
}

static int verify_pinned_file(const char *path, const uint8_t expected[32], unsigned long max_bytes) {
    FILE *file;
    long length;
    uint8_t *bytes, actual[32];
    int ok = 0;
    if (!path || !(file = fopen(path, "rb"))) return 0;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        (unsigned long)length > max_bytes || fseek(file, 0, SEEK_SET))
        goto done;
    bytes = (uint8_t *)malloc((size_t)length);
    if (!bytes) goto done;
    if (fread(bytes, 1, (size_t)length, file) == (size_t)length) {
        sha256_compute(bytes, (size_t)length, actual);
        ok = memcmp(actual, expected, sizeof(actual)) == 0;
    }
    free(bytes);
done:
    fclose(file);
    return ok;
}

static int verify_audio_cache(const char *cache, char *audio_dir, size_t audio_dir_size) {
    unsigned i;
    char path[1024];
    join_path(audio_dir, audio_dir_size, cache, "audio");
    for (i = 0; i < 11; ++i) {
        join_path(path, sizeof(path), audio_dir, k_audio_filenames[i]);
        if (!verify_pinned_file(path, k_audio_sha256[i], 16u * 1024u * 1024u)) return 0;
    }
    return 1;
}

static int load_final_cache(const char *cache) {
    const char *base;
    char blob[1024], manifest[1024], audio_dir[1024];
    FalconPresentation *loaded;
    if (!absolute_path(cache)) { note("cache path is not absolute"); return 0; }
    base = strrchr(cache, '/');
#ifdef _WIN32
    { const char *backslash = strrchr(cache, '\\'); if (!base || (backslash && backslash > base)) base = backslash; }
#endif
    base = base ? base + 1 : cache;
    if (!valid_cache_name(base)) { note("cache is not an immutable approved final-cache name"); return 0; }
    join_path(manifest, sizeof(manifest), cache, "manifest.json");
    join_path(blob, sizeof(blob), cache, "falcon_runtime.bin");
    { FILE *file = fopen(manifest, "rb");
      if (!file) { note("final cache has no manifest"); return 0; }
      fclose(file); }
    /* The helper validates the full inventory; host additionally pins exactly
     * the approved runtime bytes before the parser sees them. */
    if (!verify_pinned_file(blob, k_runtime_sha256, FALCON_RUNTIME_MAX_BYTES)) { note("runtime blob hash is not approved"); return 0; }
    loaded = falcon_presentation_load_file(blob);
    if (!loaded) { note("runtime blob is malformed"); return 0; }
    falcon_presentation_destroy(s_presentation);
    s_presentation = loaded;
    if (verify_audio_cache(cache, audio_dir, sizeof(audio_dir))) {
        snprintf(s_validated_audio_dir, sizeof(s_validated_audio_dir), "%s", audio_dir);
        s_audio_pending = 1;
    } else {
        note("approved owner cache audio disabled");
    }
    trace("approved runtime cache loaded");
    return 1;
}

static int default_cache_root(char *out, size_t size) {
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    if (!absolute_path(local)) return 0;
    return snprintf(out, size, "%s/SuperMarioWorldRecomp/smash64", local) < (int)size;
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (absolute_path(xdg))
        return snprintf(out, size, "%s/SuperMarioWorldRecomp/smash64", xdg) < (int)size;
    if (!absolute_path(home)) return 0;
    return snprintf(out, size, "%s/.cache/SuperMarioWorldRecomp/smash64", home) < (int)size;
#endif
}

/* Resolve the physical executable, not cwd or APPIMAGE: the helper lives
 * inside the application while state lives outside its read-only mount. */
static int bundled_cache_helper(char *out, size_t size) {
#ifdef _WIN32
    wchar_t executable[1024];
    DWORD n = GetModuleFileNameW(NULL, executable, 1024);
    if (!n || n >= 1024 ||
        !WideCharToMultiByte(CP_UTF8, 0, executable, -1, out, (int)size,
                            NULL, NULL)) return 0;
    const char *leaf = "smw-falcon-cache.exe";
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n <= 0 || (size_t)n >= size - 1) return 0;
    out[n] = '\0';
    const char *leaf = "smw-falcon-cache";
#endif
    char *slash = strrchr(out, '/');
    char *backslash = strrchr(out, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (!slash || (size_t)(slash + 1 - out) + strlen(leaf) >= size) return 0;
    strcpy(slash + 1, leaf);
    return 1;
}

/* Never route owner-controlled paths through a shell. The helper contract is
 * an executable, not a command string or .cmd/.sh wrapper; each path below is
 * one argv item even when it contains shell metacharacters or spaces. */
static int run_cache_helper(const char *helper, const char *owner_rom_path,
                            const char *root, const char *result) {
    char *const argv[] = { (char *)helper, "--rom", (char *)owner_rom_path,
                           "--cache-root", (char *)root, "--result-file",
                           (char *)result, NULL };
#ifdef _WIN32
    /* Windows accepts a command line, even for spawnv. Quote every argv item
     * using the CRT backslash rules; never pass it through a shell. */
    char command[8192], *out = command;
    for (unsigned i = 0; argv[i]; ++i) {
        const char *p = argv[i];
        if ((size_t)(out - command) + 4 >= sizeof(command)) return 0;
        if (i) *out++ = ' ';
        *out++ = '"';
        while (*p) {
            unsigned slashes = 0;
            while (*p == '\\') { ++slashes; ++p; }
            unsigned escaped = (*p == '"' || !*p) ? slashes * 2 : slashes;
            if ((size_t)(out - command) + escaped + 4 >= sizeof(command)) return 0;
            while (escaped--) *out++ = '\\';
            if (*p == '"') *out++ = '\\';
            if (*p) *out++ = *p++;
        }
        *out++ = '"';
    }
    *out = '\0';
    wchar_t wide_helper[1024], wide_command[8192];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, helper, -1,
                             wide_helper, 1024) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command, -1,
                             wide_command, 8192)) return 0;
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION child = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(wide_helper, wide_command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &startup, &child)) return 0;
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return code == 0;
#else
    pid_t child = fork();
    int status;
    if (child < 0) return 0;
    if (child == 0) {
        execv(helper, argv);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static int invoke_cache_helper(const char *owner_rom_path) {
    const char *helper = getenv("SNESRECOMP_FALCON_CACHE_HELPER");
    const char *root = getenv("SNESRECOMP_FALCON_CACHE_ROOT");
    char fallback_root[768], bundled_helper[1024];
    char result[1024], cache[1024], name[256];
    FILE *file;
    int rc;
    if (!root || !*root) {
        if (!default_cache_root(fallback_root, sizeof(fallback_root))) {
            note("user cache directory unavailable"); return 0;
        }
        root = fallback_root;
    }
    if (!helper || !*helper) {
        if (!bundled_cache_helper(bundled_helper, sizeof(bundled_helper))) {
            note("bundled cache helper unavailable"); return 0;
        }
        helper = bundled_helper;
    }
    if (!absolute_path(helper) || !absolute_path(root) || !absolute_path(owner_rom_path)) {
        note("helper, cache root, or committed owner ROM path is not absolute");
        return 0;
    }
    if (snprintf(result, sizeof(result), "%s/.smw-falcon-cache-result-%ld.txt", root,
                 (long)SMW_GETPID()) >= (int)sizeof(result)) return 0;
    rc = run_cache_helper(helper, owner_rom_path, root, result);
    if (!rc) { note("external final-cache helper failed (see helper output)"); return 0; }
    file = fopen(result, "rb");
    if (!file || !fgets(name, sizeof(name), file)) {
        if (file) fclose(file);
        note("external final-cache helper returned no result file");
        return 0;
    }
    fclose(file);
    remove(result);
    name[strcspn(name, "\r\n")] = '\0';
    if (!valid_cache_name(name) || strchr(name, '/') || strchr(name, '\\')) {
        note("external final-cache helper returned an invalid cache basename");
        return 0;
    }
    join_path(cache, sizeof(cache), root, name);
    return load_final_cache(cache);
}

FalconPresentationPose smw_falcon_presentation_pose_for_state(
    int state, unsigned state_frame, float facing) {
    FalconPresentationPose pose;
    pose.frame = (float)state_frame;
    pose.facing_right = facing >= 0.0f;
    switch (state) {
    case FL_JAB: pose.state = FALCON_PRESENT_JAB; break;
    case FL_FTILT: pose.state = FALCON_PRESENT_FTILT; break;
    case FL_ATTACK_AIR_N: pose.state = FALCON_PRESENT_NAIR; break;
    case FL_ATTACK_AIR_F: pose.state = FALCON_PRESENT_FAIR; break;
    case FL_ATTACK_AIR_B: pose.state = FALCON_PRESENT_BAIR; break;
    case FL_ATTACK_AIR_LW: pose.state = FALCON_PRESENT_DAIR; break;
    case FL_WALK_SLOW: case FL_WALK_MIDDLE: case FL_WALK_FAST: pose.state = FALCON_PRESENT_WALK; break;
    case FL_DASH: case FL_RUN: case FL_RUN_BRAKE: pose.state = FALCON_PRESENT_RUN; break;
    case FL_KNEEBEND: case FL_JUMP_F: case FL_JUMP_B:
    case FL_JUMP_AERIAL_F: case FL_JUMP_AERIAL_B: pose.state = FALCON_PRESENT_JUMP; break;
    case FL_FALL: case FL_FALL_AERIAL: case FL_LANDING_LIGHT: case FL_LANDING_HEAVY: pose.state = FALCON_PRESENT_FALL; break;
    case FL_FALCON_PUNCH_GROUND: case FL_FALCON_PUNCH_AIR: pose.state = FALCON_PRESENT_PUNCH; break;
    case FL_FALCON_KICK_GROUND: case FL_FALCON_KICK_GROUND_AIR:
    case FL_FALCON_KICK_LANDING: pose.state = FALCON_PRESENT_KICK; break;
    /* The direct aerial special owns DownSpecialAir, whose kick/fire joint is
     * deliberately pitched down-forward in the source effect manager. */
    case FL_FALCON_KICK_AIR: case FL_FALCON_KICK_BOUND:
        pose.state = FALCON_PRESENT_KICK_AIR; break;
    case FL_FALCON_DIVE_CATCH: pose.state = FALCON_PRESENT_DIVE_CATCH; break;
    case FL_FALCON_DIVE_THROW: pose.state = FALCON_PRESENT_DIVE_THROW; break;
    case FL_FALCON_DIVE_GROUND: case FL_FALCON_DIVE_AIR: case FL_FALCON_DIVE_FALL:
    case FL_FALCON_DIVE_LANDING: pose.state = FALCON_PRESENT_DIVE; break;
    default: pose.state = FALCON_PRESENT_IDLE; break;
    }
    return pose;
}

float smw_falcon_presentation_foot_anchor_y(int player_screen_y) {
    return (float)(player_screen_y + 32);
}

void smw_falcon_presentation_reanchor_oam_group(uint8_t *entries,
                                                unsigned count,
                                                int anchor_x, int anchor_y) {
    unsigned i;
    int min_x = 256, max_x = -1, min_y = 256, max_y = -1;
    if (!entries || !count) return;
    for (i = 0; i < count; ++i) {
        const OamEnt *entry = (const OamEnt *)(entries + i * sizeof(OamEnt));
        if (entry->ypos >= 224u) continue;
        if (entry->xpos < min_x) min_x = entry->xpos;
        if (entry->xpos > max_x) max_x = entry->xpos;
        if (entry->ypos < min_y) min_y = entry->ypos;
        if (entry->ypos > max_y) max_y = entry->ypos;
    }
    if (max_x < min_x || max_y < min_y) return;
    for (i = 0; i < count; ++i) {
        OamEnt *entry = (OamEnt *)(entries + i * sizeof(OamEnt));
        if (entry->ypos >= 224u) continue;
        entry->xpos = (uint8_t)(entry->xpos + anchor_x - (min_x + max_x) / 2);
        entry->ypos = (uint8_t)(entry->ypos + anchor_y - (min_y + max_y) / 2);
    }
}

static int ppu_oam_x(const Ppu *ppu, unsigned slot) {
    return (int)(ppu->oam[slot * 2u] & 0xffu) |
        (((ppu->highOam[slot >> 2u] >> ((slot & 3u) * 2u)) & 1u) << 8u);
}

static void ppu_oam_set_xy(Ppu *ppu, unsigned slot, int x, int y) {
    uint8_t *high = &ppu->highOam[slot >> 2u];
    const uint8_t mask = (uint8_t)(1u << ((slot & 3u) * 2u));
    ppu->oam[slot * 2u] = (uint16_t)((ppu->oam[slot * 2u] & 0xff00u) |
                                      ((unsigned)x & 0xffu));
    ppu->oam[slot * 2u] = (uint16_t)((ppu->oam[slot * 2u] & 0x00ffu) |
                                      (((unsigned)y & 0xffu) << 8u));
    if (x & 0x100) *high |= mask;
    else *high &= (uint8_t)~mask;
}

void smw_falcon_presentation_reanchor_ppu_oam_group(Ppu *ppu,
                                                     unsigned first,
                                                     unsigned count,
                                                     int anchor_x,
                                                     int anchor_y) {
    unsigned i;
    int min_x = 512, max_x = -1, min_y = 256, max_y = -1;
    if (!ppu || !count || first >= 128u || count > 128u - first) return;
    for (i = 0; i < count; ++i) {
        const unsigned slot = first + i;
        const int x = ppu_oam_x(ppu, slot);
        const int y = ppu->oam[slot * 2u] >> 8u;
        if (y >= 224) continue;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    if (max_x < min_x || max_y < min_y) return;
    for (i = 0; i < count; ++i) {
        const unsigned slot = first + i;
        const int x = ppu_oam_x(ppu, slot);
        const int y = ppu->oam[slot * 2u] >> 8u;
        if (y >= 224) continue;
        ppu_oam_set_xy(ppu, slot,
                       x + anchor_x - (min_x + max_x) / 2,
                       y + anchor_y - (min_y + max_y) / 2);
    }
}

unsigned smw_falcon_presentation_normal_sprite_ppu_slot(uint8_t oam_offset) {
    return FALCON_PLAYER_OAM_FIRST +
           (unsigned)oam_offset / (unsigned)sizeof(OamEnt);
}

int smw_falcon_presentation_stunned_shell_ppu_slot(uint8_t restored_offset,
                                                    unsigned *out_slot) {
    const unsigned draw_offset = (unsigned)restored_offset + 8u;
    if (!out_slot || draw_offset > 0xfcu) return 0;
    *out_slot = smw_falcon_presentation_normal_sprite_ppu_slot(
        (uint8_t)draw_offset);
    return 1;
}

/* `$15EA` is a completed normal-sprite OAM allocation.  By this point the
 * status-$0B routine has already updated native sprite positions, throw
 * state, collisions and despawn.  Touching just these finished OAM entries
 * therefore moves the visible carried shell/card without changing its SMW
 * lifecycle.  The final renderer seam runs after guest OAM DMA and changes
 * the transient PPU OAM copy, never guest WRAM. Stock
 * StunnedShellDraw ($01:9806) temporarily advances its `$15EA` allocation by
 * eight bytes, writes two 16x16 OAM entries at `$0300+Y` and `$0304+Y`, then
 * restores `$15EA`. Do not infer a wider group: its next entry can belong to
 * another sprite. */
static void relocate_carried_oam(Ppu *ppu, const FalconPresentationPose *pose) {
    FalconPresentationTarget target;
    float hand_x, hand_y;
    unsigned slot;
    memset(&target, 0, sizeof(target));
    target.width = kPpuBufWidth;
    target.height = 240;
    target.anchor_x = (float)((kPpuBufWidth - 256) / 2 +
                              (int16_t)player_on_screen_pos_x + 8);
    target.anchor_y = smw_falcon_presentation_foot_anchor_y(
        (int16_t)player_on_screen_pos_y);
    target.scale = 1.0f;
    target.yaw_degrees = 88.0f;
    if (!falcon_presentation_joint_screen_position(
            s_presentation, pose, &target, FALCON_PRESENT_JOINT_CARRY_HAND,
            &hand_x, &hand_y)) return;
    /* The completed two-tile shell is reanchored by its centre.  The hand
     * joint is an attachment pivot instead, so centre-on-joint leaves the
     * card visibly forward of Falcon.  Bring it one small native-pixel step
     * inward and upward; invert the inset with facing to preserve symmetry. */
    hand_x += pose->facing_right ? -FALCON_CARRIED_SHELL_BODY_INSET
                                 : FALCON_CARRIED_SHELL_BODY_INSET;
    hand_y -= FALCON_CARRIED_SHELL_HAND_RAISE;
    /* OAM is still in native 256-wide coordinates; the renderer adds the
     * centred widescreen margin. */
    hand_x -= (float)((kPpuBufWidth - 256) / 2);
    for (slot = 0; slot != 12; ++slot) {
        unsigned first;
        /* Shell IDs $04-$07 take the proven StunnedShellDraw two-entry path.
         * Other carried sprites have their own renderer/OAM contracts and are
         * intentionally left entirely native until individually audited. */
        if (spr_current_status[slot] != 0x0b || spr_spriteid[slot] < 0x04u ||
            spr_spriteid[slot] > 0x07u) continue;
        /* `$15EA` is restored after StunnedShellDraw, so the finalized pair
         * is eight bytes beyond the value visible here. The live save-2
         * shell retains $E4 while its rendered pair begins at $0300+$EC:
         * absolute PPU slot 64 + $EC/4 = 123. */
        if (!smw_falcon_presentation_stunned_shell_ppu_slot(
                spr_oamindex[slot], &first)) continue;
        if (first > 126u) continue;
        smw_falcon_presentation_reanchor_ppu_oam_group(
            ppu, first, 2u, (int)(hand_x + .5f), (int)(hand_y + .5f));
    }
}

static const char *controllable_reason(void) {
    if (!s_presentation) return "cache unavailable";
    if (!falcon_controller_selected()) return "Falcon controller inactive";
    if (snes_foreign_ownership() != FOREIGN_OWNERSHIP_FOREIGN) return "controller handoff";
    if (misc_game_mode != 0x14) return "not level gameplay";
    if (player_current_state != 0) return "nonordinary player state";
    if (active_pipe_handoff()) return "pipe handoff";
    if (timer_end_level || timer_end_level_via_keyhole) return "goal handoff";
    return "active";
}

static int controllable(void) {
    const char *reason = controllable_reason();
    if (strcmp(reason, s_last_gate)) {
        snprintf(s_last_gate, sizeof(s_last_gate), "%s", reason);
        trace(reason);
    }
    return !strcmp(reason, "active");
}

static int presentation_active(void)
{
    return controllable() || death_active() || course_clear_active() ||
           powerup_animation_active() || scripted_water_active() ||
           scripted_pipe_active();
}

void smw_falcon_presentation_reset(void) {
    /* Voices are host-owned and intentionally excluded from savestates. Reset
     * (including activation/reload) drops them before state becomes visible. */
    if (smw_falcon_audio_is_active()) smw_falcon_audio_reset();
    falcon_presentation_destroy(s_presentation);
    s_presentation = NULL;
    s_bound = 0;
    s_suppression_active = 0;
    s_suppression_failed = 0;
    s_mesh_draw_active = 0;
    s_last_gate[0] = '\0';
    s_validated_audio_dir[0] = '\0';
    s_audio_pending = 0;
    s_audio_attempted = 0;
    s_death_latched = 0; s_death_hidden = 0;
    s_death_frame = 0; s_death_anchor_y = 0.0f;
    s_last_pose.state = FALCON_PRESENT_IDLE; s_last_pose.frame = 0.0f; s_last_pose.facing_right = 1;
}

void smw_falcon_presentation_audio_ready(void) {
    if (!s_audio_pending || s_audio_attempted) return;
    s_audio_attempted = 1;
    s_audio_pending = 0;
    if (smw_falcon_audio_activate(s_validated_audio_dir)) trace("audio active");
    else note("approved owner cache audio disabled");
    s_validated_audio_dir[0] = '\0';
}

int smw_falcon_presentation_activate(const char *owner_rom_path) {
    const char *cache = getenv("SNESRECOMP_FALCON_CACHE");
    smw_falcon_presentation_reset();
    trace("activation requested");
    if (!owner_rom_path || !absolute_path(owner_rom_path)) { note("committed owner ROM path unavailable"); return 0; }
    if (cache && *cache) return load_final_cache(cache);
    return invoke_cache_helper(owner_rom_path);
}

int smw_falcon_presentation_is_active(void) { return presentation_active(); }

int smw_falcon_presentation_root_delta(const char *animation, float frame,
                                       float *delta_y, float *delta_z) {
    if (delta_y != NULL) *delta_y = 0.0f;
    if (delta_z != NULL) *delta_z = 0.0f;
    if (s_presentation == NULL || animation == NULL) return 0;
    return falcon_presentation_root_delta(s_presentation, animation, frame,
                                          delta_y, delta_z);
}

void smw_falcon_presentation_prepare_ppu(Ppu *ppu) {
    if (!ppu) return;
    PpuClearOverlayCaptures(ppu);
    if (!presentation_active()) { s_suppression_active = 0; return; }
    if (!s_bound) {
        if (!PpuBindOverlaySurface(ppu, kPpuOverlaySource_Obj,
                                   (uint8_t *)s_obj_scratch,
                                   kPpuBufWidth * sizeof(uint32_t))) {
            if (!s_suppression_failed) {
                s_suppression_failed = 1;
                note("could not bind narrow OBJ suppression surface");
            }
            return;
        }
        s_bound = 1;
        trace("player OBJ suppression bound");
    }
    if (!PpuSetOverlayCapture(ppu, kPpuOverlaySource_Obj, -128, 0, 512, 224,
                              kPpuOverlayFlag_RemoveFromGame) ||
        !PpuSetOverlayOamRange(ppu, FALCON_PLAYER_OAM_FIRST,
                               FALCON_PLAYER_OAM_COUNT)) {
        s_suppression_active = 0;
        if (!s_suppression_failed) {
            s_suppression_failed = 1;
            note("could not suppress the player OBJ range");
        }
    } else if (!s_suppression_active) {
        s_suppression_failed = 0;
        s_suppression_active = 1;
        trace("player OBJ suppression active");
    }
}

void smw_falcon_presentation_finalize_ppu_oam(Ppu *ppu) {
    const ForeignState *state;
    FalconPresentationPose pose;
    if (!ppu || !controllable()) return;
    state = snes_foreign_state();
    if (!state) return;
    pose = smw_falcon_presentation_pose_for_state(
        state->state, state->state_frame, state->facing);
    relocate_carried_oam(ppu, &pose);
}

void smw_falcon_presentation_present(uint8_t *pixels, size_t pitch,
                                     int width, int height) {
    const ForeignState *state;
    FalconPresentationTarget target;
    FalconPresentationPose pose;
    if (!presentation_active() || !pixels || pitch % sizeof(uint32_t)) {
        s_mesh_draw_active = 0;
        return;
    }
    state = snes_foreign_state();
    if (!state && !death_active()) return;
    memset(&target, 0, sizeof(target));
    target.framebuffer = (uint32_t *)pixels;
    target.width = width;
    target.height = height;
    target.pitch_pixels = (int)(pitch / sizeof(uint32_t));
    target.anchor_x = (float)(SmwRendererNativeOffset() + (int16_t)player_on_screen_pos_x + 8);
    /* $80 is PlayerGFXRt's 32px sprite origin. The projected mesh foot plane
     * contacts the terrain at its native $80 + 32 foot baseline. */
    target.anchor_y = smw_falcon_presentation_foot_anchor_y(
        (int16_t)player_on_screen_pos_y);
    target.scale = 1.0f;
    /* Mature NES port convention: Captain's authored front/back axis must be
     * yawed 88 degrees into the 2D host plane for readable left/right profile. */
    target.yaw_degrees = 88.0f;
    if (course_clear_active()) {
        /* Native SMW is driving the end-level walk/score script.  Keep the
         * parody readable by rendering Captain's slow Walk2 pose instead of
         * the native Mario peace/score sprite.  The approved runtime cache
         * currently exposes Wait/Walk variants but no Captain appeal/taunt
         * animation, so the requested salute remains a future cache-export
         * addition rather than a fabricated pose. */
        pose.state = FALCON_PRESENT_WALK;
        pose.frame = (float)(counter_global_frames >> 1);
        pose.facing_right = player_facing_direction != 0;
        s_last_pose = pose;
    } else if (powerup_animation_active()) {
        /* The source controller is intentionally frozen while native SMW
         * runs powerup/powerdown animation states. Do not leave a stale
         * Punch/Kick frame or expose native Mario; render a stable,
         * native-facing full-size Wait pose instead. */
        pose.state = FALCON_PRESENT_IDLE;
        pose.frame = 0.0f;
        pose.facing_right = player_facing_direction != 0;
        s_last_pose = pose;
    } else if ((scripted_water_active() || scripted_pipe_active()) &&
               !controllable()) {
        pose = s_last_pose;
        pose.facing_right = player_facing_direction != 0;
    } else if (state) {
        pose = smw_falcon_presentation_pose_for_state(
            state->state, state->state_frame, state->facing);
        s_last_pose = pose;
    } else pose = s_last_pose;
    if (death_active()) {
        if (!s_death_latched) {
            s_death_latched = 1; s_death_hidden = 0;
            s_death_frame = 0; s_death_anchor_y = target.anchor_y;
        }
        /* The mature NES port maps Smash's unreadable Star-KO depth travel to
         * a gently accelerating screen-space fall. Its renderer uses a
         * positive-up coordinate system; this framebuffer is positive-down. */
        target.anchor_y = s_death_anchor_y +
            (.30f * s_death_frame + .018f * s_death_frame * s_death_frame);
        target.tumble_radians = s_death_frame * (18.0f * 3.14159265358979323846f / 180.0f);
        target.tumble_center_y = -16.0f; /* NES render_death_vertex torso midpoint. */
        pose.state = FALCON_PRESENT_FALL; pose.frame = s_death_frame++ * .5f;
        if (target.anchor_y > (float)height + 64.0f) s_death_hidden = 1;
        if (s_death_hidden) { s_mesh_draw_active = 0; return; }
    } else if (controllable()) {
        /* Only genuine ordinary control completes the death sequence. This
         * prevents a transient scripted handoff from restarting it on-screen. */
        s_death_latched = 0; s_death_hidden = 0; s_death_frame = 0;
    }
    if (!falcon_presentation_draw(s_presentation, &pose, &target)) {
        s_mesh_draw_active = 0;
        note("mesh compositor rejected the current target or pose");
    } else if (!s_mesh_draw_active) {
        s_mesh_draw_active = 1;
        trace("mesh compositor active");
    }
}
