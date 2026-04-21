#include "common_cpu_infra.h"
#include "smw_rtl.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .patch_bugs = NULL,
  .initialize = NULL,
  .run_frame = &SmwRunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
};
