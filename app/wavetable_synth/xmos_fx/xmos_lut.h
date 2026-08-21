#ifndef XMOS_LUT_H
#define XMOS_LUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XMOS_SIN_LUT_BITS   10
#define XMOS_SIN_LUT_SIZE   (1 << XMOS_SIN_LUT_BITS)
#define XMOS_SIN_LUT_MASK   (XMOS_SIN_LUT_SIZE - 1)

#define XMOS_TS808_CURVE_LUT_SIZE  256
#define XMOS_TS808_CURVE_XMAX      4.0f

extern const float xmos_sin_lut[XMOS_SIN_LUT_SIZE];
extern const float xmos_reverb_damp_lut[256];
extern const float xmos_ts808_curve_lut[XMOS_TS808_CURVE_LUT_SIZE];

/* 0 = libm expf (reference), non-zero = LUT. MIDI CC104 can toggle for A/B. */
extern volatile uint8_t g_xmos_ts808_curve_lut;

/* phase01 in [0,1): linearly interpolated sin. */
static inline float xmos_sin_norm(float phase01)
{
  float x;
  int i;
  int j;
  float f;
  float a;
  float b;

  if (phase01 >= 1.0f || phase01 < 0.0f)
    {
      phase01 -= (float)(int)phase01;
      if (phase01 < 0.0f)
        {
          phase01 += 1.0f;
        }
    }

  x = phase01 * (float)XMOS_SIN_LUT_SIZE;
  i = (int)x;
  f = x - (float)i;
  i &= XMOS_SIN_LUT_MASK;
  j = (i + 1) & XMOS_SIN_LUT_MASK;
  a = xmos_sin_lut[i];
  b = xmos_sin_lut[j];
  return a + f * (b - a);
}

/* Unipolar LFO: 0.5*(1+sin), phase in [0,1). */
static inline float xmos_lfo_sine01(float phase01)
{
  return 0.5f * (1.0f + xmos_sin_norm(phase01));
}

/* Radians → sin via normalized LUT (Reverb LFO). */
static inline float xmos_sin_rad(float rad)
{
  const float inv_two_pi = 0.15915494309189535f; /* 1/(2*pi) */
  float phase01 = rad * inv_two_pi;
  phase01 -= (float)(int)phase01;
  if (phase01 < 0.0f)
    {
      phase01 += 1.0f;
    }
  return xmos_sin_norm(phase01);
}

/* TS808 soft-clip unit curve 1-exp(-3*|x|) * kn * sign(x). */
static inline float xmos_ts808_curve_lut_eval(float kn, float x)
{
  float ax;
  float idx;
  int i;
  int j;
  float f;
  float u;

  if (x == 0.0f)
    {
      return 0.0f;
    }

  ax = x < 0.0f ? -x : x;
  if (ax >= XMOS_TS808_CURVE_XMAX)
    {
      u = xmos_ts808_curve_lut[XMOS_TS808_CURVE_LUT_SIZE - 1];
    }
  else
    {
      idx = ax * ((float)(XMOS_TS808_CURVE_LUT_SIZE - 1) / XMOS_TS808_CURVE_XMAX);
      i = (int)idx;
      f = idx - (float)i;
      if (i >= XMOS_TS808_CURVE_LUT_SIZE - 1)
        {
          u = xmos_ts808_curve_lut[XMOS_TS808_CURVE_LUT_SIZE - 1];
        }
      else
        {
          j = i + 1;
          u = xmos_ts808_curve_lut[i]
            + f * (xmos_ts808_curve_lut[j] - xmos_ts808_curve_lut[i]);
        }
    }

  return (x < 0.0f) ? (-kn * u) : (kn * u);
}

#ifdef __cplusplus
}
#endif

#endif /* XMOS_LUT_H */
