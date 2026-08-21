/****************************************************************************
 * apps/wavetable_synth/pcm64_engine.h
 *
 * 64-voice PCM-in-RAM sampler for BES A7 CPU load probing.
 * Algorithm aligned with P4_WaveTableADPCM samp_adpcm render path:
 *   - Q32.32 phase
 *   - Hermite4 when sparse, linear when dense (>=24 active)
 *   - soft master trim vs polyphony
 *   - short attack ramp + end fade
 ****************************************************************************/

#ifndef __APPS_WAVETABLE_SYNTH_PCM64_ENGINE_H
#define __APPS_WAVETABLE_SYNTH_PCM64_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PCM64_MAX_VOICES          64
#define PCM64_FRAG_SAMPLES        512   /* per-fragment loop length (power of 2) */
#define PCM64_NUM_FRAGS           64
#define PCM64_ADAPT_VOICES        24    /* same threshold as SAMP_ADPCM_ADAPT_VOICES */
#define PCM64_ATTACK_SAMPLES      256
#define PCM64_END_FADE            64
#define PCM64_DEFAULT_MASTER_Q15  20000

struct pcm64_voice_s
{
  bool active;
  uint8_t frag;              /* 0..PCM64_NUM_FRAGS-1 */
  uint64_t pos;              /* Q32.32 sample index into fragment */
  uint64_t rate_inc;         /* Q32.32 */
  int32_t gain_q15;
  int32_t attack_ramp_q15;
  int32_t attack_ramp_inc;
  uint32_t age;
};

struct pcm64_engine_s
{
  uint32_t sample_rate;
  int32_t master_q15;
  int16_t frags[PCM64_NUM_FRAGS][PCM64_FRAG_SAMPLES];
  struct pcm64_voice_s voices[PCM64_MAX_VOICES];
  uint8_t active_list[PCM64_MAX_VOICES];
  int active_count;
  /* stats */
  uint64_t cubic_samples;
  uint64_t linear_samples;
  uint32_t mix_peak;
};

void pcm64_engine_init(struct pcm64_engine_s *eng, uint32_t sample_rate);

/* Build 64 distinct RAM PCM fragments (harmonics of a cycle). */
void pcm64_engine_build_frags(struct pcm64_engine_s *eng);

void pcm64_engine_note_off_all(struct pcm64_engine_s *eng);

/* Start one voice on fragment `frag` at midi note (or freq_hz if midi<0). */
int pcm64_engine_voice_on(struct pcm64_engine_s *eng, int frag,
                          int midi_note, uint32_t freq_hz, int velocity);

/* Fire all 64 voices (one per fragment) for stress / CPU load. */
int pcm64_engine_all_on(struct pcm64_engine_s *eng, int base_midi,
                        int velocity);

void pcm64_engine_render(struct pcm64_engine_s *eng, int16_t *out,
                         unsigned frames);

/**
 * Offline CPU load probe: render `seconds` of audio with `voices` active.
 * Returns CPU% = 100 * render_wall / audio_wall  (may exceed 100 if overrun).
 * Also fills *render_us and *audio_us if non-NULL.
 */
int pcm64_engine_bench(struct pcm64_engine_s *eng, int voices, float seconds,
                       double *out_cpu_pct, uint64_t *render_us,
                       uint64_t *audio_us);

#endif /* __APPS_WAVETABLE_SYNTH_PCM64_ENGINE_H */
