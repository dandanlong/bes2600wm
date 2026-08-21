/****************************************************************************
 * apps/wavetable_synth/wt_engine.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "wt_engine.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t midi_to_phase_inc(uint32_t sample_rate, int midi_note,
                                  uint32_t table_len)
{
  /* freq = 440 * 2^((note-69)/12)
   * Approximate with integer: use fixed A4 table of cents via power-of-two.
   * Simple float-free approach: precompute using integer exp2 approx.
   */
  static const uint32_t freq_hz_x100[128] =
  {
    /* MIDI 0..127 frequency * 100, approximate equal temperament */
      818,    866,    918,    972,   1030,   1091,   1156,   1225,
     1298,   1375,   1457,   1543,   1635,   1732,   1835,   1945,
     2060,   2183,   2312,   2450,   2596,   2750,   2914,   3087,
     3270,   3465,   3671,   3889,   4120,   4365,   4625,   4900,
     5191,   5500,   5827,   6174,   6541,   6929,   7342,   7778,
     8241,   8731,   9250,   9800,  10382,  11000,  11654,  12347,
    13081,  13859,  14683,  15556,  16481,  17461,  18500,  19600,
    20765,  22000,  23308,  24694,  26163,  27718,  29366,  31113,
    32963,  34923,  36999,  39199,  41530,  44000,  46616,  49388,
    52325,  55437,  58733,  62225,  65925,  69846,  73999,  78399,
    83061,  88000,  93233,  98777, 104650, 110873, 117466, 124451,
   131851, 139691, 148998, 156798, 166122, 176000, 186466, 197553,
   209300, 221746, 234932, 248902, 263702, 279383, 295996, 313596,
   332244, 352000, 372931, 395107, 418601, 443492, 469864, 497803,
   527404, 558765, 591991, 627193, 664488, 704000, 745862, 790213,
   837201, 886984, 939727, 995607,1054808,1117530,1183983,1254386
  };

  uint32_t freq_x100;
  uint64_t inc;

  if (midi_note < 0)
    {
      midi_note = 0;
    }
  else if (midi_note > 127)
    {
      midi_note = 127;
    }

  freq_x100 = freq_hz_x100[midi_note];

  /* phase_inc = freq * table_len / sample_rate  in Q16.16
   * = (freq_x100 * table_len << 16) / (sample_rate * 100)
   */
  inc = ((uint64_t)freq_x100 * table_len) << 16;
  inc /= ((uint64_t)sample_rate * 100);
  if (inc == 0)
    {
      inc = 1;
    }

  return (uint32_t)inc;
}

static int16_t lerp_sample(FAR const int16_t *table, uint32_t table_len,
                           uint32_t phase)
{
  uint32_t idx;
  uint32_t frac;
  uint32_t idx2;
  int32_t s0;
  int32_t s1;

  /* phase is Q16.16 index */
  idx = (phase >> 16) % table_len;
  frac = phase & 0xffff;
  idx2 = idx + 1;
  if (idx2 >= table_len)
    {
      idx2 = 0;
    }

  s0 = table[idx];
  s1 = table[idx2];
  return (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
}

static int load_raw_i16(FAR struct wt_engine_s *eng, int fd, off_t nbytes)
{
  size_t samples;
  FAR int16_t *buf;
  ssize_t nread;

  if (nbytes < 2 || (nbytes & 1))
    {
      return -EINVAL;
    }

  samples = (size_t)(nbytes / 2);
  if (samples > WT_MAX_TABLE_LEN)
    {
      samples = WT_MAX_TABLE_LEN;
      nbytes = (off_t)(samples * 2);
    }

  buf = (FAR int16_t *)malloc(samples * sizeof(int16_t));
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  nread = read(fd, buf, (size_t)nbytes);
  if (nread != nbytes)
    {
      free(buf);
      return -EIO;
    }

  if (eng->owned_table)
    {
      free(eng->owned_table);
    }

  eng->owned_table = buf;
  eng->table = buf;
  eng->table_len = (uint32_t)samples;
  return OK;
}

static int load_wav_pcm(FAR struct wt_engine_s *eng, int fd)
{
  uint8_t hdr[44];
  ssize_t n;
  uint16_t audio_format;
  uint16_t channels;
  uint16_t bits;
  uint32_t data_size;
  off_t data_off;

  n = read(fd, hdr, sizeof(hdr));
  if (n != (ssize_t)sizeof(hdr))
    {
      return -EIO;
    }

  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
      /* Not WAV: rewind and treat as raw */
      if (lseek(fd, 0, SEEK_SET) < 0)
        {
          return -errno;
        }

      data_size = (uint32_t)lseek(fd, 0, SEEK_END);
      if (lseek(fd, 0, SEEK_SET) < 0)
        {
          return -errno;
        }

      return load_raw_i16(eng, fd, (off_t)data_size);
    }

  audio_format = (uint16_t)(hdr[20] | (hdr[21] << 8));
  channels = (uint16_t)(hdr[22] | (hdr[23] << 8));
  bits = (uint16_t)(hdr[34] | (hdr[35] << 8));
  data_size = (uint32_t)(hdr[40] | (hdr[41] << 8) |
                         (hdr[42] << 16) | (hdr[43] << 24));
  data_off = 44;

  /* Handle non-canonical chunk layout: search for "data" */
  if (memcmp(hdr + 36, "data", 4) != 0)
    {
      uint8_t chunk[8];
      off_t pos = 12;

      if (lseek(fd, pos, SEEK_SET) < 0)
        {
          return -errno;
        }

      for (; ; )
        {
          if (read(fd, chunk, 8) != 8)
            {
              return -EIO;
            }

          data_size = (uint32_t)(chunk[4] | (chunk[5] << 8) |
                                 (chunk[6] << 16) | (chunk[7] << 24));
          if (memcmp(chunk, "data", 4) == 0)
            {
              data_off = lseek(fd, 0, SEEK_CUR);
              break;
            }

          pos = lseek(fd, data_size, SEEK_CUR);
          if (pos < 0)
            {
              return -errno;
            }
        }
    }

  if (audio_format != 1 || channels != 1 || bits != 16)
    {
      printf("wtsynth: WAV must be PCM mono 16-bit (fmt=%u ch=%u bits=%u)\n",
             audio_format, channels, bits);
      return -ENOTSUP;
    }

  if (lseek(fd, data_off, SEEK_SET) < 0)
    {
      return -errno;
    }

  return load_raw_i16(eng, fd, (off_t)data_size);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void wt_engine_init(FAR struct wt_engine_s *eng, uint32_t sample_rate)
{
  memset(eng, 0, sizeof(*eng));
  eng->sample_rate = sample_rate ? sample_rate : 48000;
  wt_engine_use_builtin_sine(eng);
}

void wt_engine_fini(FAR struct wt_engine_s *eng)
{
  if (eng->owned_table)
    {
      free(eng->owned_table);
      eng->owned_table = NULL;
    }

  eng->table = NULL;
  eng->table_len = 0;
}

void wt_engine_use_builtin_sine(FAR struct wt_engine_s *eng)
{
  if (eng->owned_table)
    {
      free(eng->owned_table);
      eng->owned_table = NULL;
    }

  eng->table = g_wt_sine_table;
  eng->table_len = WT_DEFAULT_TABLE_LEN;
}

int wt_engine_load_file(FAR struct wt_engine_s *eng, FAR const char *path)
{
  int fd;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      printf("wtsynth: open %s failed: %d\n", path, errno);
      return -errno;
    }

  ret = load_wav_pcm(eng, fd);
  close(fd);

  if (ret == OK)
    {
      printf("wtsynth: loaded %s (%lu samples)\n",
             path, (unsigned long)eng->table_len);
    }

  return ret;
}

static int voice_on_with_inc(FAR struct wt_engine_s *eng, uint32_t phase_inc,
                             uint16_t velocity, uint32_t duration_ms)
{
  int i;
  int slot = -1;
  uint32_t decay_samples;

  for (i = 0; i < WT_MAX_VOICES; i++)
    {
      if (!eng->voices[i].active)
        {
          slot = i;
          break;
        }
    }

  if (slot < 0)
    {
      /* Steal voice 0 */
      slot = 0;
    }

  if (velocity == 0)
    {
      velocity = 800;
    }
  else if (velocity > 1000)
    {
      velocity = 1000;
    }

  if (phase_inc == 0)
    {
      phase_inc = 1;
    }

  eng->voices[slot].active = true;
  eng->voices[slot].phase = 0;
  eng->voices[slot].phase_inc = phase_inc;
  eng->voices[slot].velocity = velocity;
  eng->voices[slot].env = 0x10000; /* 1.0 in Q16.16 */

  if (duration_ms == 0)
    {
      eng->voices[slot].env_decay = 0; /* sustain */
    }
  else
    {
      decay_samples = (eng->sample_rate * duration_ms) / 1000;
      if (decay_samples == 0)
        {
          decay_samples = 1;
        }

      eng->voices[slot].env_decay = 0x10000 / decay_samples;
      if (eng->voices[slot].env_decay == 0)
        {
          eng->voices[slot].env_decay = 1;
        }
    }

  return slot;
}

int wt_engine_note_on(FAR struct wt_engine_s *eng, int midi_note,
                      uint16_t velocity, uint32_t duration_ms)
{
  return voice_on_with_inc(eng,
                           midi_to_phase_inc(eng->sample_rate, midi_note,
                                             eng->table_len),
                           velocity, duration_ms);
}

int wt_engine_tone_on(FAR struct wt_engine_s *eng, uint32_t freq_hz,
                      uint16_t velocity, uint32_t duration_ms)
{
  uint64_t inc;

  if (freq_hz == 0)
    {
      freq_hz = 440;
    }

  /* phase_inc = freq * table_len / sample_rate in Q16.16 */
  inc = ((uint64_t)freq_hz * eng->table_len) << 16;
  if (eng->sample_rate != 0)
    {
      inc /= eng->sample_rate;
    }

  return voice_on_with_inc(eng, (uint32_t)inc, velocity, duration_ms);
}

void wt_engine_note_off_all(FAR struct wt_engine_s *eng)
{
  int i;

  for (i = 0; i < WT_MAX_VOICES; i++)
    {
      eng->voices[i].active = false;
      eng->voices[i].env = 0;
    }
}

void wt_engine_render(FAR struct wt_engine_s *eng, FAR int16_t *out,
                      unsigned frames, unsigned channels)
{
  unsigned f;
  int v;

  if (channels == 0)
    {
      channels = 1;
    }

  for (f = 0; f < frames; f++)
    {
      int32_t mix = 0;
      int active = 0;

      for (v = 0; v < WT_MAX_VOICES; v++)
        {
          FAR struct wt_voice_s *voice = &eng->voices[v];
          int32_t s;
          int32_t amp;

          if (!voice->active)
            {
              continue;
            }

          active++;
          s = lerp_sample(eng->table, eng->table_len, voice->phase);
          amp = ((int32_t)voice->velocity * (int32_t)(voice->env >> 8)) / 1000;
          mix += (s * amp) >> 8;

          voice->phase += voice->phase_inc;

          if (voice->env_decay)
            {
              if (voice->env <= voice->env_decay)
                {
                  voice->env = 0;
                  voice->active = false;
                }
              else
                {
                  voice->env -= voice->env_decay;
                }
            }
        }

      if (active > 1)
        {
          mix /= active;
        }

      if (mix > 32767)
        {
          mix = 32767;
        }
      else if (mix < -32768)
        {
          mix = -32768;
        }

      if (channels == 1)
        {
          out[f] = (int16_t)mix;
        }
      else
        {
          out[f * channels] = (int16_t)mix;
          out[f * channels + 1] = (int16_t)mix;
          for (v = 2; (unsigned)v < channels; v++)
            {
              out[f * channels + v] = (int16_t)mix;
            }
        }
    }
}
