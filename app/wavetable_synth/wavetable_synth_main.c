/****************************************************************************
 * apps/wavetable_synth/wavetable_synth_main.c
 *
 * A7 wavetable synthesizer: load table from TF (/sdcard), play via PCM.
 *
 * Usage:
 *   wtsynth [-w /sdcard/wave.wav] [-d /dev/audio/pcm0p] [-r 48000]
 *           [-n midi | -f hz] [-t ms] [-v vol]
 *
 * Defaults: builtin sine, MIDI 60 (C4), 2000 ms, device pcm0p.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <time.h>
#include <stdint.h>

#include <nuttx/audio/audio.h>
#include <nuttx/fs/ioctl.h>
#include <audioutils/nxaudio.h>

#include "wt_engine.h"
#include "pcm64_engine.h"
#include "pcm_sample_engine.h"
#include "midi_rpmsg_receiver.h"
#include "lc3_bench.h"

/* Avoid pulling BES HAL headers into apps (CHIP_ID deps). */
enum { PCM64_CMU_FREQ_780M = 11 }; /* HAL_CMU_FREQ_780M in best2003 */
int hal_cmu_dsp_set_freq(int freq);
int hal_cmu_dsp_get_freq(void);
uint32_t hal_sys_timer_calc_cpu_freq(uint32_t osc_intvl_ms, int high_res);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define APP_FS_DEFAULT     44100
#define APP_BPS            16
#define APP_CHNUM          1
#define APP_DEFAULT_VOL    400
#define APP_DEFAULT_NOTE   60
#define APP_DEFAULT_MS     2000
#define APP_DEFAULT_DEV    "/dev/audio/pcm0p"
#define APP_DEFAULT_WAVE   "/sdcard/wavetable/sine.wav"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wtsynth_s
{
  struct nxaudio_s nxaudio;
  struct wt_engine_s engine;
  volatile bool running;
  bool pcm_mode;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wtsynth_s g_app;
static struct pcm_samp_engine_s g_pcm;

#ifndef CONFIG_AUDIO_RPMSG_CLIENT
/* TRANSQ DMA callback: AP pulls PCM via high-prio transq IRQ messages. */
static uint32_t wt_transq_playback_cb(uint8_t *buf, uint32_t len)
{
  unsigned frames;

  if (buf == NULL || len < sizeof(int16_t))
    {
      return 0;
    }

  frames = len / sizeof(int16_t); /* mono */
  if (g_app.pcm_mode)
    {
      pcm_samp_render(&g_pcm, (FAR int16_t *)buf, frames);
    }
  else
    {
      wt_engine_render(&g_app.engine, (FAR int16_t *)buf, frames, 1);
    }
  return len;
}
#else
static int wait_audio_device(FAR const char *dev, unsigned int timeout_ms)
{
  unsigned int elapsed = 0;

  while (elapsed < timeout_ms)
    {
      if (access(dev, F_OK) == 0)
        {
          if (elapsed > 0)
            {
              syslog(LOG_NOTICE,
                     "wtsynth: audio device %s ready after %ums\n",
                     dev, elapsed);
            }
          return 0;
        }

      usleep(50000);
      elapsed += 50;
    }

  return -ETIMEDOUT;
}
#endif

static void app_dequeue_cb(unsigned long arg, FAR struct ap_buffer_s *apb);
static void app_complete_cb(unsigned long arg);
static void app_user_cb(unsigned long arg, FAR struct audio_msg_s *msg,
                        FAR bool *running);

static struct nxaudio_callbacks_s g_cbs =
{
  app_dequeue_cb,
  app_complete_cb,
  app_user_cb
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void app_dequeue_cb(unsigned long arg, FAR struct ap_buffer_s *apb)
{
  FAR struct wtsynth_s *app = (FAR struct wtsynth_s *)(uintptr_t)arg;
  unsigned frames;
  struct timespec t0;
  struct timespec t1;
  uint32_t cb_us;
  uint32_t budget_us;
  static uint32_t s_slow_cb;
  static uint32_t s_cb_max;

  apb->curbyte = 0;
  apb->flags = 0;

  frames = apb->nmaxbytes / (sizeof(int16_t) * app->nxaudio.chnum);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  if (app->pcm_mode)
    {
      pcm_samp_render(&g_pcm, (FAR int16_t *)apb->samp, frames);
    }
  else
    {
      wt_engine_render(&app->engine, (FAR int16_t *)apb->samp,
                       frames, app->nxaudio.chnum);
    }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  cb_us = (uint32_t)((int64_t)(t1.tv_sec - t0.tv_sec) * 1000000ll +
                     ((int64_t)t1.tv_nsec - t0.tv_nsec) / 1000ll);
  if (cb_us > s_cb_max)
    {
      s_cb_max = cb_us;
    }
  budget_us = (app->pcm_mode && g_pcm.out_rate) ?
              (uint32_t)((uint64_t)frames * 1000000ull / g_pcm.out_rate) : 0;
  if (budget_us && cb_us > (budget_us * 4u) / 5u && s_slow_cb < 24u)
    {
      syslog(LOG_WARNING,
             "pcm_diag: SLOW_CB frames=%u cb=%uus budget=%uus "
             "max_cb=%uus active=%d\n",
             frames, cb_us, budget_us, s_cb_max,
             app->pcm_mode ? g_pcm.active_count : 0);
      s_slow_cb++;
    }
  apb->nbytes = frames * sizeof(int16_t) * app->nxaudio.chnum;

  if (app->running)
    {
      nxaudio_enqbuffer(&app->nxaudio, apb);
    }
}

static void app_complete_cb(unsigned long arg)
{
  (void)arg;
  printf("wtsynth: audio complete\n");
}

static void app_user_cb(unsigned long arg, FAR struct audio_msg_s *msg,
                        FAR bool *running)
{
  (void)arg;
  (void)msg;
  (void)running;
}

static FAR void *audio_loop_thread(pthread_addr_t arg)
{
  FAR struct wtsynth_s *app = (FAR struct wtsynth_s *)arg;

  nxaudio_start(&app->nxaudio);
  nxaudio_msgloop(&app->nxaudio, &g_cbs, (unsigned long)app);
  return NULL;
}

static pthread_t create_audio_thread(FAR struct wtsynth_s *app)
{
  pthread_t pid;
  pthread_attr_t tattr;
  struct sched_param sparam;

  pthread_attr_init(&tattr);
  sparam.sched_priority = sched_get_priority_max(SCHED_FIFO) - 9;
  pthread_attr_setschedparam(&tattr, &sparam);
  pthread_attr_setstacksize(&tattr,
                            CONFIG_WAVETABLE_SYNTH_STACKSIZE);
#ifdef CONFIG_SMP
  {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_attr_setaffinity_np(&tattr, sizeof(cpuset), &cpuset);
  }
#endif

  pthread_create(&pid, &tattr, audio_loop_thread, app);
  pthread_attr_destroy(&tattr);
  syslog(LOG_NOTICE, "wtsynth: audio/mix thread pinned to CPU0\n");
  return pid;
}

static void show_usage(FAR const char *prog)
{
  printf("Wavetable synth (A7)\n");
  printf("Usage: %s [options]\n", prog);
  printf("  -pcm <bank> stream DBNK v2 mono int16 PCM from SD/FAT\n");
  printf("     [-i inst] [-n note] [-demo] [-test] [-cache KiB (ignored)]\n");
  printf("  -bench [voices] [seconds] [rate] keeps RAM CPU benchmark\n");
  printf("  -w <path>   wavetable file (raw int16 or WAV mono 16-bit)\n");
  printf("              default try %s, else builtin sine\n", APP_DEFAULT_WAVE);
  printf("  -d <dev>    audio device (default %s)\n", APP_DEFAULT_DEV);
  printf("  -r <hz>     sample rate (default %d)\n", APP_FS_DEFAULT);
  printf("  -n <midi>   MIDI note (default %d)\n", APP_DEFAULT_NOTE);
  printf("  -f <hz>     tone frequency in Hz (overrides -n)\n");
  printf("  -t <ms>     duration ms (default %d, 0=play forever)\n",
         APP_DEFAULT_MS);
  printf("  -v <0-1000> volume (default %d)\n", APP_DEFAULT_VOL);
  printf("  -h          help\n");
  printf("\nTF card is mounted at /sdcard (see rcS.audio).\n");
  printf("Put single-cycle tables under /sdcard/wavetable/\n");
}

static bool path_exists(FAR const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

/* Faithful port of P4_WaveTable/main/guitar_demo.c.
 * DBNK instrument 0=high E ... 5=low E (PSus1...PSus6).
 */

struct guitar_note_s
{
  uint8_t inst;
  uint8_t note;
  uint8_t vel;
};

struct guitar_demo_s
{
  FAR struct pcm_samp_engine_s *eng;
  unsigned limit_ms;
  unsigned elapsed_ms;
  uint8_t eq_log_bucket;
  volatile bool running;
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const struct guitar_note_s g_em[] =
{
  {5, 40, 105}, {4, 45, 100}, {3, 52, 95},
  {2, 55,  92}, {1, 59,  90}, {0, 64, 88}
};

/* Open E major: E B E G# B E */
static const struct guitar_note_s g_e[] =
{
  {5, 40, 105}, {4, 47, 100}, {3, 52, 96},
  {2, 56,  92}, {1, 59,  90}, {0, 64, 88}
};

static const struct guitar_note_s g_g[] =
{
  {5, 43, 105}, {4, 47, 100}, {3, 50, 95},
  {2, 55,  92}, {1, 59,  90}, {0, 67, 88}
};

static const struct guitar_note_s g_c[] =
{
  {4, 48, 100}, {3, 52, 96}, {2, 55, 92},
  {1, 60,  90}, {0, 64, 88}
};

static const struct guitar_note_s g_d[] =
{
  {3, 50, 108}, {2, 57, 102}, {1, 62, 98}, {0, 66, 94}
};

static const struct guitar_note_s g_am[] =
{
  {4, 45, 106}, {3, 52, 100}, {2, 57, 96},
  {1, 60, 94}, {0, 64, 90}
};

static void startup_wait_half0(FAR struct pcm_samp_engine_s *eng, int slot,
                               unsigned timeout_ms)
{
  unsigned waited = 0;

  if (!eng || slot < 0 || slot >= PCM_SAMP_MAX_VOICES)
    {
      return;
    }

  while (waited < timeout_ms)
    {
      uint32_t st = atomic_load_explicit(&eng->voices[slot].half[0].state,
                                         memory_order_acquire);
      if (st == PCM_HALF_READY)
        {
          return;
        }

      usleep(2000);
      waited += 2u;
    }
}

static void guitar_startup_sound(FAR struct pcm_samp_engine_s *eng)
{
  int i;
  int slot;

  /* Startup-only (MIDI path untouched): first chord after silence can sound
   * sandy.  Warm with silence, then re-arm the bus fade and strum underneath
   * so the onset is covered — no engine/I2S changes.
   */
  pcm_samp_wait_start_fade(eng, 800);
  usleep(250000);

  pcm_samp_arm_start_fade(eng);
  syslog(LOG_NOTICE, "wtsynth: startup sound C strum under fade\n");
  for (i = 0; i < (int)ARRAY_SIZE(g_c); i++)
    {
      slot = pcm_samp_note_on(eng, g_c[i].inst, g_c[i].note, g_c[i].vel);
      startup_wait_half0(eng, slot, 80);
      usleep(30000);
    }

  usleep(650000);
  for (i = 0; i < (int)ARRAY_SIZE(g_c); i++)
    {
      pcm_samp_note_off(eng, g_c[i].inst, g_c[i].note);
    }
  usleep(700000);
}

static const struct guitar_note_s g_picks[] =
{
  {5, 40, 110}, {5, 42, 100}, {5, 43, 105}, {5, 45, 100},
  {4, 45, 108}, {4, 47, 100}, {4, 48, 105}, {3, 50, 100},
  {3, 52, 108}, {2, 55, 100}, {1, 59, 105}, {0, 64, 100}
};

/* Classic 1-2-3-4 grid, low E -> high E. */
static const struct guitar_note_s g_grid[] =
{
  {5, 40, 114}, {5, 41, 112}, {5, 42, 110}, {5, 43, 108},
  {4, 45, 112}, {4, 46, 110}, {4, 47, 108}, {4, 48, 106},
  {3, 50, 112}, {3, 51, 110}, {3, 52, 108}, {3, 53, 106},
  {2, 55, 110}, {2, 56, 108}, {2, 57, 106}, {2, 58, 104},
  {1, 59, 112}, {1, 60, 110}, {1, 61, 108}, {1, 62, 106},
  {0, 64, 114}, {0, 65, 112}, {0, 66, 110}, {0, 67, 108}
};

static void guitar_demo_eq_update(FAR struct guitar_demo_s *demo)
{
  unsigned bucket;

  bucket = demo->elapsed_ms / 10000u;
  if (bucket != demo->eq_log_bucket)
    {
      demo->eq_log_bucket = (uint8_t)bucket;
      syslog(LOG_NOTICE,
             "guitar_demo: bus dry t=%ums (legacy EQ removed)\n",
             demo->elapsed_ms);
    }
}

static bool guitar_demo_wait(FAR struct guitar_demo_s *demo, unsigned ms)
{
  unsigned wait_ms = ms;

  if (!demo->running)
    {
      return false;
    }

  if (demo->limit_ms != 0)
    {
      if (demo->elapsed_ms >= demo->limit_ms)
        {
          demo->running = false;
          return false;
        }

      if (wait_ms > demo->limit_ms - demo->elapsed_ms)
        {
          wait_ms = demo->limit_ms - demo->elapsed_ms;
        }
    }

  usleep((useconds_t)wait_ms * 1000u);
  demo->elapsed_ms += wait_ms;
  guitar_demo_eq_update(demo);
  if (demo->limit_ms != 0 && demo->elapsed_ms >= demo->limit_ms)
    {
      demo->running = false;
    }

  return demo->running;
}

static void guitar_demo_note(FAR struct guitar_demo_s *demo,
                             FAR const struct guitar_note_s *note)
{
  if (demo->running)
    {
      pcm_samp_note_on(demo->eng, note->inst, note->note, note->vel);
    }
}

static bool guitar_demo_strum(FAR struct guitar_demo_s *demo,
                              FAR const struct guitar_note_s *chord,
                              int count, int gap_ms, bool up)
{
  int i;

  if (up)
    {
      for (i = 0; i < count && demo->running; i++)
        {
          guitar_demo_note(demo, &chord[i]);
          guitar_demo_wait(demo, gap_ms);
        }
    }
  else
    {
      for (i = count - 1; i >= 0 && demo->running; i--)
        {
          guitar_demo_note(demo, &chord[i]);
          guitar_demo_wait(demo, gap_ms);
        }
    }

  return demo->running;
}

static void guitar_demo_log(FAR struct guitar_demo_s *demo,
                            FAR const char *where)
{
  syslog(LOG_NOTICE, "guitar_demo: %s active=%d peak=%d/%d elapsed=%ums\n",
         where, demo->eng->active_count, demo->eng->peak_active,
         PCM_SAMP_MAX_VOICES, demo->elapsed_ms);
  pcm_samp_stats(demo->eng, where);
}

static bool guitar_demo_before(FAR struct guitar_demo_s *demo,
                               unsigned deadline_ms)
{
  return demo->running && demo->elapsed_ms < deadline_ms;
}

static void guitar_demo_wait_until(FAR struct guitar_demo_s *demo,
                                   unsigned deadline_ms)
{
  while (guitar_demo_before(demo, deadline_ms))
    {
      unsigned left = deadline_ms - demo->elapsed_ms;
      guitar_demo_wait(demo, left > 200u ? 200u : left);
    }
}

static void guitar_demo_grid(FAR struct guitar_demo_s *demo, unsigned ms)
{
  unsigned deadline = demo->elapsed_ms + ms;
  int pass = 0;
  int i;

  syslog(LOG_NOTICE, "guitar_demo: phrase 1234 grid (%ums)\n", ms);
  while (guitar_demo_before(demo, deadline))
    {
      int fret_off = (pass % 3) * 2;
      for (i = 0; i < (int)ARRAY_SIZE(g_grid) &&
                  guitar_demo_before(demo, deadline); i++)
        {
          struct guitar_note_s note = g_grid[i];
          note.note = (uint8_t)((int)note.note + fret_off > 84 ?
                                84 : (int)note.note + fret_off);
          guitar_demo_note(demo, &note);
          guitar_demo_wait(demo, 200);
        }
      for (i = (int)ARRAY_SIZE(g_grid) - 2;
           i >= 0 && guitar_demo_before(demo, deadline); i--)
        {
          struct guitar_note_s note = g_grid[i];
          note.note = (uint8_t)((int)note.note + fret_off > 84 ?
                                84 : (int)note.note + fret_off);
          guitar_demo_note(demo, &note);
          guitar_demo_wait(demo, 200);
        }
      pass++;
    }
  guitar_demo_log(demo, "grid-end");
}

static void guitar_demo_arp(FAR struct guitar_demo_s *demo,
                            FAR const struct guitar_note_s *chord,
                            int count, int style, int gap_ms)
{
  static const int8_t pat4[] = {0, 2, 1, 3, 2, 0, 1, 3};
  static const int8_t pat5[] = {0, 2, 1, 3, 4, 2, 1, 0};
  static const int8_t pat6[] = {0, 2, 1, 3, 2, 4, 5, 3, 1, 0};
  FAR const int8_t *pat;
  int plen;
  int i;

  if (style == 0)
    {
      pat = count >= 6 ? pat6 : (count >= 5 ? pat5 : pat4);
      plen = count >= 6 ? (int)ARRAY_SIZE(pat6) :
             (count >= 5 ? (int)ARRAY_SIZE(pat5) :
                           (int)ARRAY_SIZE(pat4));
      for (i = 0; i < plen && demo->running; i++)
        {
          if (pat[i] < count)
            {
              guitar_demo_note(demo, &chord[pat[i]]);
              guitar_demo_wait(demo, (unsigned)gap_ms);
            }
        }
    }
  else if (style == 1)
    {
      for (i = 0; i < count && demo->running; i++)
        {
          guitar_demo_note(demo, &chord[i]);
          guitar_demo_wait(demo, (unsigned)gap_ms);
        }
      for (i = count - 2; i >= 0 && demo->running; i--)
        {
          guitar_demo_note(demo, &chord[i]);
          guitar_demo_wait(demo, (unsigned)gap_ms);
        }
    }
  else
    {
      guitar_demo_strum(demo, chord, count, gap_ms, style == 2);
    }
}

static void guitar_demo_release_chord(FAR struct guitar_demo_s *demo,
                                      FAR const struct guitar_note_s *chord,
                                      int count)
{
  int i;

  for (i = 0; i < count; i++)
    {
      pcm_samp_note_off(demo->eng, chord[i].inst, chord[i].note);
    }
}

/* Guitar string 1 = high E = bank inst 0 ... string 6 = low E = inst 5. */
static FAR const struct guitar_note_s *guitar_demo_find_string(
  FAR const struct guitar_note_s *chord, int count, int string_1to6)
{
  uint8_t inst = (uint8_t)(string_1to6 - 1);
  int i;

  for (i = 0; i < count; i++)
    {
      if (chord[i].inst == inst)
        {
          return &chord[i];
        }
    }
  return NULL;
}

static void guitar_demo_pluck_string(
  FAR struct guitar_demo_s *demo,
  FAR const struct guitar_note_s *chord, int count, int string_1to6)
{
  FAR const struct guitar_note_s *n =
    guitar_demo_find_string(chord, count, string_1to6);

  if (!n)
    {
      return;
    }

  /* Real guitar: re-picking a string stops the previous ring first.
   * Without this, 53231323 stacks four open-G voices and sounds wrong.
   */
  pcm_samp_note_off(demo->eng, n->inst, n->note);
  guitar_demo_note(demo, n);
}

static void guitar_demo_pick_53231323(
  FAR struct guitar_demo_s *demo,
  FAR const struct guitar_note_s *chord,
  int count, unsigned gap_ms, int bass_string)
{
  /* Absolute string numbers, same as 右手分解:
   * C  = 5 3 2 3 1 3 2 3
   * D  = 4 3 2 3 1 3 2 3
   * Em = 6 3 2 3 1 3 2 3
   */
  const uint8_t pat[8] =
  {
    (uint8_t)bass_string, 3, 2, 3, 1, 3, 2, 3
  };
  int i;

  for (i = 0; i < 8 && demo->running; i++)
    {
      guitar_demo_pluck_string(demo, chord, count, pat[i]);
      guitar_demo_wait(demo, gap_ms);
    }
}

static void guitar_demo_arp_progression(FAR struct guitar_demo_s *demo,
                                        unsigned ms)
{
  struct chord_ref_s
  {
    FAR const struct guitar_note_s *chord;
    uint8_t count;
  };
  static const struct chord_ref_s prog[] =
  {
    {g_c, 5},  {g_g, 6},  {g_am, 5}, {g_em, 6},
    {g_c, 5},  {g_em, 6}, {g_am, 5}, {g_g, 6},
    {g_c, 5},  {g_d, 4},  {g_em, 6}, {g_g, 6},
    {g_am, 5}, {g_em, 6}, {g_d, 4},  {g_g, 6},
    {g_c, 5},  {g_g, 6},  {g_d, 4},  {g_em, 6}
  };
  unsigned deadline = demo->elapsed_ms + ms;
  int step = 0;

  syslog(LOG_NOTICE, "guitar_demo: phrase chord arpeggios (%ums)\n", ms);
  while (guitar_demo_before(demo, deadline))
    {
      FAR const struct chord_ref_s *cr = &prog[step % ARRAY_SIZE(prog)];
      int gap = 130 + (step % 3) * 10;
      guitar_demo_arp(demo, cr->chord, cr->count, step % 4, gap);
      guitar_demo_wait(demo, 120 + (step & 1) * 40);
      step++;
    }
  guitar_demo_log(demo, "arp-prog-end");
}

static void guitar_demo_natural(FAR struct guitar_demo_s *demo, unsigned ms)
{
  unsigned deadline = demo->elapsed_ms + ms;
  int i;

  syslog(LOG_NOTICE, "guitar_demo: phrase natural decay (%ums)\n", ms);
  for (i = 0; i < (int)ARRAY_SIZE(g_picks) &&
              guitar_demo_before(demo, deadline); i++)
    {
      guitar_demo_note(demo, &g_picks[i]);
      guitar_demo_wait(demo, 280);
    }
  if (guitar_demo_before(demo, deadline))
    {
      guitar_demo_strum(demo, g_em, ARRAY_SIZE(g_em), 32, false);
      guitar_demo_wait(demo, 450);
      guitar_demo_strum(demo, g_em, ARRAY_SIZE(g_em), 28, true);
      guitar_demo_wait(demo, 400);
      guitar_demo_strum(demo, g_g, ARRAY_SIZE(g_g), 26, false);
      guitar_demo_wait(demo, 400);
      guitar_demo_strum(demo, g_c, ARRAY_SIZE(g_c), 24, false);
      guitar_demo_wait(demo, 350);
    }
  guitar_demo_wait_until(demo, deadline);
  guitar_demo_log(demo, "natural-end");
}

static void guitar_demo_continuous(FAR struct guitar_demo_s *demo,
                                   unsigned ms)
{
  unsigned deadline = demo->elapsed_ms + ms;
  int cycle = 0;
  int i;

  syslog(LOG_NOTICE, "guitar_demo: phrase continuous play (%ums)\n", ms);
  while (guitar_demo_before(demo, deadline))
    {
      for (i = 0; i < (int)ARRAY_SIZE(g_picks) &&
                  guitar_demo_before(demo, deadline); i++)
        {
          guitar_demo_note(demo, &g_picks[i]);
          guitar_demo_wait(demo, 160);
        }
      guitar_demo_strum(demo, g_em, ARRAY_SIZE(g_em), 24, false);
      guitar_demo_wait(demo, 180);
      guitar_demo_strum(demo, g_em, ARRAY_SIZE(g_em), 20, true);
      guitar_demo_wait(demo, 280);
      guitar_demo_strum(demo, g_g, ARRAY_SIZE(g_g), 16, false);
      guitar_demo_wait(demo, 100);
      guitar_demo_strum(demo, g_c, ARRAY_SIZE(g_c), 14, false);
      guitar_demo_wait(demo, 90);

      for (i = 0; i < 6 && guitar_demo_before(demo, deadline); i++)
        {
          guitar_demo_strum(demo, g_em, ARRAY_SIZE(g_em),
                            i < 2 ? 11 : (i < 4 ? 9 : 7), false);
          guitar_demo_strum(demo, (i & 1) ? g_c : g_g,
                            (i & 1) ? ARRAY_SIZE(g_c) : ARRAY_SIZE(g_g),
                            i < 2 ? 11 : (i < 4 ? 9 : 7), (i & 1) == 0);
        }
      cycle++;
      if ((cycle & 1) == 0)
        {
          guitar_demo_log(demo, "continuous-checkpoint");
        }
    }
  guitar_demo_log(demo, "continuous-end");
}

static void guitar_demo_melody(FAR struct guitar_demo_s *demo, unsigned ms)
{
  /* C-D-Em beginner fingerpicking loop. Keep other strings ringing; only the
   * re-picked string is muted+replucked, matching a real right hand.
   */
  struct chord_ref_s
  {
    FAR const struct guitar_note_s *chord;
    uint8_t count;
    uint8_t bass_string;
  };
  static const struct chord_ref_s prog[] =
  {
    {g_c,  5, 5}, {g_d,  4, 4}, {g_em, 6, 6}, {g_em, 6, 6},
    {g_c,  5, 5}, {g_d,  4, 4}, {g_em, 6, 6}, {g_em, 6, 6}
  };
  unsigned deadline = demo->elapsed_ms + ms;
  unsigned step = 0;
  unsigned last_log = 0;

  syslog(LOG_NOTICE,
         "guitar_demo: C-D-Em 53231323 melody (%ums)\n", ms);
  syslog(LOG_NOTICE,
         "guitar_demo: verify C=5-3-2-3-1-3-2-3 D=4-3-2-3-1-3-2-3 "
         "Em=6-3-2-3-1-3-2-3\n");

  while (guitar_demo_before(demo, deadline))
    {
      FAR const struct chord_ref_s *cr = &prog[step % ARRAY_SIZE(prog)];
      unsigned gap = 140u;
      int bar;

      /* Two bars per chord. */
      for (bar = 0; bar < 2 && guitar_demo_before(demo, deadline); bar++)
        {
          guitar_demo_pick_53231323(demo, cr->chord, cr->count, gap,
                                    cr->bass_string);
        }

      /* Brief ring, then mute before the next chord shape. */
      guitar_demo_wait(demo, 220);
      guitar_demo_release_chord(demo, cr->chord, cr->count);
      guitar_demo_wait(demo, 60);

      if (demo->elapsed_ms - last_log >= 5000u)
        {
          guitar_demo_log(demo, "melody-step");
          last_log = demo->elapsed_ms;
        }
      step++;
    }
  guitar_demo_wait_until(demo, deadline);
  guitar_demo_log(demo, "melody-end");
}

static unsigned guitar_demo_tempo_gap(unsigned elapsed_ms)
{
  unsigned span = 25000u;
  unsigned t = elapsed_ms > span ? span : elapsed_ms;
  return 110u - (t * 45u) / span;
}

static unsigned guitar_demo_tempo_rest(unsigned elapsed_ms)
{
  unsigned span = 25000u;
  unsigned t = elapsed_ms > span ? span : elapsed_ms;
  return 360u - (t * 140u) / span;
}

static void guitar_demo_chord_ramp(FAR struct guitar_demo_s *demo,
                                   unsigned ms)
{
  struct chord_ref_s
  {
    FAR const struct guitar_note_s *chord;
    uint8_t count;
    uint8_t bass_string;
  };
  static const struct chord_ref_s prog[] =
  {
    {g_c, 5, 5}, {g_d, 4, 4}, {g_em, 6, 6}, {g_em, 6, 6},
    {g_c, 5, 5}, {g_d, 4, 4}, {g_em, 6, 6}, {g_g,  6, 6}
  };
  unsigned start = demo->elapsed_ms;
  unsigned deadline = start + ms;
  unsigned fill_deadline = ms > 4000u ? deadline - 4000u : deadline;
  unsigned step = 0;
  unsigned fill_pass = 0;
  unsigned last_log = 0;

  syslog(LOG_NOTICE,
         "guitar_demo: %ums 53231323+strum test, target=%d voices\n",
         ms, PCM_SAMP_MAX_VOICES);

  while (guitar_demo_before(demo, fill_deadline))
    {
      FAR const struct chord_ref_s *cr = &prog[step % ARRAY_SIZE(prog)];
      unsigned elapsed = demo->elapsed_ms - start;
      unsigned gap = guitar_demo_tempo_gap(elapsed);
      unsigned rest = guitar_demo_tempo_rest(elapsed);

      /* Keep the same right-hand pattern; thicken with a light strum reply. */
      guitar_demo_pick_53231323(demo, cr->chord, cr->count, gap,
                                cr->bass_string);
      if ((step & 1u) != 0)
        {
          guitar_demo_wait(demo, 40);
          guitar_demo_strum(demo, cr->chord, cr->count,
                            gap > 20u ? (int)(gap / 3u) : 8, true);
        }
      guitar_demo_wait(demo, rest);
      guitar_demo_release_chord(demo, cr->chord, cr->count);
      guitar_demo_wait(demo, 60);

      if (demo->elapsed_ms - last_log >= 5000u)
        {
          guitar_demo_log(demo, "chord-ramp-step");
          last_log = demo->elapsed_ms;
        }
      step++;
    }

  /* Final crescendo still uses the same pattern, just faster/overlapping. */
  while (guitar_demo_before(demo, deadline) &&
         demo->eng->active_count < PCM_SAMP_MAX_VOICES)
    {
      FAR const struct chord_ref_s *cr = &prog[fill_pass % ARRAY_SIZE(prog)];
      guitar_demo_pick_53231323(demo, cr->chord, cr->count, 55,
                                cr->bass_string);
      guitar_demo_wait(demo, 40);
      fill_pass++;
    }
  guitar_demo_log(demo, "chord-ramp-64");
  guitar_demo_wait_until(demo, deadline);
}

static FAR void *guitar_demo_thread(pthread_addr_t arg)
{
  FAR struct guitar_demo_s *demo = (FAR struct guitar_demo_s *)arg;

  demo->eng->peak_active = demo->eng->active_count;
  pcm_samp_all_off(demo->eng);
  guitar_demo_melody(demo, 30000);
  pcm_samp_all_off(demo->eng);
  guitar_demo_chord_ramp(demo, 30000);

  pcm_samp_all_off(demo->eng);
  syslog(LOG_NOTICE, "guitar_demo: stopped at %u ms\n", demo->elapsed_ms);
  return NULL;
}


static int run_sdbench_file(FAR const char *path, unsigned iters);
static int run_rawbench(unsigned iters);

static int run_pcm_mode(int argc, FAR char *argv[])
{
  FAR const char *bank = argv[2];
  FAR const char *dev = APP_DEFAULT_DEV;
  int rate = APP_FS_DEFAULT;
  int dur_ms = APP_DEFAULT_MS;
  int vol = APP_DEFAULT_VOL;
  int note = APP_DEFAULT_NOTE;
  int instrument = 0;
  int cache_kib = PCM_SAMP_DEFAULT_CACHE / 1024;
  bool demo = false;
  bool midi = false;
  bool test = false;
  bool demo_started = false;
  int ret;
  int i;
  pthread_t tid;
  pthread_t demo_tid;
  struct guitar_demo_s demo_state;

  if (argc < 3 || bank == NULL)
    {
      printf("wtsynth: -pcm requires a DBNK v2 path\n");
      return 1;
    }

  for (i = 3; i < argc; i++)
    {
      if (strcmp(argv[i], "-demo") == 0)
        {
          demo = true;
        }
      else if (strcmp(argv[i], "-midi") == 0)
        {
          midi = true;
        }
      else if (strcmp(argv[i], "-test") == 0)
        {
          test = true;
        }
      else if (i + 1 < argc && strcmp(argv[i], "-d") == 0)
        {
          dev = argv[++i];
        }
      else if (i + 1 < argc && strcmp(argv[i], "-r") == 0)
        {
          rate = atoi(argv[++i]);
        }
      else if (i + 1 < argc && strcmp(argv[i], "-t") == 0)
        {
          dur_ms = atoi(argv[++i]);
        }
      else if (i + 1 < argc && strcmp(argv[i], "-v") == 0)
        {
          vol = atoi(argv[++i]);
        }
      else if (i + 1 < argc && strcmp(argv[i], "-n") == 0)
        {
          note = atoi(argv[++i]);
        }
      else if (i + 1 < argc && strcmp(argv[i], "-i") == 0)
        {
          instrument = atoi(argv[++i]);
        }
      else if (i + 1 < argc && strcmp(argv[i], "-cache") == 0)
        {
          cache_kib = atoi(argv[++i]);
        }
      else
        {
          printf("wtsynth: unknown PCM option %s\n", argv[i]);
          return 1;
        }
    }

  if (rate <= 0)
    {
      rate = APP_FS_DEFAULT;
    }
  if (cache_kib < PCM_SAMP_MIN_CACHE / 1024)
    {
      cache_kib = PCM_SAMP_MIN_CACHE / 1024;
    }

  syslog(LOG_NOTICE,
         "wtsynth: pcm args voice_buf=%ux%uKiB demo=%d t=%d v=%d "
         "(cache_kib=%d ignored)\n",
         PCM_SAMP_MAX_VOICES, (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u),
         (int)demo, dur_ms, vol, cache_kib);

  memset(&g_app, 0, sizeof(g_app));
  g_app.running = true;
  g_app.pcm_mode = true;

#ifndef CONFIG_AUDIO_RPMSG_CLIENT
  /* Match mcu_audio.h MIC_SAMPLERATE (board sets 44100). */
  rate = APP_FS_DEFAULT;
#endif

  ret = pcm_samp_open(&g_pcm, bank, (uint32_t)rate,
                      (size_t)cache_kib * 1024u);
  if (ret < 0)
    {
      printf("wtsynth: DBNK open failed %d: %s\n", ret, bank);
      syslog(LOG_ERR, "wtsynth: DBNK open failed %d: %s\n", ret, bank);
      return 2;
    }

  /* Bus is dry by default; enable XMOS FX (e.g. CC91 reverb) as needed. */
  syslog(LOG_NOTICE,
         "wtsynth: bus dry (legacy EQ/Body/Comp/Room/Gate removed); "
         "XMOS FX + soft_bus16; CC91 = XMOS reverb\n");

  if (test)
    {
      ret = pcm_samp_selftest(&g_pcm, dur_ms > 0 ? dur_ms : 1200);
      pcm_samp_close(&g_pcm);
      printf("wtsynth: PCM selftest %s\n", ret == 0 ? "PASS" : "FAIL");
      syslog(LOG_NOTICE, "wtsynth: PCM selftest %s\n",
             ret == 0 ? "PASS" : "FAIL");
      return ret == 0 ? 0 : 3;
    }

  if (!demo && !midi)
    {
      ret = pcm_samp_note_on(&g_pcm, instrument, note, 110);
      if (ret < 0)
        {
          printf("wtsynth: no DBNK region for inst=%d note=%d\n",
                 instrument, note);
          syslog(LOG_ERR, "wtsynth: no DBNK region for inst=%d note=%d\n",
                 instrument, note);
          pcm_samp_close(&g_pcm);
          return 3;
        }
    }

  memset(&demo_state, 0, sizeof(demo_state));
  demo_state.eng = &g_pcm;
  demo_state.limit_ms = dur_ms > 0 ? (unsigned)dur_ms : 0;
  demo_state.running = demo;

  syslog(LOG_NOTICE,
         "wtsynth: PCM play start bank=%s mode=%s rate=%d "
         "dur=%dms vol=%d\n",
         bank, midi ? "usb-midi" : (demo ? "guitar-demo" : "note"),
         rate, dur_ms, vol);

  /* Let worker prime initial pages before the first real-time callback. */
  usleep(30000);

#ifndef CONFIG_AUDIO_RPMSG_CLIENT
  {
    extern uint32_t nuttx_med_open(FAR struct audio_caps_s *caps,
                                  void *callback, bool is_alsa_api);
    extern int nuttx_med_start(uint32_t h);
    extern void transq_msg_poll_rx(void);
    struct audio_caps_s caps;
    uint32_t h;
    volatile uint32_t spin;
    uint32_t rx_i;
    uint32_t rx_loops;

    memset(&caps, 0, sizeof(caps));
    caps.ac_len = sizeof(caps);
    caps.ac_type = AUDIO_TYPE_OUTPUT;
    caps.ac_channels = 1;
    caps.ac_controls.hw[0] = (uint16_t)rate;
    caps.ac_controls.b[2] = APP_BPS;
    h = nuttx_med_open(&caps, (void *)wt_transq_playback_cb, false);
    if (h == 0)
      {
        pcm_samp_close(&g_pcm);
        return 4;
      }
    pcm_samp_arm_start_fade(&g_pcm);
    nuttx_med_start(h);
    if (midi)
      {
        ret = midi_rpmsg_receiver_start(&g_pcm);
        if (ret != 0)
          {
            syslog(LOG_ERR, "usb_midi: receiver start failed %d\n", ret);
          }
      }
    if (demo)
      {
        ret = pthread_create(&demo_tid, NULL, guitar_demo_thread,
                             &demo_state);
        if (ret == 0)
          {
            demo_started = true;
          }
        else
          {
            syslog(LOG_ERR, "guitar_demo: pthread_create failed %d\n", ret);
          }
      }
    rx_loops = (uint32_t)(dur_ms > 0 ? dur_ms : 10000) * 200u;
    if (rx_loops < 100000u)
      {
        rx_loops = 100000u;
      }
    if (rx_loops > 2000000u)
      {
        rx_loops = 2000000u;
      }
    for (rx_i = 0; rx_i < rx_loops; rx_i++)
      {
        transq_msg_poll_rx();
        for (spin = 0; spin < 200u; spin++)
          {
          }
      }
    if (demo_started)
      {
        demo_state.running = false;
        pthread_join(demo_tid, NULL);
      }
  }
#else
  ret = wait_audio_device(dev, 20000);
  if (ret < 0)
    {
      syslog(LOG_ERR, "wtsynth: audio device %s not ready (%d)\n",
             dev, ret);
      pcm_samp_close(&g_pcm);
      return 4;
    }
  ret = init_nxaudio_devname(&g_app.nxaudio, rate, APP_BPS, APP_CHNUM,
                             dev, "/tmp/wtsynth_pcm_mq");
  if (ret < 0)
    {
      syslog(LOG_ERR, "wtsynth: nxaudio init %s failed %d\n",
             dev, ret);
      pcm_samp_close(&g_pcm);
      return 5;
    }
  /* Open at digital mute, prime with faded silence, then raise codec gain
   * after the path has settled — avoids the boot unmute pop.
   */
  nxaudio_setvolume(&g_app.nxaudio, 0);
  pcm_samp_arm_start_fade(&g_pcm);
  for (i = 0; i < g_app.nxaudio.abufnum; i++)
    {
      app_dequeue_cb((unsigned long)&g_app, g_app.nxaudio.abufs[i]);
    }
  tid = create_audio_thread(&g_app);
  usleep(80000);
  nxaudio_setvolume(&g_app.nxaudio, (uint16_t)vol);
  if (midi)
    {
      syslog(LOG_NOTICE, "wtsynth: startup sound skipped\n");
    }
  if (midi)
    {
      ret = midi_rpmsg_receiver_start(&g_pcm);
      if (ret != 0)
        {
          syslog(LOG_ERR, "usb_midi: receiver start failed %d\n", ret);
          g_app.running = false;
          nxaudio_stop(&g_app.nxaudio);
          pthread_join(tid, NULL);
          fin_nxaudio(&g_app.nxaudio);
          pcm_samp_close(&g_pcm);
          return 6;
        }
    }
  if (demo)
    {
      ret = pthread_create(&demo_tid, NULL, guitar_demo_thread, &demo_state);
      if (ret != 0)
        {
          syslog(LOG_ERR, "guitar_demo: pthread_create failed %d\n", ret);
          g_app.running = false;
          nxaudio_stop(&g_app.nxaudio);
          pthread_join(tid, NULL);
          fin_nxaudio(&g_app.nxaudio);
          pcm_samp_close(&g_pcm);
          return 6;
        }
      demo_started = true;
    }
  printf("wtsynth: PCM DBNK %s, %s, voice_buf=%ux%uKiB, rate=%d\n",
         bank, midi ? "USB MIDI" : (demo ? "guitar demo" : "single note"),
         PCM_SAMP_MAX_VOICES, (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u),
         rate);
  syslog(LOG_NOTICE,
         "wtsynth: PCM audio on %s, %s, voice_buf=%ux%uKiB, rate=%d\n",
         dev, midi ? "USB MIDI" : (demo ? "guitar demo" : "single note"),
         PCM_SAMP_MAX_VOICES, (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u),
         rate);
  if (demo_started)
    {
      pthread_join(demo_tid, NULL);
      demo_started = false;
    }
  else if (dur_ms == 0)
    {
      pthread_join(tid, NULL);
    }
  else
    {
      usleep((useconds_t)dur_ms * 1000u);
    }
  if (midi)
    {
      midi_rpmsg_receiver_stop();
      pcm_samp_all_off(&g_pcm);
    }
  else if (!demo)
    {
      pcm_samp_note_off(&g_pcm, instrument, note);
    }
  else
    {
      pcm_samp_all_off(&g_pcm);
    }
  usleep(250000);
  g_app.running = false;
  nxaudio_stop(&g_app.nxaudio);
  pthread_join(tid, NULL);
  fin_nxaudio(&g_app.nxaudio);
#endif

  pcm_samp_stats(&g_pcm, "play");
  pcm_samp_close(&g_pcm);
  printf("wtsynth: PCM done\n");
  syslog(LOG_NOTICE, "wtsynth: PCM done\n");
  return 0;
}

/****************************************************************************
 * TF random-read throughput: wtsynth -sdbench <file> [iters]
 ****************************************************************************/

static uint64_t mono_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static uint32_t xorshift32(uint32_t *s)
{
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x ? x : 0xA5A5A5A5u;
  return *s;
}

static int run_sdbench_file(FAR const char *path, unsigned iters)
{
  static const unsigned k_sizes[] =
    {
      4u * 1024u, 8u * 1024u, 16u * 1024u, 32u * 1024u
    };
  FILE *fp;
  struct stat st;
  uint8_t *buf = NULL;
  unsigned si;
  uint32_t rng = 0xC0FFEEu;
  size_t max_bs = 32u * 1024u;

  if (!path || path[0] == '\0')
    {
      path = "/sdcard/banks/guitar_pick_sus.dbnk";
    }
  if (iters == 0)
    {
      iters = 200;
    }

  syslog(LOG_NOTICE, "sdbench: enter path=%s iters=%u\n", path, iters);
  printf("sdbench: enter path=%s iters=%u\n", path, iters);

  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < (off_t)max_bs)
    {
      printf("sdbench: bad file %s (errno=%d size=%ld)\n",
             path, errno, (long)st.st_size);
      syslog(LOG_ERR, "sdbench: bad file %s errno=%d size=%ld\n",
             path, errno, (long)st.st_size);
      return 2;
    }

  fp = fopen(path, "rb");
  if (!fp)
    {
      printf("sdbench: fopen failed %s errno=%d\n", path, errno);
      syslog(LOG_ERR, "sdbench: fopen failed %s errno=%d\n", path, errno);
      return 2;
    }

  buf = (uint8_t *)malloc(max_bs);
  if (!buf)
    {
      fclose(fp);
      return 3;
    }

  printf("sdbench: file=%s size=%ld iters=%u (random seek+read)\n",
         path, (long)st.st_size, iters);
  syslog(LOG_NOTICE,
         "sdbench: file=%s size=%ld iters=%u\n",
         path, (long)st.st_size, iters);

  {
    size_t got = fread(buf, 1, max_bs, fp);
    (void)got;
  }

  for (si = 0; si < sizeof(k_sizes) / sizeof(k_sizes[0]); si++)
    {
      unsigned bs = k_sizes[si];
      unsigned i;
      unsigned ok = 0;
      uint64_t t0;
      uint64_t t1;
      uint64_t bytes = 0;
      double sec;
      double mib_s;
      double us_op;
      uint64_t span = (uint64_t)st.st_size - (uint64_t)bs;
      uint64_t nslot = (span / (uint64_t)bs) + 1ull;

      t0 = mono_us();
      for (i = 0; i < iters; i++)
        {
          uint64_t slot = (uint64_t)(xorshift32(&rng) % (uint32_t)nslot);
          off_t off = (off_t)(slot * (uint64_t)bs);
          size_t got;

          if (fseek(fp, (long)off, SEEK_SET) != 0)
            {
              continue;
            }
          got = fread(buf, 1, bs, fp);
          if (got == bs)
            {
              ok++;
              bytes += got;
            }
          else
            {
              clearerr(fp);
            }
        }
      t1 = mono_us();

      sec = (double)(t1 - t0) / 1.0e6;
      mib_s = sec > 0.0 ? ((double)bytes / (1024.0 * 1024.0)) / sec : 0.0;
      us_op = ok ? (double)(t1 - t0) / (double)ok : 0.0;

      printf("sdbench: bs=%uKiB ok=%u/%u bytes=%llu time=%.3fs "
             "%.2f MiB/s  %.1f us/op\n",
             bs / 1024u, ok, iters, (unsigned long long)bytes, sec, mib_s,
             us_op);
      syslog(LOG_NOTICE,
             "sdbench: bs=%uKiB ok=%u/%u bytes=%llu time=%.3fs "
             "%.2f MiB/s %.1f us/op\n",
             bs / 1024u, ok, iters, (unsigned long long)bytes, sec, mib_s,
             us_op);
    }

  free(buf);
  fclose(fp);
  printf("sdbench: done\n");
  syslog(LOG_NOTICE, "sdbench: done\n");
  return 0;
}

/* Raw SD partition random reads via /dev/sdcard0 (no FatFs). */
static int run_rawbench(unsigned iters)
{
  static const unsigned k_sizes[] =
    {
      4u * 1024u, 8u * 1024u, 16u * 1024u, 32u * 1024u
    };
  static const char *k_devs[] =
    {
      "/dev/sdcard0", "/dev/mmcsd0"
    };
  FAR const char *devpath = NULL;
  struct geometry geo;
  uint8_t *buf = NULL;
  unsigned si;
  unsigned di;
  uint32_t rng = 0xBADC0FFEu;
  size_t max_bs = 32u * 1024u;
  int fd = -1;
  int ret;
  bool remounted = false;

  if (iters == 0)
    {
      iters = 200;
    }

  syslog(LOG_NOTICE, "rawbench: enter iters=%u\n", iters);
  printf("rawbench: enter iters=%u\n", iters);

  /* Fat mount holds the partition; umount so we can open the block device. */
  if (umount("/sdcard") != 0)
    {
      syslog(LOG_WARNING, "rawbench: umount /sdcard failed errno=%d (continue)\n",
             errno);
    }

  memset(&geo, 0, sizeof(geo));
  for (di = 0; di < sizeof(k_devs) / sizeof(k_devs[0]); di++)
    {
      fd = open(k_devs[di], O_RDONLY);
      if (fd < 0)
        {
          continue;
        }
      ret = ioctl(fd, BIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
      if (ret >= 0 && geo.geo_available && geo.geo_nsectors > 0 &&
          geo.geo_sectorsize >= 512)
        {
          devpath = k_devs[di];
          break;
        }
      close(fd);
      fd = -1;
    }

  if (fd < 0 || !devpath)
    {
      syslog(LOG_ERR, "rawbench: no block device (errno=%d)\n", errno);
      printf("rawbench: no block device\n");
      mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
      return 2;
    }

  buf = (uint8_t *)malloc(max_bs);
  if (!buf)
    {
      close(fd);
      mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
      return 3;
    }

  syslog(LOG_NOTICE,
         "rawbench: dev=%s sectors=%lu sect=%lu (%lu MiB) iters=%u\n",
         devpath, (unsigned long)geo.geo_nsectors,
         (unsigned long)geo.geo_sectorsize,
         (unsigned long)((geo.geo_nsectors / 1024u) *
                         (geo.geo_sectorsize / 1024u)),
         iters);
  printf("rawbench: dev=%s sectors=%lu sect=%lu iters=%u\n",
         devpath, (unsigned long)geo.geo_nsectors,
         (unsigned long)geo.geo_sectorsize, iters);

  /* Warm one aligned read at LBA 0. */
  {
    ssize_t n;
    lseek(fd, 0, SEEK_SET);
    n = read(fd, buf, (size_t)geo.geo_sectorsize);
    (void)n;
  }

  for (si = 0; si < sizeof(k_sizes) / sizeof(k_sizes[0]); si++)
    {
      unsigned bs = k_sizes[si];
      unsigned nsec = bs / geo.geo_sectorsize;
      unsigned i;
      unsigned ok = 0;
      uint64_t t0;
      uint64_t t1;
      uint64_t bytes = 0;
      double sec;
      double mib_s;
      double us_op;
      uint64_t max_start;

      if (nsec == 0 || geo.geo_nsectors <= nsec)
        {
          continue;
        }
      max_start = (uint64_t)geo.geo_nsectors - (uint64_t)nsec;

      t0 = mono_us();
      for (i = 0; i < iters; i++)
        {
          uint64_t r = ((uint64_t)xorshift32(&rng) << 32) | xorshift32(&rng);
          uint64_t start = r % (max_start + 1ull);
          off_t off = (off_t)(start * (uint64_t)geo.geo_sectorsize);
          ssize_t n;

          if (lseek(fd, off, SEEK_SET) < 0)
            {
              continue;
            }
          n = read(fd, buf, bs);
          if (n == (ssize_t)bs)
            {
              ok++;
              bytes += (uint64_t)n;
            }
        }
      t1 = mono_us();

      sec = (double)(t1 - t0) / 1.0e6;
      mib_s = sec > 0.0 ? ((double)bytes / (1024.0 * 1024.0)) / sec : 0.0;
      us_op = ok ? (double)(t1 - t0) / (double)ok : 0.0;

      printf("rawbench: bs=%uKiB ok=%u/%u bytes=%llu time=%.3fs "
             "%.2f MiB/s  %.1f us/op\n",
             bs / 1024u, ok, iters, (unsigned long long)bytes, sec, mib_s,
             us_op);
      syslog(LOG_NOTICE,
             "rawbench: bs=%uKiB ok=%u/%u bytes=%llu time=%.3fs "
             "%.2f MiB/s %.1f us/op\n",
             bs / 1024u, ok, iters, (unsigned long long)bytes, sec, mib_s,
             us_op);
    }

  free(buf);
  close(fd);

  if (mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL) == 0)
    {
      remounted = true;
    }
  syslog(LOG_NOTICE, "rawbench: done remount=%d\n", (int)remounted);
  printf("rawbench: done remount=%d\n", (int)remounted);
  return remounted ? 0 : 4;
}

static int run_sdbench(int argc, FAR char *argv[])
{
  FAR const char *path = "/sdcard/banks/guitar_pick_sus.dbnk";
  unsigned iters = 200;

  if (argc >= 3 && argv[2][0] != '\0')
    {
      path = argv[2];
    }
  if (argc >= 4)
    {
      int v = atoi(argv[3]);
      if (v > 0)
        {
          iters = (unsigned)v;
        }
    }
  return run_sdbench_file(path, iters);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{

  if (argc >= 2 && strcmp(argv[1], "-pcm") == 0)
    {
      return run_pcm_mode(argc, argv);
    }

  if (argc >= 2 && strcmp(argv[1], "-sdbench") == 0)
    {
      return run_sdbench(argc, argv);
    }

  /* Single A-core LC3 decode @ 124 kbps / 10 ms / 48 kHz. */
  if (argc >= 2 && strcmp(argv[1], "-lc3bench") == 0)
    {
      return lc3_bench_run();
    }

  if (argc >= 2 && strcmp(argv[1], "-rawbench") == 0)
    {
      unsigned iters = 200;
      if (argc >= 3)
        {
          int v = atoi(argv[2]);
          if (v > 0)
            {
              iters = (unsigned)v;
            }
        }
      return run_rawbench(iters);
    }

  /* Offline CPU load: wtsynth -bench [voices] [seconds] */
  if (argc >= 2 && strcmp(argv[1], "-bench") == 0)
    {
      static struct pcm64_engine_s eng;
      int voices = PCM64_MAX_VOICES;
      float seconds = 2.0f;
      double cpu = 0.0;
      uint64_t rus = 0, aus = 0;
      uint32_t sr = APP_FS_DEFAULT;

      if (argc >= 3)
        {
          voices = atoi(argv[2]);
        }
      if (argc >= 4)
        {
          seconds = (float)atof(argv[3]);
        }
      if (argc >= 5)
        {
          sr = (uint32_t)atoi(argv[4]);
        }

      /* pin A7 @ 780M before CPU load probe */
      hal_cmu_dsp_set_freq(PCM64_CMU_FREQ_780M);
      {
        uint32_t hz = hal_sys_timer_calc_cpu_freq(5, 0);
        int cf = hal_cmu_dsp_get_freq();
        syslog(LOG_NOTICE, "pcm64: force 780M, calc=%lu Hz cmu_freq=%d\n",
               (unsigned long)hz, (int)cf);
        printf("pcm64: force 780M, calc=%lu Hz cmu_freq=%d\n",
               (unsigned long)hz, (int)cf);
      }
      syslog(LOG_NOTICE, "pcm64: build %d frags x %d samples in RAM, bench %d voices\n",
             PCM64_NUM_FRAGS, PCM64_FRAG_SAMPLES, voices);
      printf("pcm64: build %d frags x %d samples in RAM, bench %d voices\n",
             PCM64_NUM_FRAGS, PCM64_FRAG_SAMPLES, voices);
      pcm64_engine_init(&eng, sr);
      pcm64_engine_bench(&eng, voices, seconds, &cpu, &rus, &aus);
      syslog(LOG_NOTICE, "RESULT cpu_loading=%.2f%% voices=%d sr=%lu render_us=%llu audio_us=%llu\n",
             cpu, voices, (unsigned long)sr,
             (unsigned long long)rus, (unsigned long long)aus);
      printf("RESULT cpu_loading=%.2f%% voices=%d sr=%lu\n",
             cpu, voices, (unsigned long)sr);
      return (cpu > 95.0) ? 1 : 0;
    }


  FAR const char *wave = NULL;
  FAR const char *dev = APP_DEFAULT_DEV;
  int rate = APP_FS_DEFAULT;
  int note = APP_DEFAULT_NOTE;
  int freq_hz = 0;
  int dur_ms = APP_DEFAULT_MS;
  int vol = APP_DEFAULT_VOL;
  int opt;
  int ret;
  int i;
  pthread_t tid;

  while ((opt = getopt(argc, argv, "w:d:r:n:f:t:v:h")) != ERROR)
    {
      switch (opt)
        {
          case 'w':
            wave = optarg;
            break;
          case 'd':
            dev = optarg;
            break;
          case 'r':
            rate = atoi(optarg);
            break;
          case 'n':
            note = atoi(optarg);
            break;
          case 'f':
            freq_hz = atoi(optarg);
            break;
          case 't':
            dur_ms = atoi(optarg);
            break;
          case 'v':
            vol = atoi(optarg);
            break;
          case 'h':
          default:
            show_usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

  if (rate <= 0)
    {
      rate = APP_FS_DEFAULT;
    }

  memset(&g_app, 0, sizeof(g_app));
  g_app.running = true;
  wt_engine_init(&g_app.engine, (uint32_t)rate);

  /* Product bring-up: skip TF/stat — use builtin sine. */
  wt_engine_use_builtin_sine(&g_app.engine);
  UNUSED(wave);
  UNUSED(path_exists);

  syslog(LOG_NOTICE, "wtsynth: sine/table play start note=%d freq=%d "
         "dur=%dms vol=%d\n",
         note, freq_hz, dur_ms, vol);

#ifndef CONFIG_AUDIO_RPMSG_CLIENT
  /* Product AP: play via TRANSQ → MCU codec (nuttx_media_api_a7). */
  {
    extern uint32_t nuttx_med_open(FAR struct audio_caps_s *caps,
                                  void *callback, bool is_alsa_api);
    extern int nuttx_med_start(uint32_t h);
    extern void transq_msg_poll_rx(void);
    struct audio_caps_s caps;
    uint32_t h;
    volatile uint32_t spin;
    uint32_t rx_i;
    uint32_t rx_loops;

    /* Match mcu_audio.h MIC_SAMPLERATE / PLAYBACK_SIZE (44100 mono). */
    rate = APP_FS_DEFAULT;
    wt_engine_fini(&g_app.engine);
    wt_engine_init(&g_app.engine, (uint32_t)rate);
    wt_engine_use_builtin_sine(&g_app.engine);

    memset(&caps, 0, sizeof(caps));
    caps.ac_len = sizeof(caps);
    caps.ac_type = AUDIO_TYPE_OUTPUT;
    caps.ac_channels = 1;
    caps.ac_controls.hw[0] = (uint16_t)rate;
    caps.ac_controls.b[2] = APP_BPS;

    if (freq_hz > 0)
      {
        wt_engine_tone_on(&g_app.engine, (uint32_t)freq_hz, 900,
                          (uint32_t)dur_ms);
      }
    else
      {
        wt_engine_note_on(&g_app.engine, note, 900, (uint32_t)dur_ms);
      }

    g_app.running = true;
    h = nuttx_med_open(&caps, (void *)wt_transq_playback_cb, false);
    if (h == 0)
      {
        syslog(LOG_ERR, "wtsynth: nuttx_med_open failed\n");
        wt_engine_fini(&g_app.engine);
        return 3;
      }

    ret = nuttx_med_start(h);
    (void)ret;
    syslog(LOG_NOTICE, "wtsynth: TRANSQ playback running\n");

    /* No GIC: pump TRANSQ RX for AF_PLAYBACK_IRQ buffer fills. */
    rx_loops = (uint32_t)dur_ms * 200u;
    if (rx_loops < 100000u)
      {
        rx_loops = 100000u;
      }
    if (rx_loops > 2000000u)
      {
        rx_loops = 2000000u;
      }

    for (rx_i = 0; rx_i < rx_loops; rx_i++)
      {
        transq_msg_poll_rx();
        for (spin = 0; spin < 200u; spin++)
          {
          }
      }

    wt_engine_fini(&g_app.engine);
    printf("wtsynth: done\n");
    syslog(LOG_NOTICE, "wtsynth: sine/table done\n");
    return 0;
  }
#else
  /* Wait for AP to bind pcm0p before open/ioctl. */
  ret = wait_audio_device(dev, 20000);
  if (ret < 0)
    {
      syslog(LOG_ERR, "wtsynth: audio device %s not ready (%d)\n",
             dev, ret);
      wt_engine_fini(&g_app.engine);
      return 2;
    }
  syslog(LOG_NOTICE, "wtsynth: audio device %s ready\n", dev);

  ret = init_nxaudio_devname(&g_app.nxaudio, rate, APP_BPS, APP_CHNUM,
                             dev, "/tmp/wtsynth_mq");
  if (ret < 0)
    {
      syslog(LOG_ERR, "wtsynth: nxaudio init %s failed %d\n",
             dev, ret);
      wt_engine_fini(&g_app.engine);
      return 1;
    }

  nxaudio_setvolume(&g_app.nxaudio, (uint16_t)vol);
  if (freq_hz > 0)
    {
      wt_engine_tone_on(&g_app.engine, (uint32_t)freq_hz, 900,
                        (uint32_t)dur_ms);
    }
  else
    {
      wt_engine_note_on(&g_app.engine, note, 900, (uint32_t)dur_ms);
    }

  /* Prime buffers */

  for (i = 0; i < g_app.nxaudio.abufnum; i++)
    {
      app_dequeue_cb((unsigned long)&g_app, g_app.nxaudio.abufs[i]);
    }

  tid = create_audio_thread(&g_app);

  if (freq_hz > 0)
    {
      if (dur_ms == 0)
        {
          printf("wtsynth: playing %d Hz continuously on %s\n",
                 freq_hz, dev);
        }
      else
        {
          printf("wtsynth: playing %d Hz for %d ms on %s\n",
                 freq_hz, dur_ms, dev);
        }
      syslog(LOG_NOTICE, "wtsynth: playing %d Hz for %d ms on %s\n",
             freq_hz, dur_ms, dev);
    }
  else
    {
      if (dur_ms == 0)
        {
          printf("wtsynth: playing MIDI %d continuously on %s\n",
                 note, dev);
        }
      else
        {
          printf("wtsynth: playing MIDI %d for %d ms on %s\n",
                 note, dur_ms, dev);
        }
      syslog(LOG_NOTICE, "wtsynth: playing MIDI %d for %d ms on %s\n",
             note, dur_ms, dev);
    }

  if (dur_ms == 0)
    {
      /* Sustain forever until process is killed. */
      pthread_join(tid, NULL);
    }
  else
    {
      usleep((useconds_t)dur_ms * 1000 + 200000);

      g_app.running = false;
      nxaudio_stop(&g_app.nxaudio);
      pthread_join(tid, NULL);
    }

  fin_nxaudio(&g_app.nxaudio);
  wt_engine_fini(&g_app.engine);
  printf("wtsynth: done\n");
  syslog(LOG_NOTICE, "wtsynth: sine/table done\n");
  return 0;
#endif
}
