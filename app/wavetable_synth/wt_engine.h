/****************************************************************************
 * apps/wavetable_synth/wt_engine.h
 *
 * Wavetable oscillator engine for BES A7.
 ****************************************************************************/

#ifndef __APPS_WAVETABLE_SYNTH_WT_ENGINE_H
#define __APPS_WAVETABLE_SYNTH_WT_ENGINE_H

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define WT_DEFAULT_TABLE_LEN  1024
#define WT_MAX_VOICES         4
#define WT_MAX_TABLE_LEN      8192

extern const int16_t g_wt_sine_table[WT_DEFAULT_TABLE_LEN];

struct wt_voice_s
{
  bool active;
  uint32_t phase;       /* Q16.16 index into table */
  uint32_t phase_inc;   /* Q16.16 */
  uint16_t velocity;    /* 0..1000 */
  uint32_t env;         /* Q16.16 amplitude envelope */
  uint32_t env_decay;   /* per-sample decay */
};

struct wt_engine_s
{
  const int16_t *table;
  uint32_t table_len;
  int16_t *owned_table; /* heap table from TF card, or NULL */
  uint32_t sample_rate;
  struct wt_voice_s voices[WT_MAX_VOICES];
};

void wt_engine_init(FAR struct wt_engine_s *eng, uint32_t sample_rate);
void wt_engine_fini(FAR struct wt_engine_s *eng);

/* Load raw int16 LE mono single-cycle wavetable, or RIFF WAVE PCM. */
int wt_engine_load_file(FAR struct wt_engine_s *eng, FAR const char *path);

void wt_engine_use_builtin_sine(FAR struct wt_engine_s *eng);

/* MIDI note 0..127; velocity 0..1000. duration_ms=0 means sustain until note_off. */
int wt_engine_note_on(FAR struct wt_engine_s *eng, int midi_note,
                      uint16_t velocity, uint32_t duration_ms);

/* Frequency in Hz (e.g. 600). velocity 0..1000. duration_ms=0 sustains. */
int wt_engine_tone_on(FAR struct wt_engine_s *eng, uint32_t freq_hz,
                      uint16_t velocity, uint32_t duration_ms);
void wt_engine_note_off_all(FAR struct wt_engine_s *eng);

/* Render interleaved stereo int16 into out[frames * channels]. */
void wt_engine_render(FAR struct wt_engine_s *eng, FAR int16_t *out,
                      unsigned frames, unsigned channels);

#endif /* __APPS_WAVETABLE_SYNTH_WT_ENGINE_H */
