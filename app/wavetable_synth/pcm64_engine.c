/****************************************************************************
 * apps/wavetable_synth/pcm64_engine.c
 ****************************************************************************/

#include "pcm64_engine.h"

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int16_t sat_i16(int32_t x)
{
  if (x > 32767)
    {
      return 32767;
    }
  if (x < -32768)
    {
      return -32768;
    }
  return (int16_t)x;
}

/* From P4 samp_adpcm: linear Q32 frac */
static inline int32_t interp_linear_q32(int32_t a, int32_t b, uint32_t frac)
{
  int32_t t = (int32_t)(frac >> 17); /* ~Q15 */
  return a + (((b - a) * t) >> 15);
}

/* From P4 samp_adpcm: Hermite4 */
static inline int32_t interp_hermite4_q32(int32_t ym1, int32_t y0, int32_t y1,
                                         int32_t y2, uint32_t frac)
{
  int32_t t = (int32_t)(frac >> 24); /* Q8 0..255 */
  int32_t c1 = (y1 - ym1) >> 1;
  int32_t c2 = ym1 - ((5 * y0) >> 1) + (y1 << 1) - (y2 >> 1);
  int32_t c3 = ((y2 - ym1) >> 1) + ((3 * (y0 - y1)) >> 1);
  int32_t s = (c3 * t) >> 8;
  s = ((s + c2) * t) >> 8;
  s = ((s + c1) * t) >> 8;
  return s + y0;
}

static void rebuild_active(struct pcm64_engine_s *eng)
{
  int n = 0;
  int i;
  for (i = 0; i < PCM64_MAX_VOICES; i++)
    {
      if (eng->voices[i].active)
        {
          eng->active_list[n++] = (uint8_t)i;
        }
    }
  eng->active_count = n;
}

static uint64_t midi_to_rate_inc(uint32_t sample_rate, int midi_note,
                                 uint32_t frag_len)
{
  /* fragment is one cycle => freq = midi, rate_inc = freq * frag_len / sr */
  double freq = 440.0 * pow(2.0, ((double)midi_note - 69.0) / 12.0);
  double inc = freq * (double)frag_len / (double)sample_rate;
  return (uint64_t)(inc * 4294967296.0);
}

static uint64_t hz_to_rate_inc(uint32_t sample_rate, uint32_t freq_hz,
                               uint32_t frag_len)
{
  double inc = (double)freq_hz * (double)frag_len / (double)sample_rate;
  return (uint64_t)(inc * 4294967296.0);
}

void pcm64_engine_init(struct pcm64_engine_s *eng, uint32_t sample_rate)
{
  memset(eng, 0, sizeof(*eng));
  eng->sample_rate = sample_rate ? sample_rate : 16000;
  eng->master_q15 = PCM64_DEFAULT_MASTER_Q15;
}

void pcm64_engine_build_frags(struct pcm64_engine_s *eng)
{
  int f;
  int i;
  for (f = 0; f < PCM64_NUM_FRAGS; f++)
    {
      /* Distinct harmonic mix per fragment (still one-cycle loops). */
      int harm = 1 + (f % 8);
      double phase0 = (double)f * 0.17;
      for (i = 0; i < PCM64_FRAG_SAMPLES; i++)
        {
          double ph = 2.0 * M_PI * (double)i / (double)PCM64_FRAG_SAMPLES;
          double s = 0.0;
          int h;
          for (h = 1; h <= harm; h++)
            {
              s += sin(ph * (double)h + phase0) / (double)h;
            }
          /* normalize roughly */
          s *= 0.35 * 32767.0;
          if (s > 32767.0)
            {
              s = 32767.0;
            }
          if (s < -32768.0)
            {
              s = -32768.0;
            }
          eng->frags[f][i] = (int16_t)s;
        }
    }
}

void pcm64_engine_note_off_all(struct pcm64_engine_s *eng)
{
  int i;
  for (i = 0; i < PCM64_MAX_VOICES; i++)
    {
      eng->voices[i].active = false;
    }
  eng->active_count = 0;
}

int pcm64_engine_voice_on(struct pcm64_engine_s *eng, int frag,
                          int midi_note, uint32_t freq_hz, int velocity)
{
  int slot = -1;
  int i;
  struct pcm64_voice_s *v;
  uint64_t rate_inc;

  if (frag < 0 || frag >= PCM64_NUM_FRAGS)
    {
      frag = 0;
    }
  if (velocity < 1)
    {
      velocity = 1;
    }
  if (velocity > 127)
    {
      velocity = 127;
    }

  for (i = 0; i < PCM64_MAX_VOICES; i++)
    {
      if (!eng->voices[i].active)
        {
          slot = i;
          break;
        }
    }
  if (slot < 0)
    {
      /* steal oldest */
      uint32_t best_age = 0;
      slot = 0;
      for (i = 0; i < PCM64_MAX_VOICES; i++)
        {
          if (eng->voices[i].age >= best_age)
            {
              best_age = eng->voices[i].age;
              slot = i;
            }
        }
    }

  v = &eng->voices[slot];
  if (midi_note >= 0)
    {
      rate_inc = midi_to_rate_inc(eng->sample_rate, midi_note, PCM64_FRAG_SAMPLES);
    }
  else
    {
      rate_inc = hz_to_rate_inc(eng->sample_rate, freq_hz ? freq_hz : 440,
                                PCM64_FRAG_SAMPLES);
    }

  v->active = true;
  v->frag = (uint8_t)frag;
  v->pos = 0;
  v->rate_inc = rate_inc;
  v->gain_q15 = (velocity * 32767) / 127;
  v->attack_ramp_q15 = 0;
  v->attack_ramp_inc = 32767 / PCM64_ATTACK_SAMPLES;
  if (v->attack_ramp_inc < 1)
    {
      v->attack_ramp_inc = 1;
    }
  v->age = 0;
  rebuild_active(eng);
  return slot;
}

int pcm64_engine_all_on(struct pcm64_engine_s *eng, int base_midi, int velocity)
{
  int i;
  pcm64_engine_note_off_all(eng);
  for (i = 0; i < PCM64_MAX_VOICES; i++)
    {
      int midi = base_midi + (i % 24) - 12; /* spread around base */
      if (midi < 24)
        {
          midi = 24;
        }
      if (midi > 96)
        {
          midi = 96;
        }
      pcm64_engine_voice_on(eng, i, midi, 0, velocity);
    }
  return eng->active_count;
}

void pcm64_engine_render(struct pcm64_engine_s *eng, int16_t *out,
                         unsigned frames)
{
  unsigned n;
  const int nvoices = eng->active_count;
  int32_t master = eng->master_q15;
  int adapt_dense = (nvoices >= PCM64_ADAPT_VOICES);
  const uint32_t mask = PCM64_FRAG_SAMPLES - 1u;

  if (nvoices > 4)
    {
      /* same soft trim as samp_adpcm */
      master = (master * 32) / (28 + nvoices);
    }

  for (n = 0; n < frames; n++)
    {
      int32_t mix = 0;
      int j;

      for (j = 0; j < nvoices; j++)
        {
          struct pcm64_voice_s *v = &eng->voices[eng->active_list[j]];
          const int16_t *tab;
          uint32_t i0;
          uint32_t frac;
          int32_t s;
          int32_t g;
          int32_t env;

          if (!v->active)
            {
              continue;
            }

          tab = eng->frags[v->frag];
          i0 = (uint32_t)(v->pos >> 32);
          frac = (uint32_t)(v->pos & 0xffffffffu);

          if (!adapt_dense)
            {
              int32_t ym1 = (int32_t)tab[(i0 - 1u) & mask];
              int32_t y0 = (int32_t)tab[i0 & mask];
              int32_t y1 = (int32_t)tab[(i0 + 1u) & mask];
              int32_t y2 = (int32_t)tab[(i0 + 2u) & mask];
              s = interp_hermite4_q32(ym1, y0, y1, y2, frac);
              eng->cubic_samples++;
            }
          else
            {
              int32_t a = (int32_t)tab[i0 & mask];
              int32_t b = (int32_t)tab[(i0 + 1u) & mask];
              s = interp_linear_q32(a, b, frac);
              eng->linear_samples++;
            }

          env = 32767;
          if (v->attack_ramp_inc > 0 && v->attack_ramp_q15 < 32767)
            {
              v->attack_ramp_q15 += v->attack_ramp_inc;
              if (v->attack_ramp_q15 >= 32767)
                {
                  v->attack_ramp_q15 = 32767;
                  v->attack_ramp_inc = 0;
                }
            }
          if (v->attack_ramp_q15 < 32767)
            {
              env = v->attack_ramp_q15;
            }

          g = (v->gain_q15 * env) >> 15;
          mix += (s * g) >> 15;
          v->pos += v->rate_inc;
          /* wrap within fragment (one-cycle loop) */
          if ((v->pos >> 32) >= PCM64_FRAG_SAMPLES)
            {
              v->pos -= ((uint64_t)PCM64_FRAG_SAMPLES << 32);
            }
          if (v->age < UINT32_MAX)
            {
              v->age++;
            }
        }

      mix = (mix * master) >> 15;
      {
        int32_t ap = mix < 0 ? -mix : mix;
        if ((uint32_t)ap > eng->mix_peak)
          {
            eng->mix_peak = (uint32_t)ap;
          }
      }
      out[n] = sat_i16(mix);
    }
}

static uint64_t mono_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

int pcm64_engine_bench(struct pcm64_engine_s *eng, int voices, float seconds,
                       double *out_cpu_pct, uint64_t *render_us,
                       uint64_t *audio_us)
{
  enum { BLOCK = 256 };
  int16_t buf[BLOCK];
  uint64_t total_frames;
  uint64_t done = 0;
  uint64_t t0;
  uint64_t t1;
  uint64_t rus;
  uint64_t aus;
  double pct;

  if (voices < 1)
    {
      voices = 1;
    }
  if (voices > PCM64_MAX_VOICES)
    {
      voices = PCM64_MAX_VOICES;
    }
  if (seconds < 0.1f)
    {
      seconds = 0.1f;
    }

  pcm64_engine_build_frags(eng);
  pcm64_engine_all_on(eng, 60, 100);
  /* mute extras if voices < 64 */
  if (voices < PCM64_MAX_VOICES)
    {
      int i;
      for (i = voices; i < PCM64_MAX_VOICES; i++)
        {
          eng->voices[i].active = false;
        }
      rebuild_active(eng);
    }

  eng->cubic_samples = 0;
  eng->linear_samples = 0;
  eng->mix_peak = 0;

  total_frames = (uint64_t)(seconds * (float)eng->sample_rate);
  t0 = mono_us();
  while (done < total_frames)
    {
      unsigned n = BLOCK;
      if (done + n > total_frames)
        {
          n = (unsigned)(total_frames - done);
        }
      pcm64_engine_render(eng, buf, n);
      done += n;
    }
  t1 = mono_us();

  rus = t1 - t0;
  aus = (total_frames * 1000000ull) / (uint64_t)eng->sample_rate;
  pct = aus ? (100.0 * (double)rus / (double)aus) : 0.0;

  if (out_cpu_pct)
    {
      *out_cpu_pct = pct;
    }
  if (render_us)
    {
      *render_us = rus;
    }
  if (audio_us)
    {
      *audio_us = aus;
    }

  syslog(LOG_NOTICE, "pcm64 bench: voices=%d sr=%lu sec=%.2f\n",
         eng->active_count, (unsigned long)eng->sample_rate, (double)seconds);
  syslog(LOG_NOTICE, "  render_us=%llu audio_us=%llu  CPU=%.1f%%\n",
         (unsigned long long)rus, (unsigned long long)aus, pct);
  syslog(LOG_NOTICE, "  cubic=%llu linear=%llu mix_peak=%lu adapt_dense=%d\n",
         (unsigned long long)eng->cubic_samples,
         (unsigned long long)eng->linear_samples,
         (unsigned long)eng->mix_peak,
         eng->active_count >= PCM64_ADAPT_VOICES);
  printf("pcm64 bench: voices=%d sr=%lu sec=%.2f\n",
         eng->active_count, (unsigned long)eng->sample_rate, (double)seconds);
  printf("  render_us=%llu audio_us=%llu  CPU=%.1f%%\n",
         (unsigned long long)rus, (unsigned long long)aus, pct);
  printf("  cubic=%llu linear=%llu mix_peak=%lu adapt_dense=%d\n",
         (unsigned long long)eng->cubic_samples,
         (unsigned long long)eng->linear_samples,
         (unsigned long)eng->mix_peak,
         eng->active_count >= PCM64_ADAPT_VOICES);

  return (pct > 100.0) ? 1 : 0;
}
