#include "gx.hpp"
#include "__gx.h"

#include <dolphin/gx/GXCpu2Efb.h>

void GXPokeAlphaRead(GXAlphaReadMode mode) { __gx->alphaRead = mode; }
