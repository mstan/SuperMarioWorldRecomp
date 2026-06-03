#include "common_cpu_infra.h"
#include "smw_rtl.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &SmwDrawPpuFrame,
  .save_name_prefix = "save",
};
