/****************************************************************************
 * BES adapter for the 22 XMOS guitar effects. All slots default to bypass.
 ****************************************************************************/

#include "xmos_fx_chain.h"
#include "xmos_fx/xmos_fx_compat.h"
#include "xmos_fx/global.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <syslog.h>

/* Original XMOS algorithm entry points. */
void VoxAC30_init(void);
void VoxAC30_process(float *, float *);
void JCM800_init(void);
void JCM800_process(float *, float *);
void FenderTwinReverb_init(void);
void FenderTwinReverb_process(float *, float *);
void MesaBoogie_init(void);
void MesaBoogie_process(float *, float *);
void DiezelCH4_init(void);
void DiezelCH4_process(float *, float *);
void Ts808_init(void);
void Ts808_process(float *, float *);
void MxrDisPlus_init(void);
void MxrDisPlus_process(float *, float *);
void Vintage_ProcoRat_init(void);
void Vintage_ProcoRat_process(float *, float *);
void Vox212_init(void);
void Vox212_process(float *, float *);
void Mesa412_init(void);
void Mesa412_process(float *, float *);
void Marshall212_init(void);
void Marshall212_process(float *, float *);
void Marshall412_init(void);
void Marshall412_process(float *, float *);
void FenderTwinReverb212_init(void);
void FenderTwinReverb212_process(float *, float *);
void Diezel412_init(void);
void Diezel412_process(float *, float *);
void Compressor_init(void);
void t1_Compressor_fProcess(float *, float *);
void multiBandCompT0_init(void);
void t0_CompL_fProcess(float *, float *);
void chorus_init(void);
void t1_ChorusSine_fProcess(float *, float *);
void pureDelay_init(void);
void t1_PureDelay_fProcess(float *, float *);
void EQT1_init(void);
void t1_EQ_fProcess(float *, float *);
void reverb_T1_init(void);
void t1_reverb_fProcess(float *, float *);
void Tremolo_init(void);
void t1_Tremolo_fprocess(float *, float *);
void AutoPan_init(void);
void t1_AutoPan_fprocess(float *, float *);

sAlgosParams_t sT0_algo_paras[3];
static uint8_t g_params[XMOS_FX_COUNT][ParaLength];
static atomic_uint_fast32_t g_enable_mask;
/* getParam(algoId) used to linear-scan 22 slots every knob read. */
static int8_t g_algo_to_slot[t1_algo_max];

#ifndef XMOS_FX_FRAME_LENGTH
#define XMOS_FX_FRAME_LENGTH FRAME_LENGTH_ALGO /* 8, matches original XMOS */
#endif

static const uint8_t g_xmos_algo_index[XMOS_FX_COUNT] =
{
  [XMOS_FX_VOX_AC30]       = t1_VoxAC30 - 1,
  [XMOS_FX_JCM800]         = t1_JCM800 - 1,
  [XMOS_FX_FENDER_TWIN]    = t1_FenderTwinReverb - 1,
  [XMOS_FX_MESA_BOOGIE]    = t1_Mesa - 1,
  [XMOS_FX_DIEZEL_CH4]     = t1_DiezelCH4 - 1,
  [XMOS_FX_TS808]          = t1_Ts808 - 1,
  [XMOS_FX_MXR_DIST_PLUS]  = t1_MxrDisPlus - 1,
  [XMOS_FX_PROCO_RAT]      = t1_Vintage_ProcoRat - 1,
  [XMOS_FX_VOX_212]        = t1_Vox212 - 1,
  [XMOS_FX_MESA_412]       = t1_Mesa412 - 1,
  [XMOS_FX_MARSHALL_212]   = t1_Marshall212 - 1,
  [XMOS_FX_MARSHALL_412]   = t1_Marshall412 - 1,
  [XMOS_FX_FENDER_212]     = t1_FenderTwinReverb212 - 1,
  [XMOS_FX_DIEZEL_412]     = t1_Diezel412 - 1,
  [XMOS_FX_COMPRESSOR]     = t1_Compressor - 1,
  [XMOS_FX_MULTIBAND_COMP] = t1_MultibandCompressor - 1,
  [XMOS_FX_CHORUS]         = t1_ChorusSine - 1,
  [XMOS_FX_DELAY]          = t1_PureDelay - 1,
  [XMOS_FX_EQ]             = t1_EQ - 1,
  [XMOS_FX_REVERB]         = t1_Reverb - 1,
  [XMOS_FX_TREMOLO]        = t1_Tremolo - 1,
  [XMOS_FX_AUTOPAN]        = t1_AutoPan - 1,
};

uint8_t getParam(uint8_t algoId, uint8_t paramNo)
{
  int slot;
  if (paramNo >= ParaLength || algoId >= t1_algo_max)
    {
      return 0;
    }
  slot = g_algo_to_slot[algoId];
  if (slot < 0)
    {
      return 0;
    }
  return g_params[slot][paramNo];
}

void xmos_fx_init(void)
{
  unsigned id;
  memset(g_params, 128, sizeof(g_params));
  memset(sT0_algo_paras, 0, sizeof(sT0_algo_paras));
  memset(g_algo_to_slot, -1, sizeof(g_algo_to_slot));
  for (id = 0; id < XMOS_FX_COUNT; id++)
    {
      uint8_t algo = g_xmos_algo_index[id];
      if (algo < t1_algo_max)
        {
          g_algo_to_slot[algo] = (int8_t)id;
        }
    }
  memcpy(sT0_algo_paras[0].algoParas,
         g_params[XMOS_FX_MULTIBAND_COMP], ParaLength);
  atomic_init(&g_enable_mask, 0); /* Product default: all 22 bypassed. */

  VoxAC30_init();
  JCM800_init();
  FenderTwinReverb_init();
  MesaBoogie_init();
  DiezelCH4_init();
  Ts808_init();
  MxrDisPlus_init();
  Vintage_ProcoRat_init();
  Vox212_init();
  Mesa412_init();
  Marshall212_init();
  Marshall412_init();
  FenderTwinReverb212_init();
  Diezel412_init();
  Compressor_init();
  multiBandCompT0_init();
  chorus_init();
  pureDelay_init();
  EQT1_init();
  reverb_T1_init();
  Tremolo_init();
  AutoPan_init();

  syslog(LOG_NOTICE,
         "xmos_fx: 22 algorithms ready, all bypassed (frame=%d)\n",
         XMOS_FX_FRAME_LENGTH);
}

void xmos_fx_enable(enum xmos_fx_id_e id, bool enable)
{
  uint_fast32_t bit;
  if ((unsigned)id >= XMOS_FX_COUNT)
    {
      return;
    }
  bit = (uint_fast32_t)1u << (unsigned)id;
  if (enable)
    {
      atomic_fetch_or_explicit(&g_enable_mask, bit, memory_order_relaxed);
    }
  else
    {
      atomic_fetch_and_explicit(&g_enable_mask, ~bit, memory_order_relaxed);
    }
}

void xmos_fx_enable_all(bool enable)
{
  uint_fast32_t mask = enable ? (((uint_fast32_t)1u << XMOS_FX_COUNT) - 1u) : 0u;
  atomic_store_explicit(&g_enable_mask, mask, memory_order_relaxed);
  syslog(LOG_NOTICE, "xmos_fx: enable_all=%s mask=0x%lx\n",
         enable ? "on" : "off", (unsigned long)mask);
}

bool xmos_fx_enabled(enum xmos_fx_id_e id)
{
  if ((unsigned)id >= XMOS_FX_COUNT)
    {
      return false;
    }
  return (atomic_load_explicit(&g_enable_mask, memory_order_relaxed) &
          ((uint_fast32_t)1u << (unsigned)id)) != 0;
}

bool xmos_fx_any_enabled(void)
{
  return atomic_load_explicit(&g_enable_mask, memory_order_relaxed) != 0;
}

uint32_t xmos_fx_enable_mask(void)
{
  return (uint32_t)atomic_load_explicit(&g_enable_mask, memory_order_relaxed);
}

void xmos_fx_set_param(enum xmos_fx_id_e id, uint8_t param, uint8_t value)
{
  if ((unsigned)id >= XMOS_FX_COUNT || param >= ParaLength)
    {
      return;
    }
  g_params[id][param] = value;
  if (id == XMOS_FX_MULTIBAND_COMP)
    {
      sT0_algo_paras[0].algoParas[param] = value;
    }
}

static void process_one(enum xmos_fx_id_e id, float *in, float *out)
{
  switch (id)
    {
      case XMOS_FX_VOX_AC30:       VoxAC30_process(in, out); break;
      case XMOS_FX_JCM800:         JCM800_process(in, out); break;
      case XMOS_FX_FENDER_TWIN:    FenderTwinReverb_process(in, out); break;
      case XMOS_FX_MESA_BOOGIE:    MesaBoogie_process(in, out); break;
      case XMOS_FX_DIEZEL_CH4:     DiezelCH4_process(in, out); break;
      case XMOS_FX_TS808:          Ts808_process(in, out); break;
      case XMOS_FX_MXR_DIST_PLUS:  MxrDisPlus_process(in, out); break;
      case XMOS_FX_PROCO_RAT:      Vintage_ProcoRat_process(in, out); break;
      case XMOS_FX_VOX_212:        Vox212_process(in, out); break;
      case XMOS_FX_MESA_412:       Mesa412_process(in, out); break;
      case XMOS_FX_MARSHALL_212:   Marshall212_process(in, out); break;
      case XMOS_FX_MARSHALL_412:   Marshall412_process(in, out); break;
      case XMOS_FX_FENDER_212:     FenderTwinReverb212_process(in, out); break;
      case XMOS_FX_DIEZEL_412:     Diezel412_process(in, out); break;
      case XMOS_FX_COMPRESSOR:     t1_Compressor_fProcess(in, out); break;
      case XMOS_FX_MULTIBAND_COMP: t0_CompL_fProcess(in, out); break;
      case XMOS_FX_CHORUS:         t1_ChorusSine_fProcess(in, out); break;
      case XMOS_FX_DELAY:          t1_PureDelay_fProcess(in, out); break;
      case XMOS_FX_EQ:             t1_EQ_fProcess(in, out); break;
      case XMOS_FX_REVERB:         t1_reverb_fProcess(in, out); break;
      case XMOS_FX_TREMOLO:        t1_Tremolo_fprocess(in, out); break;
      case XMOS_FX_AUTOPAN:        t1_AutoPan_fprocess(in, out); break;
      default: out[0] = in[0]; out[1] = in[1]; break;
    }
}

void xmos_fx_process_mono_i16(int16_t *samples, size_t frames)
{
  uint_fast32_t mask =
    atomic_load_explicit(&g_enable_mask, memory_order_relaxed);
  size_t base;

  if (!mask || samples == NULL || frames == 0)
    {
      return;
    }

  /* Match original XMOS tile scheduling: process FRAME_LENGTH samples as a
   * block. Algos are still 1-stereo-sample process(), but we keep conversion
   * and the enable mask outside the inner sample loop.
   */
  for (base = 0; base < frames; base += (size_t)XMOS_FX_FRAME_LENGTH)
    {
      size_t n;
      size_t count = frames - base;
      if (count > (size_t)XMOS_FX_FRAME_LENGTH)
        {
          count = (size_t)XMOS_FX_FRAME_LENGTH;
        }

      for (n = 0; n < count; n++)
        {
          float a[2];
          float b[2];
          float mono;
          float s = (float)samples[base + n] * (1.0f / 32768.0f);
          unsigned id;
          int32_t q;

          a[0] = s;
          a[1] = s;
          for (id = 0; id < XMOS_FX_COUNT; id++)
            {
              if (mask & ((uint_fast32_t)1u << id))
                {
                  process_one((enum xmos_fx_id_e)id, a, b);
                  a[0] = b[0];
                  a[1] = b[1];
                }
            }
          mono = 0.5f * (a[0] + a[1]);
          if (mono > 1.0f)
            {
              mono = 1.0f;
            }
          else if (mono < -1.0f)
            {
              mono = -1.0f;
            }
          q = (int32_t)(mono * 32767.0f);
          samples[base + n] = (int16_t)q;
        }
    }
}
