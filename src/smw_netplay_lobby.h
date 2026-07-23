#ifndef SMW_NETPLAY_LOBBY_H
#define SMW_NETPLAY_LOBBY_H

#ifdef SMW_COOP_BUILD

#include "recomp_launcher.h"

/* recomp-ui owns the lobby presentation. These callbacks adapt its generic
 * two-player UI to snesrecomp's lobby client for the SMW co-op build. */
const RecompLauncherCNetplayCallbacks *SmwNetplayLauncherCallbacks(void);
/* Deterministic integration-test path for exercising the deployed online
 * lobby and ICE signaling without automating ImGui. The lobby connection is
 * intentionally left open after success because it relays ICE signals. */
int SmwNetplayLauncherAutoLaunch(const char *role, const char *player_name,
                                 const char *lobby_name,
                                 unsigned timeout_ms,
                                 RecompLauncherCNetplayLaunch *out);
/* Reset a completed match back to the existing waiting room without dropping
 * the lobby connection or LAN registry seat. */
void SmwNetplayLauncherPrepareRematch(void);
/* Endpoint shown by recomp-ui when resuming a local/LAN room, or NULL for an
 * online lobby whose header should show the lobby-server URL. */
const char *SmwNetplayLauncherResumeEndpoint(void);
/* Surface a game-session failure when recomp-ui resumes the waiting room. */
void SmwNetplayLauncherSetRuntimeError(const char *error_code);
void SmwNetplayLauncherDisconnect(void);

#endif

#endif /* SMW_NETPLAY_LOBBY_H */
