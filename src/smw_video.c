#include "smw_renderer.h"
#include <math.h>

SmwVideoSettings g_smw_video = {false, true, 0};
SmwViewport g_smw_viewport = {256, 0, 4.0 / 3.0};

int SmwViewOffset(SmwViewport view, int camera, int level_width) {
  int origin = camera - view.extra;
  int last = level_width > view.width ? level_width - view.width : 0;
  if (origin > last) origin = last;
  if (origin < 0) origin = 0;
  int offset = camera - origin;
  if (offset < 0) offset = 0;
  if (offset > view.width - 256) offset = view.width - 256;
  return offset;
}

SmwViewport SmwCalculateViewport(const SmwVideoSettings *s, int w, int h,
                                 SnesDisplayAspect display_aspect) {
  double target = s->aspect;
  if (!isfinite(target) || target <= 0)
    target = w > 0 && h > 0 ? (double)w / h : 0;
  SnesDisplayFrame frame = SnesDisplayAspect_ComputeAdaptiveFrame(
      256, 224, s->enabled ? SMW_RENDER_MAX_WIDTH : 256, target, display_aspect);
  return (SmwViewport){frame.width, frame.extra, frame.aspect};
}
void SmwDestination(SmwViewport view, int w, int h, int *x, int *y, int *dw, int *dh) {
  SnesDisplayViewport dst = SnesDisplayAspect_FitViewport(
      view.aspect, w > 0 ? w : 1, h > 0 ? h : 1);
  *x = dst.x; *y = dst.y; *dw = dst.width; *dh = dst.height;
}
