/****************************************************************************
 * Dedicated 1 MiB FX working RAM in PSRAM (linker .fx_psram_pool).
 * Not part of the NuttX/synth heap — delay/reverb/IR live here.
 ****************************************************************************/

#ifndef __APPS_WAVETABLE_SYNTH_FX_PSRAM_POOL_H
#define __APPS_WAVETABLE_SYNTH_FX_PSRAM_POOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FX_PSRAM_POOL_BYTES  (1u * 1024u * 1024u)

void fx_pool_init(void);
void fx_pool_reset(void);
void *fx_pool_alloc(size_t bytes);
void *fx_pool_calloc(size_t count, size_t size);
size_t fx_pool_used(void);
size_t fx_pool_free(void);
uintptr_t fx_pool_base(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_WAVETABLE_SYNTH_FX_PSRAM_POOL_H */
