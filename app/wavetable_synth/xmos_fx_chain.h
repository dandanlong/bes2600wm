#ifndef __APPS_WAVETABLE_SYNTH_XMOS_FX_CHAIN_H
#define __APPS_WAVETABLE_SYNTH_XMOS_FX_CHAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum xmos_fx_id_e
{
  XMOS_FX_VOX_AC30 = 0,
  XMOS_FX_JCM800,
  XMOS_FX_FENDER_TWIN,
  XMOS_FX_MESA_BOOGIE,
  XMOS_FX_DIEZEL_CH4,
  XMOS_FX_TS808,
  XMOS_FX_MXR_DIST_PLUS,
  XMOS_FX_PROCO_RAT,
  XMOS_FX_VOX_212,
  XMOS_FX_MESA_412,
  XMOS_FX_MARSHALL_212,
  XMOS_FX_MARSHALL_412,
  XMOS_FX_FENDER_212,
  XMOS_FX_DIEZEL_412,
  XMOS_FX_COMPRESSOR,
  XMOS_FX_MULTIBAND_COMP,
  XMOS_FX_CHORUS,
  XMOS_FX_DELAY,
  XMOS_FX_EQ,
  XMOS_FX_REVERB,
  XMOS_FX_TREMOLO,
  XMOS_FX_AUTOPAN,
  XMOS_FX_COUNT
};

void xmos_fx_init(void);
void xmos_fx_enable(enum xmos_fx_id_e id, bool enable);
void xmos_fx_enable_all(bool enable);
bool xmos_fx_enabled(enum xmos_fx_id_e id);
bool xmos_fx_any_enabled(void);
uint32_t xmos_fx_enable_mask(void);
void xmos_fx_set_param(enum xmos_fx_id_e id, uint8_t param,
                       uint8_t value);
void xmos_fx_process_mono_i16(int16_t *samples, size_t frames);

#endif
