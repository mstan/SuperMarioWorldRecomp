#include "smw_netplay_lobby.h"

#ifdef SMW_COOP_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "recomp_net/address.h"
#include "recomp_net/lan_lobby.h"
#include "snes_lobby_client.h"

#define SMW_NETPLAY_GAME "Super Mario World Co-op"
#define SMW_NETPLAY_MAX_LOCAL_ADDRESSES 32

static char g_lobby_url[256];
static char g_external_ip[RNET_IPV4_ADDRESS_TEXT_MAX];
static RNetIpv4Address g_local_addresses[SMW_NETPLAY_MAX_LOCAL_ADDRESSES];
static int g_local_address_count;
static int g_hosting_lan;
static int g_joined_lan;
static int g_remote_ready_requested;
static RecompLauncherCNetplayLaunch g_lan_launch;

static int LobbyDebugEnabled(void) {
  const char *value = getenv("SNES_NET_DEBUG");
  return value && value[0] && strcmp(value, "0") != 0;
}

static const char *LanLobbyPath(void) {
  return "netplay_lan_lobby.txt";
}

static int ReadLanState(RNetLanLobby *state) {
  return rnet_lan_lobby_read(LanLobbyPath(), SMW_NETPLAY_GAME,
                             SNES_GAME_VERSION, state) == RNET_LAN_LOBBY_OK;
}

static int CreateLanState(const char *name, const char *endpoint,
                          const char *password) {
  RNetLanLobby state;
  const char *player_name = snes_lobby_display_name();
  char advertised[RNET_LAN_LOBBY_ENDPOINT_MAX];
  const char *stored_endpoint = endpoint;
  memset(&state, 0, sizeof(state));
  snprintf(state.name, sizeof(state.name), "%s",
           name && name[0] ? name : "SMW Co-op Lobby");
  snprintf(state.game, sizeof(state.game), "%s", SMW_NETPLAY_GAME);
  snprintf(state.game_version, sizeof(state.game_version), "%s",
           SNES_GAME_VERSION);
  /* Online hosts bind all interfaces (0.0.0.0), but that wildcard is not a
   * routable guest destination. The remote service keeps the wildcard bind;
   * the merged LAN row advertises the preferred concrete local address. */
  if (endpoint && strncmp(endpoint, "0.0.0.0:", 8) == 0) {
    RNetIpv4Address address;
    if (rnet_ipv4_enumerate(&address, 1) > 0 && address.address[0]) {
      snprintf(advertised, sizeof(advertised), "%s:%s", address.address,
               endpoint + 8);
      stored_endpoint = advertised;
    }
  }
  snprintf(state.endpoint, sizeof(state.endpoint), "%s",
           stored_endpoint && stored_endpoint[0]
               ? stored_endpoint : "127.0.0.1:7777");
  snprintf(state.host_name, sizeof(state.host_name), "%s",
           player_name && player_name[0] ? player_name : "Host");
  snprintf(state.password, sizeof(state.password), "%s",
           password ? password : "");
  state.host_slot = 0;
  if (rnet_lan_lobby_publish(LanLobbyPath(), &state) !=
      RNET_LAN_LOBBY_OK)
    return 0;
  g_hosting_lan = 1;
  g_joined_lan = 0;
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
  return 1;
}

static int FillLanLobbyRow(RecompLauncherCNetplayLobby *out) {
  RNetLanLobby state;
  if (!out || !ReadLanState(&state)) return 0;
  memset(out, 0, sizeof(*out));
  snprintf(out->lobby_id, sizeof(out->lobby_id), "lan:%s", state.endpoint);
  snprintf(out->name, sizeof(out->name), "LAN - %s",
           state.name[0] ? state.name : "Lobby");
  snprintf(out->game_name, sizeof(out->game_name), "%s", state.game);
  snprintf(out->game_version, sizeof(out->game_version), "%s",
           state.game_version);
  out->player_count = state.joiner_name[0] ? 2 : 1;
  out->max_slots = 2;
  out->has_password = state.password[0] != '\0';
  return 1;
}

static int UseLanMembers(RNetLanLobby *state) {
  RNetLanLobby local;
  if (!state) state = &local;
  if (!ReadLanState(state)) return 0;
  if (g_joined_lan) return 1;
  if (!g_hosting_lan) return 0;
  /* Prefer the remote room once it has two seated members. Until then the
   * host-owned record makes the waiting room immediate and deterministic. */
  return state->joiner_name[0] || snes_lobby_member_count() < 2;
}

static int UdpEndpointAvailable(const char *endpoint) {
  char host[64];
  const char *colon;
  char *end;
  long port;
  struct sockaddr_in address;
#ifdef _WIN32
  static int winsock_started;
  SOCKET sock;
  if (!winsock_started) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    winsock_started = 1;
  }
#else
  int sock;
#endif

  if (!endpoint || !(colon = strrchr(endpoint, ':')) || colon == endpoint)
    return 0;
  if ((size_t)(colon - endpoint) >= sizeof(host)) return 0;
  memcpy(host, endpoint, (size_t)(colon - endpoint));
  host[colon - endpoint] = '\0';
  port = strtol(colon + 1, &end, 10);
  if (*end || port < 1 || port > 65535) return 0;

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  if (inet_pton(AF_INET, host, &address.sin_addr) != 1) return 0;
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (sock == INVALID_SOCKET) return 0;
  if (bind(sock, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    closesocket(sock);
    return 0;
  }
  closesocket(sock);
#else
  if (sock < 0) return 0;
  if (bind(sock, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    close(sock);
    return 0;
  }
  close(sock);
#endif
  return 1;
}

static int SelectAvailableEndpoint(char *endpoint) {
  char *colon;
  long port;
  int online;
  if (!endpoint || !endpoint[0]) return 0;
  colon = strrchr(endpoint, ':');
  if (!colon || !colon[1]) return 0;
  port = strtol(colon + 1, NULL, 10);
  if (port < 1 || port > 65535) return 0;
  online = strncmp(endpoint, "0.0.0.0:", 8) == 0;
  if (!online)
    return UdpEndpointAvailable(endpoint);

  /* Online hosting may transparently move to a nearby free port. The recomp-ui
   * callback contract guarantees at least 64 writable bytes in endpoint. */
  for (int i = 0; i < 32 && port + i <= 65535; i++) {
    char candidate[64];
    snprintf(candidate, sizeof(candidate), "0.0.0.0:%ld", port + i);
    if (UdpEndpointAvailable(candidate)) {
      snprintf(endpoint, 64, "%s", candidate);
      return 1;
    }
  }
  return 0;
}

static SnesLobbyMatchCaps MatchCaps(const RecompLauncherCSettings *settings) {
  SnesLobbyMatchCaps caps;
  memset(&caps, 0, sizeof(caps));
  caps.valid = 1;
  caps.widescreen = 0;
  caps.widescreen_hud = 0;
  caps.ignore_aspect = settings ? settings->ignore_aspect != 0 : 0;
  caps.input_delay = 2;
  caps.ws_extra = 0;
  return caps;
}

static const char *DefaultUrl(void *ctx) {
  (void)ctx;
  return g_lobby_url[0] ? g_lobby_url : snes_lobby_default_url();
}

static void SetUrl(void *ctx, const char *url) {
  (void)ctx;
  snprintf(g_lobby_url, sizeof(g_lobby_url), "%s",
           url && url[0] ? url : snes_lobby_default_url());
}

static int Connect(void *ctx) {
  (void)ctx;
  snes_lobby_set_game_identity(SMW_NETPLAY_GAME, SNES_GAME_VERSION);
  return snes_lobby_connect(DefaultUrl(NULL));
}

static int Connected(void *ctx) {
  (void)ctx;
  return snes_lobby_connected();
}

static void Pump(void *ctx) {
  (void)ctx;
  snes_lobby_pump();
  /* The current room UI treats a connected guest as ready and only exposes
   * Play to the host, while the deployed lobby service still requires both
   * sockets to send set_ready before accepting start. Bridge that protocol
   * detail here so the UI remains a simple wait-room flow. */
  if (!snes_lobby_in_lobby()) {
    g_remote_ready_requested = 0;
  } else if (!g_joined_lan && !snes_lobby_is_host() &&
             snes_lobby_member_count() >= 2) {
    if (snes_lobby_local_ready()) {
      g_remote_ready_requested = 1;
    } else if (!g_remote_ready_requested && snes_lobby_set_ready(1) == 0) {
      g_remote_ready_requested = 1;
      if (LobbyDebugEnabled())
        fprintf(stderr, "[netplay lobby] remote guest auto-ready\n");
    }
  }
}

static void SetPlayerName(void *ctx, const char *name) {
  (void)ctx;
  snes_lobby_set_display_name(name && name[0] ? name : "Player");
}

static const char *PlayerName(void *ctx) {
  (void)ctx;
  return snes_lobby_display_name();
}

static void RequestList(void *ctx) {
  (void)ctx;
  snes_lobby_request_list();
}

static int ListCount(void *ctx) {
  RecompLauncherCNetplayLobby lan;
  (void)ctx;
  return snes_lobby_list_count() + (FillLanLobbyRow(&lan) ? 1 : 0);
}

static int ListGet(void *ctx, int index, RecompLauncherCNetplayLobby *out) {
  SnesLobbyRow row;
  int remote_count;
  (void)ctx;
  if (!out || index < 0) return 0;
  remote_count = snes_lobby_list_count();
  if (index >= remote_count)
    return index == remote_count ? FillLanLobbyRow(out) : 0;
  if (!snes_lobby_list_get(index, &row)) return 0;
  memset(out, 0, sizeof(*out));
  snprintf(out->lobby_id, sizeof(out->lobby_id), "%s", row.lobby_id);
  snprintf(out->name, sizeof(out->name), "%s", row.name);
  snprintf(out->game_name, sizeof(out->game_name), "%s", row.game_name);
  snprintf(out->game_version, sizeof(out->game_version), "%s",
           row.game_version);
  out->player_count = row.player_count;
  out->max_slots = row.max_slots;
  out->has_password = row.has_password;
  return 1;
}

static int RefreshLocalAddresses(void) {
  int count = rnet_ipv4_enumerate(g_local_addresses,
                                  SMW_NETPLAY_MAX_LOCAL_ADDRESSES);
  if (count < 0) count = 0;
  if (count > SMW_NETPLAY_MAX_LOCAL_ADDRESSES)
    count = SMW_NETPLAY_MAX_LOCAL_ADDRESSES;
  g_local_address_count = count;
  return count;
}

static int LocalAddressGet(void *ctx, int index,
                           RecompLauncherCNetplayLocalAddress *out) {
  (void)ctx;
  if (!out || index < 0) return 0;
  if (index == 0) RefreshLocalAddresses();
  if (index >= g_local_address_count) return 0;
  memset(out, 0, sizeof(*out));
  snprintf(out->address, sizeof(out->address), "%s",
           g_local_addresses[index].address);
  snprintf(out->label, sizeof(out->label), "%s",
           g_local_addresses[index].interface_label);
  return 1;
}

static int LocalIp(void *ctx, char *out, size_t out_len) {
  RecompLauncherCNetplayLocalAddress address;
  if (!out || !out_len || !LocalAddressGet(ctx, 0, &address)) return 0;
  snprintf(out, out_len, "%s", address.address);
  return out[0] != '\0';
}

static int ExternalIp(void *ctx, char *out, size_t out_len) {
  RNetExternalIpv4Config config;
  int rc;
  (void)ctx;
  if (!out || !out_len) return 0;
  if (!g_external_ip[0]) {
    rnet_external_ipv4_config_init(&config);
    config.timeout_ms = 900;
    rc = rnet_external_ipv4_discover(&config, g_external_ip,
                                     sizeof(g_external_ip));
    if (rc != RNET_EXTERNAL_IPV4_OK) {
      snprintf(out, out_len, "Unavailable");
      return 0;
    }
  }
  snprintf(out, out_len, "%s", g_external_ip);
  return out[0] != '\0';
}

static int Create(void *ctx, const char *name, char *host_endpoint,
                  const char *password,
                  const RecompLauncherCSettings *settings, int lan_only) {
  SnesLobbyMatchCaps caps = MatchCaps(settings);
  const char *endpoint;
  (void)ctx;
  if (!host_endpoint || !host_endpoint[0]) return -1;
  g_remote_ready_requested = 0;
  endpoint = host_endpoint;
  if (lan_only) {
    return CreateLanState(name, endpoint, password) ? 0 : -1;
  }
  g_hosting_lan = 0;
  g_joined_lan = 0;
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
  return snes_lobby_create(name && name[0] ? name : "SMW Co-op Lobby",
                           SMW_NETPLAY_GAME, SNES_GAME_VERSION,
                           password ? password : "",
                           endpoint, &caps);
}

static int Join(void *ctx, const char *lobby_id, const char *password) {
  RNetLanLobby state;
  char guest_endpoint[64];
  const char *name;
  (void)ctx;
  g_remote_ready_requested = 0;
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
  if (lobby_id && strncmp(lobby_id, "lan:", 4) == 0) {
    name = snes_lobby_display_name();
    if (rnet_lan_lobby_join(LanLobbyPath(), SMW_NETPLAY_GAME,
                            SNES_GAME_VERSION, password ? password : "",
                            name && name[0] ? name : "Player", &state) !=
        RNET_LAN_LOBBY_OK)
      return -1;
    g_hosting_lan = 0;
    g_joined_lan = 1;
    return 0;
  }
  g_hosting_lan = 0;
  g_joined_lan = 0;
  g_remote_ready_requested = 0;
  snprintf(guest_endpoint, sizeof(guest_endpoint), "0.0.0.0:7778");
  if (!SelectAvailableEndpoint(guest_endpoint)) return -4;
  return snes_lobby_join(lobby_id, password ? password : "", guest_endpoint);
}

static int Leave(void *ctx) {
  int rc;
  (void)ctx;
  if (g_hosting_lan)
    (void)rnet_lan_lobby_leave(LanLobbyPath(), 1);
  else if (g_joined_lan)
    (void)rnet_lan_lobby_leave(LanLobbyPath(), 0);
  g_hosting_lan = 0;
  g_joined_lan = 0;
  g_remote_ready_requested = 0;
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
  rc = snes_lobby_leave();
  return rc;
}

static int InLobby(void *ctx) {
  (void)ctx;
  return g_hosting_lan || g_joined_lan || snes_lobby_in_lobby();
}

static int IsHost(void *ctx) {
  (void)ctx;
  if (g_hosting_lan || g_joined_lan) return g_hosting_lan ? 1 : 0;
  return snes_lobby_is_host();
}

static int MemberCount(void *ctx) {
  (void)ctx;
  RNetLanLobby state;
  return UseLanMembers(&state) ? 2 : snes_lobby_member_count();
}

static int MemberGet(void *ctx, int index,
                     RecompLauncherCNetplayMember *out) {
  SnesLobbyMember member;
  RNetLanLobby state;
  (void)ctx;
  if (!out) return 0;
  memset(out, 0, sizeof(*out));
  if (UseLanMembers(&state)) {
    if (index < 0 || index > 1) return 0;
    out->slot = index == 0 ? state.host_slot : 1 - state.host_slot;
    out->ready = index == 0 || state.joiner_name[0] != '\0';
    out->is_host = index == 0;
    snprintf(out->display_name, sizeof(out->display_name), "%s",
             index == 0 ? state.host_name : state.joiner_name);
    return 1;
  }
  if (!snes_lobby_member_get(index, &member)) return 0;
  out->slot = member.slot;
  out->ready = member.ready;
  out->is_host = member.slot == 0;
  snprintf(out->display_name, sizeof(out->display_name), "%s",
           member.display_name);
  return 1;
}

static int MoveMember(void *ctx, int from_slot, int to_slot) {
  RNetLanLobby state;
  (void)ctx;
  if (g_hosting_lan && from_slot >= 0 && from_slot <= 1 &&
      to_slot >= 0 && to_slot <= 1 && from_slot != to_slot &&
      ReadLanState(&state))
    return rnet_lan_lobby_set_host_slot(LanLobbyPath(),
                                        1 - state.host_slot);
  return -1;
}

static int KickMember(void *ctx, int slot) {
  RNetLanLobby state;
  (void)ctx;
  if (UseLanMembers(&state)) {
    if (!g_hosting_lan || !state.joiner_name[0] ||
        slot != 1 - state.host_slot)
      return -1;
    return rnet_lan_lobby_leave(LanLobbyPath(), 0);
  }
  return snes_lobby_kick(slot);
}

static int LocalReady(void *ctx) {
  RNetLanLobby state;
  (void)ctx;
  if (UseLanMembers(&state)) return 1;
  return snes_lobby_local_ready();
}

static int AllReady(void *ctx) {
  (void)ctx;
  RNetLanLobby state;
  if (UseLanMembers(&state)) return state.joiner_name[0] != '\0';
  return snes_lobby_all_ready();
}

static int SetReady(void *ctx, int ready) {
  RNetLanLobby state;
  int use_lan;
  (void)ctx;
  use_lan = UseLanMembers(&state);
  if (LobbyDebugEnabled())
    fprintf(stderr, "[netplay lobby] set_ready=%d route=%s\n",
            ready, use_lan ? "lan" : "server");
  if (use_lan) return 0;
  return snes_lobby_set_ready(ready);
}

static int RequestStart(void *ctx,
                        const RecompLauncherCSettings *settings) {
  SnesLobbyMatchCaps caps = MatchCaps(settings);
  RNetLanLobby state;
  int use_lan;
  (void)ctx;
  use_lan = UseLanMembers(&state);
  if (LobbyDebugEnabled())
    fprintf(stderr, "[netplay lobby] request_start route=%s remote_members=%d\n",
            use_lan ? "lan" : "server", snes_lobby_member_count());
  if (g_hosting_lan && use_lan && state.joiner_name[0])
    return rnet_lan_lobby_set_started(LanLobbyPath(), 1);
  return snes_lobby_request_start(&caps);
}

static int LaunchPending(void *ctx) {
  RNetLanLobby state;
  const char *colon;
  const char *port;
  (void)ctx;
  if ((g_hosting_lan || g_joined_lan) && !g_lan_launch.enabled &&
      ReadLanState(&state) && state.started) {
    memset(&g_lan_launch, 0, sizeof(g_lan_launch));
    g_lan_launch.enabled = 1;
    g_lan_launch.local_slot = g_hosting_lan
                                ? state.host_slot : 1 - state.host_slot;
    g_lan_launch.input_player = 0;
    g_lan_launch.session_id = 1;
    g_lan_launch.input_delay = 2;
    if (g_hosting_lan) {
      colon = strrchr(state.endpoint, ':');
      port = colon ? colon + 1 : "7777";
      snprintf(g_lan_launch.bind_hostport,
               sizeof(g_lan_launch.bind_hostport), "0.0.0.0:%s", port);
    } else {
      snprintf(g_lan_launch.bind_hostport,
               sizeof(g_lan_launch.bind_hostport), "0.0.0.0:0");
      snprintf(g_lan_launch.peer_hostport,
               sizeof(g_lan_launch.peer_hostport), "%s", state.endpoint);
    }
  }
  return g_lan_launch.enabled || snes_lobby_launch_pending();
}

static void ClearLaunchPending(void *ctx) {
  (void)ctx;
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
  snes_lobby_clear_launch_pending();
}

static int FillLaunch(void *ctx, RecompLauncherCNetplayLaunch *out) {
  const SnesLobbyJoinInfo *join;
  const SnesLobbyMatchCaps *caps;
  (void)ctx;
  if (!out) return 0;
  if (g_lan_launch.enabled) {
    *out = g_lan_launch;
    return 1;
  }
  join = snes_lobby_join_info();
  if (!join || !join->ok) return 0;
  caps = snes_lobby_match_caps();
  memset(out, 0, sizeof(*out));
  out->enabled = 1;
  out->local_slot = join->local_slot;
  out->input_player = 0;
  out->session_id = join->session_id;
  out->input_delay = caps && caps->valid ? caps->input_delay : 2;
  snprintf(out->bind_hostport, sizeof(out->bind_hostport), "%s",
           join->bind_hostport);
  snprintf(out->peer_hostport, sizeof(out->peer_hostport), "%s",
           join->peer_hostport);
  return 1;
}

static RecompLauncherCNetplayCallbacks g_callbacks = {
    NULL, DefaultUrl, SetUrl, Connect, Connected, Pump, SetPlayerName,
    PlayerName, RequestList, ListCount, ListGet, LocalIp, ExternalIp, Create,
    Join, Leave, InLobby, IsHost, MemberCount, MemberGet, MoveMember, LocalReady,
    AllReady, SetReady, RequestStart, LaunchPending, ClearLaunchPending,
    FillLaunch, LocalAddressGet, KickMember};

const RecompLauncherCNetplayCallbacks *SmwNetplayLauncherCallbacks(void) {
  return &g_callbacks;
}

void SmwNetplayLauncherPrepareRematch(void) {
  if (g_hosting_lan || g_joined_lan)
    (void)rnet_lan_lobby_set_started(LanLobbyPath(), 0);
  (void)snes_lobby_set_ready(0);
  g_remote_ready_requested = 0;
  snes_lobby_clear_launch_pending();
  memset(&g_lan_launch, 0, sizeof(g_lan_launch));
}

const char *SmwNetplayLauncherResumeEndpoint(void) {
  static char endpoint[RNET_LAN_LOBBY_ENDPOINT_MAX];
  RNetLanLobby state;
  if (!(g_hosting_lan || g_joined_lan) || !ReadLanState(&state)) return NULL;
  snprintf(endpoint, sizeof(endpoint), "%s", state.endpoint);
  return endpoint;
}

void SmwNetplayLauncherDisconnect(void) {
  if (g_hosting_lan || g_joined_lan || snes_lobby_in_lobby())
    (void)Leave(NULL);
  snes_lobby_disconnect();
  g_remote_ready_requested = 0;
}

#endif /* SMW_COOP_BUILD */
