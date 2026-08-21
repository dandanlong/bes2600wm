/****************************************************************************
 * BES2600 A7 DBNK v2 streaming PCM sampled-synth
 *
 * Per-voice 16 KiB ping-pong (2 x 8 KiB).  Attack heads stay in RAM;
 * sustain streams from TF via proactive half fills (not miss-driven LRU).
 ****************************************************************************/

#ifndef __APPS_WAVETABLE_SYNTH_PCM_SAMPLE_ENGINE_H
#define __APPS_WAVETABLE_SYNTH_PCM_SAMPLE_ENGINE_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>

#define PCM_SAMP_MAX_VOICES       64
#define PCM_SAMP_HALF_SIZE        (8 * 1024)
#define PCM_SAMP_VOICE_BYTES      (PCM_SAMP_HALF_SIZE * 2u) /* 16 KiB */
#define PCM_SAMP_VOICE_POOL       (PCM_SAMP_MAX_VOICES * PCM_SAMP_VOICE_BYTES)
/* Hard quality floors; the render-time controller can degrade earlier. */
#define PCM_SAMP_ADAPT_VOICES     24
#define PCM_SAMP_FAST_VOICES      52
#define PCM_SAMP_END_FADE         1024
#define PCM_SAMP_ATTACK_SAMPLES   256
/* Same-note retrigger: duck old voice, brief soft-attack on the new one. */
#define PCM_SAMP_RETRIG_KILL_MS   3
#define PCM_SAMP_RETRIG_ATTACK    12   /* ~0.27 ms @ 44.1 kHz */
#define PCM_SAMP_ALL_OFF_MS       12
#define PCM_SAMP_TAIL_DETACH_MS   8
#define PCM_SAMP_STEAL_QUIET      4000 /* env level: hard-replace OK */
/* Control-rate for LFO, filter/pitch coeffs (audio-rate IIR still per-sample). */
#define PCM_SAMP_CTRL_PERIOD      32
#define PCM_SAMP_ENV_SEGS         4
#define PCM_SAMP_REQ_COUNT        256
/* Attack head size = one stream half (8 KiB / 4096 frames ≈ 93 ms @ 44.1 kHz). */
#define PCM_SAMP_HEAD_BYTES       PCM_SAMP_HALF_SIZE
#define PCM_SAMP_HEAD_FRAMES      (PCM_SAMP_HEAD_BYTES / 2u)
/* Legacy ms label for logs/tools (~93 ms @ 44.1 kHz). */
#define PCM_SAMP_HEAD_MS          93
/* Safety-net watermark: re-kick sibling half if still empty this close to
 * the end of the current half. Primary path queues the next half as soon as
 * the previous half becomes READY (see voice_queue_next_half). */
#define PCM_SAMP_PREFETCH_REMAIN  (4 * 1024)
/* Short guitar-body impulse (~2.9 ms @ 44.1 kHz). Kept short to avoid mud. */
#define PCM_SAMP_BODY_IR_LEN      128
/* Acoustic legato dual-source crossfade upper bound (~23 ms @ 44.1 kHz). */
#define PCM_SAMP_LEGATO_XFADE_MAX 1024
#define PCM_SAMP_LEGATO_NONE      0u
#define PCM_SAMP_LEGATO_MAIN      1u /* new target sample */
#define PCM_SAMP_LEGATO_TAIL      2u /* old source still streaming */

/* Legacy aliases kept so callers that passed -cache still compile. */
#define PCM_SAMP_PAGE_SIZE        PCM_SAMP_HALF_SIZE
#define PCM_SAMP_DEFAULT_CACHE    PCM_SAMP_VOICE_POOL
#define PCM_SAMP_MIN_CACHE        PCM_SAMP_VOICE_BYTES

enum pcm_samp_env_phase_e
{
  PCM_ENV_IDLE = 0,
  PCM_ENV_RUN,
  PCM_ENV_SUSTAIN,
  PCM_ENV_RELEASE
};

enum pcm_samp_half_state_e
{
  PCM_HALF_EMPTY = 0,
  PCM_HALF_LOADING,
  PCM_HALF_READY
};

struct __attribute__((packed)) pcm_samp_env_seg_s
{
  int16_t rate_q15;
  int16_t level_q15;
};

struct __attribute__((packed)) pcm_samp_env_desc_s
{
  int8_t sustain_point;
  int8_t loop_start;
  int8_t vel_scaling;
  uint8_t seg_count;
  struct pcm_samp_env_seg_s segs[PCM_SAMP_ENV_SEGS];
};

struct __attribute__((packed)) pcm_samp_lfo_desc_s
{
  uint8_t waveform;
  uint8_t reserved;
  uint16_t freq_q8;
  uint16_t delay_ms;
  uint16_t attack_ms;
};

struct __attribute__((packed)) pcm_samp_kbd_point_s
{
  uint8_t key;
  int8_t value_q7;
};

struct __attribute__((packed)) pcm_samp_artic_s
{
  struct pcm_samp_env_desc_s env1;
  struct pcm_samp_env_desc_s env2;
  struct pcm_samp_env_desc_s env3;
  struct pcm_samp_lfo_desc_s lfo1;
  struct pcm_samp_lfo_desc_s lfo2;
  int16_t wave_env3_amount_q15;
  int16_t wave_lfo1_amount_q15;
  uint8_t filter_type;
  uint8_t filter_slope;
  uint8_t soft_clip;
  uint8_t reserved0;
  uint16_t filter_freq_q15;
  uint16_t filter_reso_q15;
  int16_t filter_env2_amount_q15;
  int16_t filter_lfo2_amount_q15;
  int16_t amp_volume_q8;
  int16_t amp_lfo2_amount_q15;
  int16_t amp_vel_offset;
  int16_t amp_vel_slope_q8;
  int16_t amp_vel_min_y_q8;
  int16_t amp_vel_max_y_q8;
  uint8_t amp_vel_curve;
  uint8_t amp_vel_min_x;
  uint8_t amp_vel_max_x;
  uint8_t reserved1;
  uint8_t amp_kbd_count; /* bit7: extended tail valid, low bits: point count */
  struct pcm_samp_kbd_point_s amp_kbd[4];
  uint8_t env1_kbd_count;
  struct pcm_samp_kbd_point_s env1_kbd[4];
  int8_t mixer_volume_q4; /* 1/16 dB */
  int8_t fx_send_q2;      /* 1/4 dB */
};

struct __attribute__((packed)) pcm_samp_header_s
{
  char magic[4];
  uint16_t version;
  uint16_t flags;
  uint32_t pcm_rate;
  uint16_t instrument_count;
  uint16_t region_count;
  uint16_t sample_count;
  uint16_t artic_count;
  uint32_t off_instruments;
  uint32_t off_regions;
  uint32_t off_samples;
  uint32_t off_artics;
  uint32_t off_pcm;
  uint32_t pcm_bytes;
  uint32_t page_size;
  uint8_t reserved[16];
};

struct __attribute__((packed)) pcm_samp_instrument_s
{
  uint8_t id;
  uint8_t midi_lo;
  uint8_t midi_hi;
  uint8_t flags;
  uint16_t region_begin;
  uint16_t region_count;
  char name[16];
};

struct __attribute__((packed)) pcm_samp_region_s
{
  uint8_t key_lo;
  uint8_t key_hi;
  uint8_t vel_lo;
  uint8_t vel_hi;
  uint8_t asv_group;
  uint8_t legato_start_offset; /* Dream Acoustic Legato offset, PCM frames */
  uint16_t sample_id;
  uint8_t unity_note;
  int8_t coarse_tune;
  int16_t fine_tune_cents;
  uint32_t play_start;
  uint16_t artic_id;
  uint16_t gain_q8;
};

struct __attribute__((packed)) pcm_samp_sample_s
{
  uint32_t pcm_offset;
  uint32_t frames;
  uint32_t loop_start;
  uint32_t loop_end;
  uint8_t loop_type;
  uint8_t reserved0;
  uint16_t reserved1;
  uint32_t sample_rate;
};

struct pcm_samp_env_s
{
  int32_t level;
  int32_t target;
  int32_t inc;
  uint8_t seg;
  uint8_t nsegs;
  int8_t sustain;
  uint8_t phase;
  struct pcm_samp_env_seg_s segs[PCM_SAMP_ENV_SEGS];
};

struct pcm_samp_lfo_s
{
  uint8_t waveform;
  uint32_t phase;
  uint32_t phase_inc;
  uint32_t delay_left;
  uint32_t attack_len;
  uint32_t attack_pos;
  int32_t value;
};

struct pcm_samp_filter_s
{
  uint8_t type;
  uint8_t nsec;
  uint8_t soft_clip;
  int16_t mix_q15;             /* 32767=authored full wet; preset uses tint */
  int32_t f_q15;
  int32_t q_q15;
  int32_t lp[2];
  int32_t bp[2];
  uint16_t base_freq_q15;
  uint16_t base_reso_q15;
  int16_t env2_amt_q15;
  int16_t lfo2_amt_q15;
};

struct pcm_samp_half_s
{
  atomic_uint state;   /* EMPTY / LOADING / READY */
  uint32_t pcm_off;    /* absolute byte offset in bank PCM blob */
  uint32_t nbytes;     /* valid bytes in this half */
  uint32_t gen;        /* matches voice->pp_gen when fill is current */
  uint64_t load_t0_us; /* mono us when LOADING was requested */
};

struct pcm_samp_voice_s
{
  bool active;
  uint16_t sample_id;
  int16_t instrument_id;
  int16_t note;
  int16_t velocity;
  uint32_t frames;
  uint32_t age;
  uint64_t pos;
  uint64_t rate_inc;
  uint64_t rate_inc_base;
  int16_t bend_cents;          /* per-voice pitch bend (channel bend → newest) */
  int32_t gain_q15;
  int32_t attack_q15;
  int32_t attack_inc;
  int32_t aa_z;
  int32_t aa_a_q15;
  int16_t env3_amt_q15;
  int16_t lfo1_amt_q15;
  int16_t amp_lfo2_amt_q15;
  uint8_t mod_flags;
  uint8_t ctrl_div;
  uint8_t cur_half;
  uint32_t pp_gen;             /* bumped on note_on / steal */
  uint8_t *pp_mem;             /* fixed 16 KiB slot (2 x 8 KiB) */
  struct pcm_samp_half_s half[2];
  struct pcm_samp_env_s env1;
  struct pcm_samp_env_s env2;
  struct pcm_samp_env_s env3;
  struct pcm_samp_lfo_s lfo1;
  struct pcm_samp_lfo_s lfo2;
  struct pcm_samp_filter_s filter;
  /* Dual-source Acoustic legato: MAIN (new) + TAIL (old) stream in parallel. */
  uint8_t legato_role;
  int8_t legato_pair;           /* partner voice slot, or -1 */
  uint16_t legato_xfade_len;
  uint16_t legato_xfade_pos;
  uint16_t legato_new_fade_len; /* quadratic fade-in on MAIN only */
  uint16_t legato_new_fade_pos;
  /* Open-string sympathetic resonance (Gt Body): weak derived voices. */
  uint8_t is_resonance;
  int16_t res_src_note;         /* MIDI note that excited this voice, or -1 */
};

struct pcm_samp_req_s
{
  uint8_t voice;
  uint8_t half;
  uint32_t pcm_off;
  uint32_t gen;
};

struct pcm_samp_engine_s
{
  FILE *fp;                         /* FatFs only during open (tables/heads) */
  int raw_fd;                       /* /dev/sdcard0 during playback; -1=off */
  bool raw_active;                  /* worker uses raw block I/O */
  bool raw_contig;                  /* file is one contiguous run */
  bool raw_remount;                 /* remount /sdcard on close */
  uint32_t raw_ssize;               /* sector size (usually 512) */
  uint32_t raw_csize;               /* sectors per cluster */
  uint32_t raw_ncl;                 /* clusters covered by file */
  uint64_t raw_base;                /* file byte 0 on block device */
  uint64_t raw_fsize;               /* bank file size (bytes) */
  uint32_t *raw_clba;               /* per-cluster LBA if !raw_contig */
  uint8_t *raw_bounce;              /* unaligned sector bounce (ssize + half) */

  struct pcm_samp_header_s header;
  struct pcm_samp_instrument_s *instruments;
  struct pcm_samp_region_s *regions;
  struct pcm_samp_sample_s *samples;
  struct pcm_samp_artic_s *artics;
  int16_t *heads;                   /* contiguous per-sample attack heads */
  uint32_t *head_lens;              /* frames resident in heads[] per sample */
  uint32_t head_frames;             /* max head frames (8 KiB / half @ bank) */
  uint32_t out_rate;
  int32_t master_q15;
  /* Channel pitch bend: 0..16383 (8192=center), range in cents (±). */
  uint16_t pitch_bend;
  int16_t pitch_bend_range_cents;
  /* Runtime sample start offset in milliseconds (MIDI CC27, 0..55 ms). */
  atomic_uint sample_start_ms;
  /* Env1/2/3 Attack time CC (MIDI CC70/71/72). 64 = authored, <64 slower. */
  atomic_int env_atk_cc[3];
  /* Velocity→Attack depth 0..100 (MIDI CC73). 0 = artic VelScaling only. */
  atomic_int vel_atk_amt;
  /* Velocity→S.POINT depth 0..100 (MIDI CC74). Adds up to 55 ms at vel=127. */
  atomic_int vel_spoint_amt;
  /* Open-string resonance amount 0..100 (MIDI CC28). 0 = off. */
  atomic_int gt_res_amount;
  /* Per-voice XDI filter/LFO loading for newly started voices (MIDI CC29). */
  atomic_bool xdi_mod_enable;
  uint32_t rr_state;
  uint16_t rr_last_sample[256]; /* avoid immediate ASV/RR sample repeats */
  /* Block peak follower for auto bus trim (replaces voice-count trim). */
  int32_t bus_peak_env;

  /* Soft start: linear output ramp after DAC path comes up (kills boot pop). */
  uint32_t start_fade_remain;
  uint32_t start_fade_total;

  /* Render-budget adaptive quality: 0=HQ, 1=linear, 2=fast. */
  uint8_t quality_level;
  uint8_t quality_relax_blocks;
  uint32_t quality_changes;
  uint32_t last_render_us;

  struct pcm_samp_voice_s voices[PCM_SAMP_MAX_VOICES];
  uint8_t active_list[PCM_SAMP_MAX_VOICES];
  int active_count;
  int peak_active;

  uint8_t *voice_pool;              /* MAX_VOICES x 16 KiB */
  int32_t *mixbuf;                  /* primary mix scratch */
  int32_t *mixbuf_b;                /* helper-core mix scratch */

  struct pcm_samp_req_s req[PCM_SAMP_REQ_COUNT];
  atomic_uint req_head;
  atomic_uint req_tail;
  atomic_bool stop_worker;
  sem_t worker_wake;                /* posts from request path wake CPU1 fill */
  bool worker_wake_inited;
  pthread_t worker;
  bool worker_started;

  /* Dual-core mixer helper (CPU1). */
  pthread_t mix_thread;
  bool mix_started;
  sem_t mix_go;
  sem_t mix_done;
  unsigned mix_frames;
  int mix_v0;
  int mix_v1;
  bool mix_dense;
  bool mix_fast;
  bool mix_dropped;
  uint32_t mix_under;
  uint64_t mix_cubic;
  uint64_t mix_linear;
  uint64_t mix_kick_us;             /* wall us when mix_go posted */
  uint32_t mix_kick_lat_us;         /* helper: kick -> mix start */
  uint32_t mix_b_us;                /* helper: mix_voices_range wall */

  atomic_uint cache_hits;
  atomic_uint cache_misses;
  atomic_uint underruns;
  atomic_uint io_errors;
  atomic_uint fills;
  uint64_t cubic_samples;
  uint64_t linear_samples;
  uint32_t steals;

  /* Stall diagnostics (mono us). */
  atomic_uint diag_req_drop;       /* fill queue full drops */
  atomic_uint diag_fill_stale;     /* worker discarded superseded req */
  atomic_uint diag_slow_fill;      /* fills slower than soft threshold */
  atomic_uint diag_slow_render;    /* render frames over budget */
  atomic_uint diag_q_peak;         /* peak pending fill requests */
  atomic_uint diag_fill_us_max;    /* max SD read time */
  atomic_uint diag_fill_lat_max;   /* max request->ready latency */
  atomic_uint diag_render_us_max;  /* max render wall time */
  atomic_uint diag_underrun_logs;  /* rate-limited underrun prints */
  uint64_t diag_fill_us_sum;
  uint64_t diag_render_us_sum;
  uint32_t diag_render_frames;

  /* Periodic LOAD window (~1s of audio). */
  uint32_t load_out_samples;
  uint32_t load_win_frames;
  uint64_t load_win_render_sum;
  uint32_t load_win_render_max;
  uint32_t load_sec;
  uint32_t load_fills0;
  uint32_t load_under0;
  uint32_t load_miss0;
  uint32_t load_hit0;
  uint32_t load_fill_us_max0;
  uint32_t load_fill_lat_max0;
  uint32_t load_qpeak0;
  uint32_t load_req_drop0;
  uint32_t load_peak_voices;
};

int pcm_samp_open(struct pcm_samp_engine_s *eng, const char *path,
                  uint32_t out_rate, size_t cache_bytes);
void pcm_samp_close(struct pcm_samp_engine_s *eng);
int pcm_samp_note_on(struct pcm_samp_engine_s *eng, int instrument,
                     int note, int velocity);
/* Pick the string/region with least transpose; returns instrument id or <0. */
int pcm_samp_best_instrument(struct pcm_samp_engine_s *eng, int note,
                             int velocity);
void pcm_samp_note_off(struct pcm_samp_engine_s *eng, int instrument, int note);
/* Note-Off with velocity (Dream KeyOffEnv/VolModulation). */
void pcm_samp_note_off_vel(struct pcm_samp_engine_s *eng, int instrument,
                           int note, int off_velocity);
void pcm_samp_all_off(struct pcm_samp_engine_s *eng);
/* Retune an already-sounding same-string voice without a new attack
 * (hammer-on / pull-off). from_note < 0: any active voice on instrument.
 * Returns voice slot, or falls back to note_on.
 */
int pcm_samp_legato(struct pcm_samp_engine_s *eng, int instrument,
                    int from_note, int to_note, int velocity);
/* MIDI Pitch Bend 14-bit value (0..16383, center 8192). */
void pcm_samp_set_pitch_bend(struct pcm_samp_engine_s *eng, uint16_t value14);
/* Extra sample start offset for new voices; 0 restores the authored attack. */
void pcm_samp_set_sample_start_ms(struct pcm_samp_engine_s *eng,
                                  unsigned milliseconds);
/* Env attack time CC snapshot for new voices; env_id 0=Env1..2=Env3. */
void pcm_samp_set_env_attack_cc(struct pcm_samp_engine_s *eng, int env_id,
                                int cc_0_127);
/* Product Velocity→Attack depth 0..100 (used when artic VelScaling is 0). */
void pcm_samp_set_vel_attack(struct pcm_samp_engine_s *eng, int amount_0_100);
/* Velocity→S.POINT depth 0..100 for new voices. */
void pcm_samp_set_vel_spoint(struct pcm_samp_engine_s *eng, int amount_0_100);
/* Open-string sympathetic resonance amount 0..100 (0 disables). */
void pcm_samp_set_gt_resonance(struct pcm_samp_engine_s *eng, int amount_0_100);
/* Fan-out weak open-string voices from a primary pluck. */
void pcm_samp_open_resonance(struct pcm_samp_engine_s *eng, int played_note,
                             int played_velocity);
/* Enable XDI filter/LFO; zeroed artics receive a light guitar trial preset. */
void pcm_samp_set_xdi_mod(struct pcm_samp_engine_s *eng, bool enable);
/* Arm ~350 ms output fade-in; call immediately before DAC start/prime. */
void pcm_samp_arm_start_fade(struct pcm_samp_engine_s *eng);
/* Block until start fade completes or timeout_ms elapses. */
void pcm_samp_wait_start_fade(struct pcm_samp_engine_s *eng,
                              unsigned timeout_ms);
void pcm_samp_render(struct pcm_samp_engine_s *eng, int16_t *out,
                     unsigned frames);
int pcm_samp_demo(struct pcm_samp_engine_s *eng);
int pcm_samp_selftest(struct pcm_samp_engine_s *eng, unsigned milliseconds);
void pcm_samp_stats(struct pcm_samp_engine_s *eng, const char *where);

#endif
