#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <nuttx/rpmsg/rpmsg.h>

#include "midi_rpmsg.h"
#include "midi_rpmsg_receiver.h"
#include "pcm_sample_engine.h"
#include "xmos_fx_chain.h"
#include "xmos_fx/xmos_lut.h"

/* MIDI CC map for DAILE guitar bus:
 *   MIDI ch1..6 = guitar string 1..6 (high E .. low E → bank inst 0..5)
 *   Other channels keep auto string pick by nearest sample root.
 *   CC91  XMOS reverb       0=off, 1..127=on + mix
 *   CC102 XMOS all FX       0..63=all off, 64..127=all 22 on (stress)
 *   CC103 XMOS single FX    0..21=select one, 127=all off (A7 benchmark)
 *   CC104 TS808 curve A/B   0..63=libm expf, 64..127=LUT (default LUT)
 *   CC27  sample start      0..127 → authored start + 0..55 ms
 *   CC28  open-string res   0..127 → amount 0..100 (0=off)
 *   CC29  XDI filter/LFO    0..63=off, 64..127=on (new voices)
 *   CC70  Env1 Attack       0..127 (64=authored; <64 slower)
 *   CC71  Env2 Attack       0..127 (needs CC29 on)
 *   CC72  Env3 Attack       0..127 (needs CC29 on)
 *   CC73  Velocity→Attack   0..127 → depth 0..100
 *   CC74  Velocity→S.POINT  0..127 → depth 0..100
 *   CC75  hammer/pull legato 0..63=off, 64..127=on (default on)
 *   CC7   master bus trim   0..127 (100≈default firmware master)
 * Legacy CC20-26 (EQ/Body/Comp/flat) removed with bus FX path.
 */
#define MIDI_CC_VOLUME    7u
#define MIDI_CC_SPOINT    27u
#define MIDI_CC_GT_RES    28u
#define MIDI_CC_XDI_MOD   29u
#define MIDI_CC_ENV1_ATK  70u
#define MIDI_CC_ENV2_ATK  71u
#define MIDI_CC_ENV3_ATK  72u
#define MIDI_CC_VEL_ATK   73u
#define MIDI_CC_VEL_SPOINT 74u
#define MIDI_CC_LEGATO    75u
#define MIDI_CC_REVERB    91u
#define MIDI_CC_XMOS_ALL  102u
#define MIDI_CC_XMOS_ONE  103u
#define MIDI_CC_TS808_LUT 104u

/* Hammer/pull: overlapping held notes within a few frets.  Auto string
 * routing must NOT run first — otherwise 64→67 can jump instruments and
 * become a fresh pluck.  Lock the sounding string, then legato.
 */
#define MIDI_LEGATO_MAX_MS     900u
#define MIDI_LEGATO_MAX_SEMIS  5

struct midi_receiver_s
{
  struct rpmsg_endpoint ept;
  struct pcm_samp_engine_s *eng;
  bool registered;
  bool endpoint_created;
  uint32_t events;
  /* Per MIDI note key-down state for same-string legato (hammer/pull). */
  uint8_t note_held[128];
  /* Last Note On per string/instrument for time-boxed legato. */
  int16_t last_note[PCM_SAMP_MAX_VOICES];
  uint32_t last_on_ms[PCM_SAMP_MAX_VOICES];
  bool legato_enable; /* CC75; default on */
};

static struct midi_receiver_s g_midi_rx;

static uint32_t midi_now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void midi_clear_held(struct midi_receiver_s *rx)
{
  int i;
  if (!rx)
    {
      return;
    }
  memset(rx->note_held, 0, sizeof(rx->note_held));
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      rx->last_note[i] = -1;
      rx->last_on_ms[i] = 0;
    }
}

/* Find a held+active voice to hammer/pull from.  Writes the locked string
 * id into *out_instrument.  Prefers the string's last fretted note so
 * ringing chord tones do not steal the lead legato path.
 * prefer_inst >= 0 locks the search to that bank instrument (MIDI ch1-6).
 */
static int midi_find_legato(struct midi_receiver_s *rx, int new_note,
                            int prefer_inst, int *out_instrument)
{
  int best_from = -1;
  int best_inst = -1;
  int best_semis = 99;
  int best_is_last = -1;
  uint32_t best_age = UINT32_MAX;
  int i;
  uint32_t now;

  if (!rx || !rx->eng || new_note < 0 || new_note > 127)
    {
      return -1;
    }

  now = midi_now_ms();
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *v = &rx->eng->voices[i];
      int from;
      int semis;
      int inst;
      int is_last;
      uint32_t dt;

      if (!v->active || v->is_resonance)
        {
          continue;
        }
      from = v->note;
      if (from < 0 || from > 127 || from == new_note)
        {
          continue;
        }
      if (!rx->note_held[from])
        {
          continue;
        }

      semis = new_note - from;
      if (semis < 0)
        {
          semis = -semis;
        }
      if (semis < 1 || semis > MIDI_LEGATO_MAX_SEMIS)
        {
          continue;
        }

      inst = v->instrument_id;
      if (inst < 0 || inst >= PCM_SAMP_MAX_VOICES)
        {
          continue;
        }
      if (prefer_inst >= 0 && inst != prefer_inst)
        {
          continue;
        }

      dt = now - rx->last_on_ms[inst];
      if (dt > MIDI_LEGATO_MAX_MS)
        {
          continue;
        }

      is_last = (rx->last_note[inst] == from) ? 1 : 0;
      if (is_last > best_is_last ||
          (is_last == best_is_last && semis < best_semis) ||
          (is_last == best_is_last && semis == best_semis &&
           v->age < best_age))
        {
          best_is_last = is_last;
          best_semis = semis;
          best_age = v->age;
          best_from = from;
          best_inst = inst;
        }
    }

  if (best_from >= 0 && out_instrument)
    {
      *out_instrument = best_inst;
    }
  return best_from;
}

/* MIDI channel 1..6 → guitar string 1..6 → bank instrument 0..5.
 * Other channels keep legacy auto string pick.
 */
static int midi_channel_instrument(const struct pcm_samp_engine_s *eng,
                                   uint8_t status)
{
  uint8_t ch = status & 0x0fu;

  if (!eng || eng->header.instrument_count <= 0)
    {
      return -1;
    }
  if (ch >= 6u || ch >= eng->header.instrument_count)
    {
      return -1;
    }
  return (int)ch;
}

static void midi_handle_cc(struct pcm_samp_engine_s *eng,
                           uint8_t cc, uint8_t val)
{
  if (!eng)
    {
      return;
    }

  if (cc == MIDI_CC_REVERB)
    {
      /* Map CC91 onto XMOS Reverb mix (param 5). 0 = bypass. */
      bool on = val > 0u;
      uint8_t mix = (uint8_t)(((unsigned)val * 255u + 63u) / 127u);
      xmos_fx_enable(XMOS_FX_REVERB, on);
      if (on)
        {
          xmos_fx_set_param(XMOS_FX_REVERB, 5u, mix);
        }
      syslog(LOG_NOTICE, "usb_midi: CC91 xmos_reverb=%s mix=%u (cc=%u)\n",
             on ? "on" : "off", mix, val);
    }
  else if (cc == MIDI_CC_XMOS_ALL)
    {
      bool on = val >= 64u;
      xmos_fx_enable_all(on);
      syslog(LOG_NOTICE, "usb_midi: CC102 xmos_all=%s mask=0x%x\n",
             on ? "on" : "off", (unsigned)xmos_fx_enable_mask());
    }
  else if (cc == MIDI_CC_XMOS_ONE)
    {
      xmos_fx_enable_all(false);
      if (val < XMOS_FX_COUNT)
        {
          xmos_fx_enable((enum xmos_fx_id_e)val, true);
          syslog(LOG_NOTICE,
                 "usb_midi: CC103 xmos_one=%u mask=0x%x\n",
                 val, (unsigned)xmos_fx_enable_mask());
        }
      else
        {
          syslog(LOG_NOTICE, "usb_midi: CC103 xmos_one=off\n");
        }
    }
  else if (cc == MIDI_CC_TS808_LUT)
    {
      g_xmos_ts808_curve_lut = (val >= 64u) ? 1u : 0u;
      syslog(LOG_NOTICE, "usb_midi: CC104 ts808_curve=%s\n",
             g_xmos_ts808_curve_lut ? "lut" : "expf");
    }
  else if (cc == MIDI_CC_SPOINT)
    {
      unsigned milliseconds = ((unsigned)val * 55u + 63u) / 127u;
      pcm_samp_set_sample_start_ms(eng, milliseconds);
      syslog(LOG_NOTICE, "usb_midi: CC27 sample_start=%u ms (cc=%u)\n",
             milliseconds, val);
    }
  else if (cc == MIDI_CC_GT_RES)
    {
      int amount = ((int)val * 100 + 63) / 127;
      pcm_samp_set_gt_resonance(eng, amount);
      syslog(LOG_NOTICE, "usb_midi: CC28 gt_res=%d (cc=%u)\n", amount, val);
    }
  else if (cc == MIDI_CC_XDI_MOD)
    {
      bool enable = val >= 64u;
      pcm_samp_set_xdi_mod(eng, enable);
      syslog(LOG_NOTICE, "usb_midi: CC29 xdi_mod=%s (cc=%u)\n",
             enable ? "on" : "off", val);
    }
  else if (cc == MIDI_CC_ENV1_ATK)
    {
      pcm_samp_set_env_attack_cc(eng, 0, (int)val);
      syslog(LOG_NOTICE, "usb_midi: CC70 env1_atk=%u\n", val);
    }
  else if (cc == MIDI_CC_ENV2_ATK)
    {
      pcm_samp_set_env_attack_cc(eng, 1, (int)val);
      syslog(LOG_NOTICE, "usb_midi: CC71 env2_atk=%u\n", val);
    }
  else if (cc == MIDI_CC_ENV3_ATK)
    {
      pcm_samp_set_env_attack_cc(eng, 2, (int)val);
      syslog(LOG_NOTICE, "usb_midi: CC72 env3_atk=%u\n", val);
    }
  else if (cc == MIDI_CC_VEL_ATK)
    {
      int amt = ((int)val * 100) / 127;
      pcm_samp_set_vel_attack(eng, amt);
      syslog(LOG_NOTICE, "usb_midi: CC73 vel_atk=%d (cc=%u)\n", amt, val);
    }
  else if (cc == MIDI_CC_VEL_SPOINT)
    {
      int amt = ((int)val * 100) / 127;
      pcm_samp_set_vel_spoint(eng, amt);
      syslog(LOG_NOTICE, "usb_midi: CC74 vel_spoint=%d (cc=%u)\n", amt, val);
    }
  else if (cc == MIDI_CC_LEGATO)
    {
      g_midi_rx.legato_enable = val >= 64u;
      syslog(LOG_NOTICE, "usb_midi: CC75 legato=%s (cc=%u)\n",
             g_midi_rx.legato_enable ? "on" : "off", val);
    }
  else if (cc == MIDI_CC_VOLUME)
    {
      /* Let CC7 reach a strong solo level; the peak-following bus trim below
       * the voice mixer still protects dense chords and loud transients.
       */
      int master = ((int)val * 28000) / 127;
      if (master > 28000)
        {
          master = 28000;
        }
      eng->master_q15 = master;
      syslog(LOG_NOTICE, "usb_midi: CC7 master_q15=%d (cc=%u)\n",
             master, val);
    }
  else if (cc == 120u || cc == 123u)
    {
      midi_clear_held(&g_midi_rx);
      pcm_samp_all_off(eng);
    }
}

static int midi_ept_callback(struct rpmsg_endpoint *ept, void *data,
                             size_t len, uint32_t src, void *priv)
{
  struct midi_receiver_s *rx = priv;
  const struct midi_rpmsg_msg_s *msg = data;
  uint8_t command;
  int instrument = -1;

  (void)ept;
  (void)src;

  if (!rx || !rx->eng || len != sizeof(*msg) ||
      msg->magic != MIDI_RPMSG_MAGIC)
    {
      return -EINVAL;
    }

  if (msg->type == MIDI_RPMSG_ALL_OFF)
    {
      midi_clear_held(rx);
      pcm_samp_all_off(rx->eng);
      return 0;
    }

  if (msg->type != MIDI_RPMSG_EVENT)
    {
      return -EINVAL;
    }

  command = msg->status & 0xf0u;

  if (command == 0x90u && msg->data2 != 0)
    {
      int from_note;
      int channel_inst = midi_channel_instrument(rx->eng, msg->status);

      /* Hammer/pull first: lock the already-sounding string.  Only when
       * there is no overlapping fretted source do we re-pick by nearest
       * root sample (fresh pluck / fingerstyle).  MIDI ch1-6 force the
       * string (1=high E .. 6=low E).
       */
      from_note = -1;
      if (rx->legato_enable)
        {
          from_note = midi_find_legato(rx, msg->data1, channel_inst,
                                       &instrument);
        }
      if (from_note >= 0 && instrument >= 0)
        {
          pcm_samp_legato(rx->eng, instrument, from_note,
                          msg->data1, msg->data2);
          syslog(LOG_NOTICE,
                 "usb_midi: legato %d->%d inst=%d ch=%u\n",
                 from_note, msg->data1, instrument,
                 (unsigned)((msg->status & 0x0fu) + 1u));
        }
      else
        {
          instrument = channel_inst;
          if (instrument < 0)
            {
              instrument = pcm_samp_best_instrument(rx->eng, msg->data1,
                                                    msg->data2);
            }
          if (instrument >= 0)
            {
              pcm_samp_note_on(rx->eng, instrument, msg->data1,
                               msg->data2);
              /* Fresh pluck only: wake matching open strings (Gt Body). */
              pcm_samp_open_resonance(rx->eng, msg->data1, msg->data2);
            }
        }
      if (instrument >= 0)
        {
          if (msg->data1 < 128)
            {
              rx->note_held[msg->data1] = 1;
            }
          if (instrument < PCM_SAMP_MAX_VOICES)
            {
              rx->last_note[instrument] = (int16_t)msg->data1;
              rx->last_on_ms[instrument] = midi_now_ms();
            }
        }
    }
  else if (command == 0x80u ||
           (command == 0x90u && msg->data2 == 0))
    {
      int channel_inst = midi_channel_instrument(rx->eng, msg->status);

      if (msg->data1 < 128)
        {
          rx->note_held[msg->data1] = 0;
        }
      /* Release on the channel's string when ch1-6; else any instrument.
       * After legato A→B, voice->note is B, so NoteOff(A) is a no-op — correct.
       * data2 is Note-Off velocity (Dream KeyOffEnv/VolModulation).
       */
      pcm_samp_note_off_vel(rx->eng, channel_inst, msg->data1,
                            msg->data2 ? (int)msg->data2 : 64);
    }
  else if (command == 0xb0u)
    {
      midi_handle_cc(rx->eng, msg->data1, msg->data2);
    }
  else if (command == 0xe0u)
    {
      /* Pitch Bend: data1=LSB, data2=MSB → 14-bit 0..16383 (8192 center). */
      uint16_t bend14 =
        (uint16_t)(msg->data1 & 0x7fu) |
        ((uint16_t)(msg->data2 & 0x7fu) << 7);
      pcm_samp_set_pitch_bend(rx->eng, bend14);
    }

  rx->events++;
  if (rx->events <= 8u || (rx->events % 128u) == 0u)
    {
      syslog(LOG_NOTICE,
             "usb_midi: event=%lu st=%02x d1=%u d2=%u inst=%d\n",
             (unsigned long)rx->events, msg->status,
             msg->data1, msg->data2, instrument);
    }

  return 0;
}

static void midi_rpmsg_created(struct rpmsg_device *rdev, void *priv)
{
  struct midi_receiver_s *rx = priv;
  int ret;

  if (!rx || strcmp(rpmsg_get_cpuname(rdev), "ap") != 0)
    {
      return;
    }

  rx->ept.priv = rx;
  ret = rpmsg_create_ept(&rx->ept, rdev, MIDI_RPMSG_ENDPOINT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         midi_ept_callback, NULL);
  if (ret == 0)
    {
      rx->endpoint_created = true;
      syslog(LOG_NOTICE, "usb_midi: RPMSG receiver ready\n");
    }
  else
    {
      syslog(LOG_ERR, "usb_midi: RPMSG endpoint failed %d\n", ret);
    }
}

static void midi_rpmsg_destroyed(struct rpmsg_device *rdev, void *priv)
{
  struct midi_receiver_s *rx = priv;

  (void)rdev;
  if (rx && rx->endpoint_created)
    {
      rpmsg_destroy_ept(&rx->ept);
      rx->endpoint_created = false;
    }
}

int midi_rpmsg_receiver_start(struct pcm_samp_engine_s *eng)
{
  int ret;

  if (!eng)
    {
      return -EINVAL;
    }
  if (g_midi_rx.registered)
    {
      return -EALREADY;
    }

  memset(&g_midi_rx, 0, sizeof(g_midi_rx));
  g_midi_rx.eng = eng;
  g_midi_rx.legato_enable = true; /* CC75 default on */
  midi_clear_held(&g_midi_rx);
  ret = rpmsg_register_callback(&g_midi_rx, midi_rpmsg_created,
                                midi_rpmsg_destroyed, NULL, NULL);
  if (ret == 0)
    {
      g_midi_rx.registered = true;
    }
  return ret;
}

void midi_rpmsg_receiver_stop(void)
{
  if (!g_midi_rx.registered)
    {
      return;
    }

  rpmsg_unregister_callback(&g_midi_rx, midi_rpmsg_created,
                            midi_rpmsg_destroyed, NULL, NULL);
  if (g_midi_rx.endpoint_created)
    {
      rpmsg_destroy_ept(&g_midi_rx.ept);
    }
  memset(&g_midi_rx, 0, sizeof(g_midi_rx));
}
