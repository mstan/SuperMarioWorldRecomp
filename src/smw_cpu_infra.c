#include "common_cpu_infra.h"
#include "smw_rtl.h"
#include "smw_renderer.h"
#include "foreign_controller.h"
#include "mods/smw_falcon_plugin.h"
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

typedef struct {
  uint32 magic;
  uint32 version;
  uint32 blob_size;
  uint8 blob[SMW_FOREIGN_SAVE_BLOB_CAP];
} SmwForeignSaveChunk;

static int s_smw_foreign_chunk_loaded;

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

/* Older main and experimental renderer saves have different first chunks.
 * Replay the inspected magic without requiring a seekable file: network
 * snapshots use the same callback with a memory stream. */
typedef struct {
  SaveLoadInfo base;
  SaveLoadInfo *source;
  uint32 magic;
  size_t position;
} SmwPrefixReader;

static void SmwReadPrefix(SaveLoadInfo *sli, void *data, size_t size) {
  SmwPrefixReader *reader = (SmwPrefixReader *)sli;
  size_t prefix = sizeof(reader->magic) - reader->position;
  if (prefix > size) prefix = size;
  memcpy(data, (uint8 *)&reader->magic + reader->position, prefix);
  reader->position += prefix;
  if (size > prefix)
    reader->source->func(reader->source, (uint8 *)data + prefix, size - prefix);
}

static void SmwStateSaveExtra(SaveLoadInfo *sli) {
  uint32 magic = SMW_COMBINED_SAVE_MAGIC;
  sli->func(sli, &magic, sizeof(magic));
  SmwRendererSaveExtra(sli);
  SmwForeignSaveExtra(sli);
}

static void SmwStateLoadExtra(SaveLoadInfo *sli, uint32 version) {
  SmwPrefixReader reader = { { SmwReadPrefix }, sli, 0, 0 };
  s_smw_foreign_chunk_loaded = 0;
  sli->func(sli, &reader.magic, sizeof(reader.magic));
  if (reader.magic == SMW_COMBINED_SAVE_MAGIC) {
    SmwRendererLoadExtra(sli, version);
    SmwForeignLoadExtra(sli, version);
  } else if (reader.magic == 0x53574d53u) { /* legacy SMWS */
    SmwRendererLoadExtra(&reader.base, version);
  } else if (reader.magic == SMW_FOREIGN_SAVE_MAGIC) {
    SmwForeignLoadExtra(&reader.base, version);
  }
}

static void SmwOnStateLoaded(uint32 version) {
  (void)version;
  smw_falcon_restore_selection(s_smw_foreign_chunk_loaded);
  SmwFalconOnStateLoaded();
  SmwRendererStateLoaded(version);
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
};
