#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "smw_renderer.h"
#include "foreign_controller.h"
#include "mods/smw_falcon_plugin.h"
#include "mods/coop/coop_runtime.h"
#include "snes/saveload.h"
#include "overrides/falcon/falcon_smw_adapter.h"

#include <stdio.h>
#include <string.h>

/* Fixed game chunk appended by the runner's RtlGameInfo save extension. The
 * framework record itself remains pointer-free and versioned; this wrapper
 * only gives the SMW stream a stable bounded slot for it. */
#define SMW_FOREIGN_SAVE_MAGIC 0x31574653u /* "SFW1" little-endian */
#define SMW_FOREIGN_SAVE_VERSION 1u
#define SMW_FOREIGN_SAVE_BLOB_CAP (SNES_FOREIGN_SAVE_MAX_PAYLOAD + 256u)
#define SMW_COMBINED_SAVE_MAGIC 0x31584d53u /* "SMX1": renderer then controller */
#define SMW_COOP_SAVE_MAGIC 0x31434d53u /* "SMC1": renderer then native roster */

typedef struct {
  uint32 magic;
  uint32 version;
  uint32 blob_size;
  uint8 blob[SMW_FOREIGN_SAVE_BLOB_CAP];
} SmwForeignSaveChunk;

size_t SmwStateSharedExtraSize(void);
static int s_smw_foreign_chunk_loaded;
static uint32 s_loaded_frame_driver;
#define SMW_HOST_STATE_MAGIC 0x31545348u /* HST1 */
enum { SMW_RENDERER_STATE_BYTES = 8 + 4 + 128 + 1 };

static bool SmwStatePreflight(const void *data, size_t size) {
  const uint8 *bytes = data;
  uint32 version = 0, header[2], magic = 0, saved_version = 0;
  size_t guest = RtlSnapshotGuestSize(&version);
  size_t execution = RtlExecutionStateSize();
  size_t offset = guest + sizeof(uint32) + SMW_RENDERER_STATE_BYTES;
  if (!bytes || !guest || size < offset || size - offset < sizeof(header) + execution)
    return false;
  memcpy(&magic, bytes + guest, sizeof(magic));
  memcpy(&saved_version, bytes + sizeof(uint32), sizeof(saved_version));
  memcpy(header, bytes + offset, sizeof(header));
  if (saved_version != version ||
      magic != (SmwCoopActive() ? SMW_COOP_SAVE_MAGIC : SMW_COMBINED_SAVE_MAGIC) ||
      header[0] != SMW_HOST_STATE_MAGIC || header[1] > 3 ||
      !RtlExecutionStateValid(bytes + offset + sizeof(header), execution)) return false;
  if (!SmwCoopActive() && size != guest + SmwStateSharedExtraSize()) return false;
  return SmwCoopSnapshotPreflight(data, size);
}

size_t SmwStateSharedExtraSize(void) {
  /* SMX1 + renderer SMWS v1 (no padding) + fixed foreign-controller slot. */
  return sizeof(uint32) + SMW_RENDERER_STATE_BYTES + 2 * sizeof(uint32) + RtlExecutionStateSize() +
      (SmwCoopActive() ? 0 : sizeof(SmwForeignSaveChunk));
}

static void SmwForeignSaveExtra(SaveLoadInfo *sli) {
  SmwForeignSaveChunk chunk;
  memset(&chunk, 0, sizeof(chunk));
  chunk.magic = SMW_FOREIGN_SAVE_MAGIC;
  chunk.version = SMW_FOREIGN_SAVE_VERSION;
  if (!snes_foreign_save(chunk.blob, sizeof(chunk.blob), &chunk.blob_size)) {
    /* A controller that cannot produce a complete payload must never create
     * a save that later revives stale host state. */
    chunk.magic = 0;
    fprintf(stderr, "[smw] foreign-controller save payload unavailable\n");
  }
  sli->func(sli, &chunk, sizeof(chunk));
}

static void SmwForeignLoadExtra(SaveLoadInfo *sli, uint32 version) {
  SmwForeignSaveChunk chunk;
  (void)version;
  s_smw_foreign_chunk_loaded = 0;
  memset(&chunk, 0, sizeof(chunk));
  sli->func(sli, &chunk, sizeof(chunk));
  if (chunk.magic == SMW_FOREIGN_SAVE_MAGIC &&
      chunk.version == SMW_FOREIGN_SAVE_VERSION &&
      chunk.blob_size <= sizeof(chunk.blob) &&
      snes_foreign_load(chunk.blob, chunk.blob_size)) {
    s_smw_foreign_chunk_loaded = 1;
    return;
  }
  fprintf(stderr, "[smw] foreign-controller save payload rejected\n");
}

static void SmwStateSaveExtra(SaveLoadInfo *sli) {
  uint32 magic = SmwCoopActive() ? SMW_COOP_SAVE_MAGIC : SMW_COMBINED_SAVE_MAGIC;
  sli->func(sli, &magic, sizeof(magic));
  SmwRendererSaveExtra(sli);
  uint32 host[] = {SMW_HOST_STATE_MAGIC, SmwFrameDriverDiagnosticState()};
  sli->func(sli, host, sizeof(host));
  RtlSaveExecutionState(sli);
  if (SmwCoopActive()) SmwCoopSaveExtra(sli);
  else SmwForeignSaveExtra(sli);
}

static void SmwStateLoadExtra(SaveLoadInfo *sli, uint32 version) {
  uint32 magic = 0, host[2] = {0};
  s_smw_foreign_chunk_loaded = 0;
  sli->func(sli, &magic, sizeof(magic));
  SmwRendererLoadExtra(sli, version);
  sli->func(sli, host, sizeof(host));
  if (magic != (SmwCoopActive() ? SMW_COOP_SAVE_MAGIC : SMW_COMBINED_SAVE_MAGIC) ||
      host[0] != SMW_HOST_STATE_MAGIC || host[1] > 3 || !RtlLoadExecutionState(sli)) {
    RtlStateStreamFail(sli);
    return;
  }
  s_loaded_frame_driver = host[1];
  if (SmwCoopActive()) SmwCoopLoadExtra(sli);
  else SmwForeignLoadExtra(sli, version);
}

static void SmwOnStateLoaded(uint32 version) {
  (void)version;
  smw_falcon_restore_selection(s_smw_foreign_chunk_loaded);
  SmwFalconOnStateLoaded();
  SmwRendererStateLoaded(version);
  SmwCoopStateLoaded();
  g_cpu.ram = g_ram;
  RtlApplyExecutionState();
  SmwRestoreFrameDriverDiagnosticState(s_loaded_frame_driver);
  s_smw_foreign_chunk_loaded = 0;
}

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
  .state_save_extra = &SmwStateSaveExtra,
  .state_load_extra = &SmwStateLoadExtra,
  .on_state_loaded = &SmwOnStateLoaded,
  .snapshot_guard_identity = &SmwCoopSnapshotIdentity,
  .snapshot_preflight = &SmwStatePreflight,
  .snapshot_allowed = &SmwCoopPersistenceReady,
};
