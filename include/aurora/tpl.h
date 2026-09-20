#ifndef AURORA_TPL_H
#define AURORA_TPL_H

#include <stddef.h>
#include <revolution/tpl.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL aurora_tpl_bind(void* data, size_t size);

const TPLPalette* aurora_tpl_get_palette(const void* data);

void aurora_tpl_unbind(const void* data);

#ifdef __cplusplus
}
#endif

#endif
