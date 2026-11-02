#include "smw_netplay_lobby.h"

#if SNESRECOMP_NET
#include "snes_host_lobby.h"

/* The shared backend owns accounts, online rooms, LAN discovery and ICE.
 * Keeping a private callback table here dropped sign-in entirely. */
static void InitLobby(void) {
  static int initialized;
  if (!initialized) {
    SnesHostLobbyIdentity id = {0};
    SnesHostLobbyOpts opts = {0};
#ifdef SMW_COOP_BUILD
    id.game_name = "Super Mario World Co-op";
    id.game_version = "smw-coop-netplay-1";
#else
    id.game_name = "Super Mario World";
    id.game_version = "smw-native-netplay-1/" SNESRECOMP_BUILD_VERSION;
#endif
    id.default_lobby_name = "Super Mario World Lobby";
    id.lan_registry_path = "netplay_lan_lobby.txt";
    opts.auto_ready_guests = 1;
    opts.max_players = 2;
    initialized = snes_host_lobby_init(&id, &opts) == 0;
  }
}

const RecompLauncherCNetplayCallbacks *SmwNetplayLauncherCallbacks(void) {
  InitLobby();
  return snes_host_lobby_callbacks();
}
int SmwNetplayLauncherAutoLaunch(const char *role, const char *player_name,
                                 const char *lobby_name, unsigned timeout_ms,
                                 RecompLauncherCNetplayLaunch *out) {
  InitLobby();
  return snes_host_lobby_auto_launch(role, player_name, lobby_name, timeout_ms, out);
}
void SmwNetplayLauncherPrepareRematch(void) {
  snes_host_lobby_prepare_rematch();
}
const char *SmwNetplayLauncherResumeEndpoint(void) {
  return snes_host_lobby_resume_endpoint();
}
void SmwNetplayLauncherSetRuntimeError(const char *error_code) {
  snes_host_lobby_set_runtime_error(error_code);
}
void SmwNetplayLauncherDisconnect(void) {
  snes_host_lobby_disconnect();
}
#endif
