#include "gx.hpp"

extern "C" {

// TODO GXSetGPMetric
// TODO GXClearGPMetric
// TODO GXReadGPMetric
// TODO GXReadGP0Metric
// TODO GXReadGP1Metric
// TODO GXReadMemMetric
// TODO GXClearMemMetric
void GXReadPixMetric(u32* top_pixels_in, u32* top_pixels_out, u32* bot_pixels_in, u32* bot_pixels_out,
                     u32* clr_pixels_in, u32* copy_clks) {
  *top_pixels_in = 0;
  *top_pixels_out = 0;
  *bot_pixels_in = 0;
  *bot_pixels_out = 0;
  *clr_pixels_in = 0;
  *copy_clks = 0;
}

void GXClearPixMetric() {}
// TODO GXSetVCacheMetric
// TODO GXReadVCacheMetric
// TODO GXClearVCacheMetric
// TODO GXReadXfRasMetric
// TODO GXInitXfRasMetric
// TODO GXReadClksPerVtx
}