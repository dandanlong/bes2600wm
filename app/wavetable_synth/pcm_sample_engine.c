/****************************************************************************
 * BES2600 A7 DBNK v2 streaming PCM sampled-synth.
 *
 * Audio path reads attack heads + per-voice 16 KiB ping-pong buffers.
 * All stdio seek/read lives in pcm_worker(); render never blocks on I/O.
 ****************************************************************************/

#include "pcm_sample_engine.h"
#include "fx_psram_pool.h"
#include "xmos_fx_chain.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>

/* Soft thresholds for stall logging (us). */
#define PCM_DIAG_FILL_SLOW_US     3000u
#define PCM_DIAG_FILL_LAT_SLOW_US 8000u
#define PCM_DIAG_UNDER_LOG_MAX    24u

/* Split-voice dual mix: off. CPU1 reserved for TF fill worker. */
#ifndef PCM_SAMP_DUAL_VOICE_MIX
#  define PCM_SAMP_DUAL_VOICE_MIX 0
#endif

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>

/* FatFs also typedefs DIR; rename while pulling FIL/FATFS layouts. */
#define DIR FatFs_DIR
#include "ffconf.h"
#include "ff.h"
#undef DIR

struct pcm_fatfs_file_s
{
  int refs;
  char path[PATH_MAX + 3];
  FIL f;
};

#define MOD_FILTER    1u
#define MOD_PITCH     2u
#define MOD_AMP_LFO   4u

static void log_mallinfo(const char *where)
{
  struct mallinfo mi = mallinfo();

  syslog(LOG_NOTICE,
         "pcm_samp: mallinfo %s arena=%d ordblks=%d uordblks=%d "
         "fordblks=%d (%d KiB free)\n",
         where ? where : "?",
         mi.arena, mi.ordblks, mi.uordblks, mi.fordblks,
         mi.fordblks / 1024);
}

_Static_assert(sizeof(struct pcm_samp_header_s) == 64, "DBNK header");
_Static_assert(sizeof(struct pcm_samp_instrument_s) == 24, "DBNK instrument");
_Static_assert(sizeof(struct pcm_samp_region_s) == 20, "DBNK region");
_Static_assert(sizeof(struct pcm_samp_sample_s) == 24, "DBNK sample");
_Static_assert(sizeof(struct pcm_samp_artic_s) == 128, "DBNK artic");
_Static_assert((PCM_SAMP_REQ_COUNT & (PCM_SAMP_REQ_COUNT - 1u)) == 0,
               "req ring power-of-two");

static int16_t g_sine_q15[256];
static bool g_sine_ready;

static uint64_t pcm_mono_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull +
         (uint64_t)ts.tv_nsec / 1000ull;
}

#if PCM_SAMP_DUAL_VOICE_MIX
static int pcm_start_mix_helper(struct pcm_samp_engine_s *eng);
#endif
static bool fetch_voice_sample(struct pcm_samp_engine_s *eng,
                               uint8_t voice_idx,
                               const struct pcm_samp_sample_s *sample,
                               uint32_t frame, int16_t *value,
                               bool do_prefetch, uint32_t *hits,
                               uint32_t *misses);

static uint32_t pcm_qdepth(const struct pcm_samp_engine_s *eng)
{
  uint32_t head = atomic_load_explicit(&eng->req_head, memory_order_relaxed);
  uint32_t tail = atomic_load_explicit(&eng->req_tail, memory_order_relaxed);
  return head - tail;
}

static void pcm_note_qpeak(struct pcm_samp_engine_s *eng, uint32_t q)
{
  uint32_t cur = atomic_load_explicit(&eng->diag_q_peak, memory_order_relaxed);
  while (q > cur &&
         !atomic_compare_exchange_weak_explicit(&eng->diag_q_peak, &cur, q,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
    {
    }
}

static void pcm_note_umax(atomic_uint *slot, uint32_t v)
{
  uint32_t cur = atomic_load_explicit(slot, memory_order_relaxed);
  while (v > cur &&
         !atomic_compare_exchange_weak_explicit(slot, &cur, v,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
    {
    }
}

static inline int16_t sat16(int32_t x)
{
  return x > 32767 ? 32767 : (x < -32768 ? -32768 : (int16_t)x);
}

/* Soft bus: leave most of the range linear; only bend the top ~1.5 dB. */
static inline int16_t soft_bus16(int32_t x)
{
  const int32_t knee = 28672; /* ~-1.1 dBFS */
  const int32_t span = 8192;
  int32_t sign = 1;
  int32_t d;
  int32_t y;

  if (x < 0)
    {
      sign = -1;
      x = -x;
    }
  if (x <= knee)
    {
      return (int16_t)(sign * x);
    }
  if (x > knee + span)
    {
      x = knee + span;
    }
  d = x - knee;
  y = knee + d - (int32_t)(((int64_t)d * d) / (2 * span));
  return sat16(sign * y);
}

static inline int32_t linear4(int32_t a, int32_t b, uint32_t frac)
{
  return a + (((b - a) * (int32_t)(frac >> 16)) >> 16);
}

static inline int32_t hermite4(int32_t ym1, int32_t y0, int32_t y1,
                               int32_t y2, uint32_t frac)
{
  int32_t t = (int32_t)(frac >> 24);
  int32_t c1 = (y1 - ym1) >> 1;
  int32_t c2 = ym1 - ((5 * y0) >> 1) + (y1 << 1) - (y2 >> 1);
  int32_t c3 = ((y2 - ym1) >> 1) + ((3 * (y0 - y1)) >> 1);
  int32_t s = (c3 * t) >> 8;
  s = ((s + c2) * t) >> 8;
  s = ((s + c1) * t) >> 8;
  return s + y0;
}

static inline int32_t aa_coeff(uint64_t rate)
{
  uint32_t rq16;
  int32_t a;

  if (rate <= (1ull << 32))
    {
      return 0;
    }

  rq16 = (uint32_t)(rate >> 16);
  a = (int32_t)(((uint32_t)32768u << 16) / rq16);
  if (a < 1024)
    {
      a = 1024;
    }
  return a > 32767 ? 32767 : a;
}

static void voice_half_reset(struct pcm_samp_voice_s *v)
{
  unsigned h;

  for (h = 0; h < 2; h++)
    {
      atomic_store_explicit(&v->half[h].state, PCM_HALF_EMPTY,
                            memory_order_relaxed);
      v->half[h].pcm_off = 0;
      v->half[h].nbytes = 0;
      v->half[h].gen = 0;
      v->half[h].load_t0_us = 0;
    }
  v->cur_half = 0;
}

static void voice_request_half(struct pcm_samp_engine_s *eng,
                               uint8_t voice_idx, uint8_t half,
                               uint32_t pcm_off)
{
  struct pcm_samp_voice_s *v;
  struct pcm_samp_half_s *hh;
  uint32_t head;
  uint32_t tail;
  uint32_t state;
  uint32_t q;

  if (voice_idx >= PCM_SAMP_MAX_VOICES || half > 1u ||
      pcm_off >= eng->header.pcm_bytes)
    {
      return;
    }

  v = &eng->voices[voice_idx];
  hh = &v->half[half];
  state = atomic_load_explicit(&hh->state, memory_order_acquire);
  if ((state == PCM_HALF_READY || state == PCM_HALF_LOADING) &&
      hh->pcm_off == pcm_off && hh->gen == v->pp_gen)
    {
      return;
    }

  head = atomic_load_explicit(&eng->req_head, memory_order_relaxed);
  tail = atomic_load_explicit(&eng->req_tail, memory_order_acquire);
  q = head - tail;
  if (q >= PCM_SAMP_REQ_COUNT)
    {
      uint32_t drops =
        atomic_fetch_add_explicit(&eng->diag_req_drop, 1,
                                  memory_order_relaxed);
      if (drops < 16u)
        {
          syslog(LOG_WARNING,
                 "pcm_diag: REQ_DROP v=%u half=%u off=%lu q=%u/%u "
                 "active=%d note=%d\n",
                 voice_idx, half, (unsigned long)pcm_off, q,
                 PCM_SAMP_REQ_COUNT, eng->active_count, (int)v->note);
        }
      return;
    }

  hh->pcm_off = pcm_off;
  hh->nbytes = 0;
  hh->gen = v->pp_gen;
  hh->load_t0_us = pcm_mono_us();
  atomic_store_explicit(&hh->state, PCM_HALF_LOADING, memory_order_release);

  eng->req[head & (PCM_SAMP_REQ_COUNT - 1u)] = (struct pcm_samp_req_s)
    {
      .voice = voice_idx,
      .half = half,
      .pcm_off = pcm_off,
      .gen = v->pp_gen
    };
  atomic_store_explicit(&eng->req_head, head + 1u, memory_order_release);
  pcm_note_qpeak(eng, q + 1u);
  if (eng->worker_wake_inited)
    {
      sem_post(&eng->worker_wake);
    }
}

/* Prefer other half when free; otherwise reuse half that does not cover off. */
static void voice_request_cover(struct pcm_samp_engine_s *eng,
                                uint8_t voice_idx, uint32_t pcm_off)
{
  struct pcm_samp_voice_s *v = &eng->voices[voice_idx];
  unsigned h;
  unsigned empty = 2;
  unsigned other;

  for (h = 0; h < 2; h++)
    {
      struct pcm_samp_half_s *hh = &v->half[h];
      uint32_t st = atomic_load_explicit(&hh->state, memory_order_acquire);
      uint32_t cover = st == PCM_HALF_READY ? hh->nbytes :
                       PCM_SAMP_HALF_SIZE;
      if ((st == PCM_HALF_READY || st == PCM_HALF_LOADING) &&
          hh->gen == v->pp_gen && pcm_off >= hh->pcm_off &&
          pcm_off - hh->pcm_off + 1u < cover)
        {
          return;
        }
      if (st == PCM_HALF_EMPTY && empty > 1)
        {
          empty = h;
        }
    }

  other = v->cur_half ^ 1u;
  if (empty <= 1u)
    {
      voice_request_half(eng, voice_idx, (uint8_t)empty, pcm_off);
    }
  else
    {
      voice_request_half(eng, voice_idx, (uint8_t)other, pcm_off);
    }
}

/* After a half becomes READY, immediately queue the sibling half so CPU1
 * starts the next 8 KiB while the current half still has ~93 ms left.
 * The 4 KiB render-side threshold remains as a safety net only.
 */
static void voice_queue_next_half(struct pcm_samp_engine_s *eng,
                                 uint8_t voice_idx, uint8_t filled_half)
{
  struct pcm_samp_voice_s *v;
  struct pcm_samp_half_s *hh;
  const struct pcm_samp_sample_s *sample;
  uint32_t next_off;
  uint32_t end_off;
  uint32_t loop_end_off;
  uint8_t other;

  if (voice_idx >= PCM_SAMP_MAX_VOICES || filled_half > 1u)
    {
      return;
    }

  v = &eng->voices[voice_idx];
  if (!v->active || v->sample_id >= eng->header.sample_count)
    {
      return;
    }

  sample = &eng->samples[v->sample_id];
  hh = &v->half[filled_half];
  if (atomic_load_explicit(&hh->state, memory_order_acquire) !=
        PCM_HALF_READY ||
      hh->gen != v->pp_gen || hh->nbytes < 2u)
    {
      return;
    }

  other = filled_half ^ 1u;
  {
    uint32_t ost =
      atomic_load_explicit(&v->half[other].state, memory_order_acquire);
    /* Eager path (called right after a fill): only prime an EMPTY sibling.
     * Overwriting a READY half here would clobber data still needed while
     * the head / current half plays. Ongoing ping-pong is
     * voice_maybe_prefetch(), which may overwrite a stale sibling. */
    if (ost != PCM_HALF_EMPTY)
      {
        return;
      }
  }

  next_off = hh->pcm_off + hh->nbytes;
  end_off = sample->pcm_offset + sample->frames * 2u;

  /* If this half already covers a sustain loop end, jump to loop start. */
  if (sample->loop_type && v->env1.phase != PCM_ENV_RELEASE &&
      sample->loop_end > sample->loop_start + 1u)
    {
      loop_end_off = sample->pcm_offset + sample->loop_end * 2u;
      if (hh->pcm_off < loop_end_off &&
          loop_end_off <= hh->pcm_off + hh->nbytes)
        {
          next_off = sample->pcm_offset + sample->loop_start * 2u;
          voice_request_half(eng, voice_idx, other, next_off);
          return;
        }
    }

  if (next_off >= end_off)
    {
      if (sample->loop_type && v->env1.phase != PCM_ENV_RELEASE &&
          sample->loop_end > sample->loop_start + 1u)
        {
          next_off = sample->pcm_offset + sample->loop_start * 2u;
        }
      else
        {
          return;
        }
    }

  voice_request_half(eng, voice_idx, other, next_off);
}

static void voice_maybe_prefetch(struct pcm_samp_engine_s *eng,
                                 uint8_t voice_idx,
                                 const struct pcm_samp_sample_s *sample,
                                 uint32_t frame)
{
  struct pcm_samp_voice_s *v = &eng->voices[voice_idx];
  struct pcm_samp_half_s *cur;
  uint32_t byte_off;
  uint32_t end_off;
  uint32_t next_off;
  uint32_t loop_end_off;
  uint32_t remain;
  uint8_t other;

  if (!v->active || !sample)
    {
      return;
    }

  byte_off = sample->pcm_offset + frame * 2u;
  cur = &v->half[v->cur_half];
  if (atomic_load_explicit(&cur->state, memory_order_acquire) !=
        PCM_HALF_READY ||
      cur->gen != v->pp_gen ||
      byte_off < cur->pcm_off ||
      byte_off >= cur->pcm_off + cur->nbytes)
    {
      return;
    }

  /* Safety net while PLAYING the current half: may overwrite the sibling
   * (already-consumed stale READY). Distinct from eager fill, which must
   * not clobber a sibling that is still needed for playback. */
  remain = (cur->pcm_off + cur->nbytes) - byte_off;
  if (remain > PCM_SAMP_PREFETCH_REMAIN)
    {
      return;
    }

  other = v->cur_half ^ 1u;
  next_off = cur->pcm_off + cur->nbytes;
  end_off = sample->pcm_offset + sample->frames * 2u;

  if (sample->loop_type && v->env1.phase != PCM_ENV_RELEASE &&
      sample->loop_end > sample->loop_start + 1u)
    {
      loop_end_off = sample->pcm_offset + sample->loop_end * 2u;
      if (byte_off < loop_end_off &&
          loop_end_off - byte_off <= PCM_SAMP_PREFETCH_REMAIN)
        {
          next_off = sample->pcm_offset + sample->loop_start * 2u;
          voice_request_half(eng, voice_idx, other, next_off);
          return;
        }
    }

  if (next_off >= end_off)
    {
      if (sample->loop_type && v->env1.phase != PCM_ENV_RELEASE &&
          sample->loop_end > sample->loop_start + 1u)
        {
          next_off = sample->pcm_offset + sample->loop_start * 2u;
        }
      else
        {
          return;
        }
    }

  voice_request_half(eng, voice_idx, other, next_off);
}

static bool __attribute__((unused))
half_get_sample(struct pcm_samp_voice_s *v, uint32_t byte_off,
                            int16_t *out)
{
  unsigned h;

  for (h = 0; h < 2; h++)
    {
      struct pcm_samp_half_s *hh = &v->half[h];
      uint32_t st = atomic_load_explicit(&hh->state, memory_order_acquire);
      uint32_t rel;

      if (st != PCM_HALF_READY || hh->gen != v->pp_gen ||
          byte_off < hh->pcm_off ||
          byte_off + 1u >= hh->pcm_off + hh->nbytes)
        {
          continue;
        }

      rel = byte_off - hh->pcm_off;
      *out = (int16_t)(v->pp_mem[h * PCM_SAMP_HALF_SIZE + rel] |
                       (v->pp_mem[h * PCM_SAMP_HALF_SIZE + rel + 1u] << 8));
      if (h != v->cur_half)
        {
          v->cur_half = (uint8_t)h;
        }
      return true;
    }
  return false;
}

/* ---- Raw SD streaming (bypass FatFs cluster walks) ---- */

static int dig_fat_geom(FILE *fp, DWORD *sclust, FSIZE_t *objsize,
                        BYTE *fs_type, BYTE *stat, WORD *csize,
                        DWORD *n_fatent, LBA_t *fatbase, LBA_t *database)
{
  int fd;
  FAR struct file *filep = NULL;
  struct pcm_fatfs_file_s *ff;
  FATFS *fs;

  if (!fp)
    {
      return -EINVAL;
    }
  fd = fileno(fp);
  if (fd < 0)
    {
      return -errno;
    }
  if (fs_getfilep(fd, &filep) < 0 || !filep || !filep->f_priv)
    {
      return -ENOENT;
    }
  ff = (struct pcm_fatfs_file_s *)filep->f_priv;
  fs = ff->f.obj.fs;
  if (!fs || ff->f.obj.sclust < 2)
    {
      return -EINVAL;
    }

  *sclust = ff->f.obj.sclust;
  *objsize = ff->f.obj.objsize;
  *stat = ff->f.obj.stat;
  *fs_type = fs->fs_type;
  *csize = fs->csize;
  *n_fatent = fs->n_fatent;
  *fatbase = fs->fatbase;
  *database = fs->database;
  return 0;
}

static int raw_open_dev(const char **out_path, struct geometry *geo)
{
  static const char *k_devs[] =
    {
      "/dev/sdcard0", "/dev/mmcsd0"
    };
  unsigned i;
  int fd = -1;

  memset(geo, 0, sizeof(*geo));
  for (i = 0; i < sizeof(k_devs) / sizeof(k_devs[0]); i++)
    {
      fd = open(k_devs[i], O_RDONLY);
      if (fd < 0)
        {
          continue;
        }
      if (ioctl(fd, BIOC_GEOMETRY, (unsigned long)(uintptr_t)geo) >= 0 &&
          geo->geo_available && geo->geo_nsectors > 0 &&
          geo->geo_sectorsize >= 512)
        {
          *out_path = k_devs[i];
          return fd;
        }
      close(fd);
      fd = -1;
    }
  return -1;
}

static uint32_t fat32_get_next(int fd, uint32_t ssize, LBA_t fatbase,
                               DWORD cl)
{
  uint8_t e[4];
  off_t off = (off_t)((uint64_t)fatbase * ssize + (uint64_t)cl * 4u);

  if (lseek(fd, off, SEEK_SET) < 0 || read(fd, e, 4) != 4)
    {
      return 0xffffffffu;
    }
  return (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) |
         ((uint32_t)e[3] << 24);
}

static int raw_build_cluster_map(struct pcm_samp_engine_s *eng, int fd,
                                 DWORD sclust, FSIZE_t objsize, BYTE fs_type,
                                 BYTE stat, WORD csize, DWORD n_fatent,
                                 LBA_t fatbase, LBA_t database)
{
  uint32_t ssize = eng->raw_ssize;
  uint32_t ncl;
  uint32_t i;
  DWORD cl;
  DWORD next;
  bool contig;
  uint32_t *map = NULL;
  LBA_t base;

  if (csize < 1 || ssize < 512 || sclust < 2)
    {
      return -EINVAL;
    }

  ncl = (uint32_t)((objsize + (FSIZE_t)csize * ssize - 1) /
                   ((FSIZE_t)csize * ssize));
  if (ncl < 1)
    {
      ncl = 1;
    }

  base = database + (LBA_t)(sclust - 2u) * csize;
  contig = ((stat & 3u) == 2u);

  /* Contiguous (FatFs stat==2): skip FAT walk.  Otherwise FAT32 only. */
  if (contig)
    {
      eng->raw_csize = csize;
      eng->raw_ncl = ncl;
      eng->raw_fsize = (uint64_t)objsize;
      eng->raw_base = (uint64_t)base * ssize;
      eng->raw_contig = true;
      eng->raw_clba = NULL;
      return 0;
    }

  if (fs_type != 3) /* FS_FAT32 */
    {
      syslog(LOG_ERR,
             "pcm_samp: raw needs contiguous file on fs_type=%u\n",
             (unsigned)fs_type);
      return -ENOTSUP;
    }

  map = (uint32_t *)malloc((size_t)ncl * sizeof(uint32_t));
  if (!map)
    {
      return -ENOMEM;
    }
  cl = sclust;
  contig = true;
  for (i = 0; i < ncl; i++)
    {
      if (cl < 2 || cl >= n_fatent)
        {
          free(map);
          return -EIO;
        }
      map[i] = (uint32_t)(database + (LBA_t)(cl - 2u) * csize);
      if (i + 1u < ncl)
        {
          next = fat32_get_next(fd, ssize, fatbase, cl) & 0x0fffffffu;
          if (next < 2 || next >= 0x0ffffff8u)
            {
              free(map);
              return -EIO;
            }
          if (next != cl + 1u)
            {
              contig = false;
            }
          cl = next;
        }
    }

  eng->raw_csize = csize;
  eng->raw_ncl = ncl;
  eng->raw_fsize = (uint64_t)objsize;
  eng->raw_base = (uint64_t)base * ssize;
  eng->raw_contig = contig;
  if (contig)
    {
      free(map);
      eng->raw_clba = NULL;
    }
  else
    {
      eng->raw_clba = map;
    }
  return 0;
}

static int64_t raw_abs_byte(const struct pcm_samp_engine_s *eng,
                            uint64_t file_off)
{
  uint64_t cl_bytes;
  uint32_t ci;
  uint32_t intra;

  if (eng->raw_contig)
    {
      return (int64_t)(eng->raw_base + file_off);
    }
  cl_bytes = (uint64_t)eng->raw_csize * eng->raw_ssize;
  ci = (uint32_t)(file_off / cl_bytes);
  intra = (uint32_t)(file_off % cl_bytes);
  if (!eng->raw_clba || ci >= eng->raw_ncl)
    {
      return -1;
    }
  return (int64_t)((uint64_t)eng->raw_clba[ci] * eng->raw_ssize + intra);
}

static ssize_t raw_read_file(struct pcm_samp_engine_s *eng, uint64_t file_off,
                             void *dst, size_t len)
{
  uint32_t ss = eng->raw_ssize;
  uint8_t *out = (uint8_t *)dst;
  size_t done = 0;

  if (eng->raw_fd < 0 || !dst || len == 0 || ss < 512)
    {
      return -1;
    }
  if (file_off >= eng->raw_fsize)
    {
      return 0;
    }
  if (file_off + len > eng->raw_fsize)
    {
      len = (size_t)(eng->raw_fsize - file_off);
    }

  while (done < len)
    {
      int64_t abs = raw_abs_byte(eng, file_off + done);
      uint64_t sec;
      uint32_t skip;
      size_t chunk;
      size_t nsec;
      size_t bytes;
      ssize_t n;

      if (abs < 0)
        {
          return -1;
        }
      sec = (uint64_t)abs / ss;
      skip = (uint32_t)((uint64_t)abs % ss);
      chunk = len - done;

      /* Contiguous + sector-aligned fast path. */
      if (eng->raw_contig && skip == 0)
        {
          size_t aligned = chunk & ~(size_t)(ss - 1u);
          if (aligned >= ss)
            {
              if (lseek(eng->raw_fd, (off_t)(sec * ss), SEEK_SET) < 0)
                {
                  return -1;
                }
              n = read(eng->raw_fd, out + done, aligned);
              if (n <= 0)
                {
                  return done > 0 ? (ssize_t)done : -1;
                }
              done += (size_t)n;
              continue;
            }
        }

      /* Bounce path: one or more sectors, then copy. */
      nsec = (skip + chunk + ss - 1u) / ss;
      bytes = nsec * ss;
      if (!eng->raw_bounce || bytes > (PCM_SAMP_HALF_SIZE + ss))
        {
          /* Cap to bounce capacity. */
          nsec = (PCM_SAMP_HALF_SIZE + ss) / ss;
          if (nsec < 1)
            {
              return -1;
            }
          bytes = nsec * ss;
          if (chunk > bytes - skip)
            {
              chunk = bytes - skip;
            }
        }
      if (lseek(eng->raw_fd, (off_t)(sec * ss), SEEK_SET) < 0)
        {
          return -1;
        }
      n = read(eng->raw_fd, eng->raw_bounce, bytes);
      if (n < (ssize_t)(skip + 1u))
        {
          return done > 0 ? (ssize_t)done : -1;
        }
      if (chunk > (size_t)n - skip)
        {
          chunk = (size_t)n - skip;
        }
      memcpy(out + done, eng->raw_bounce + skip, chunk);
      done += chunk;
    }
  return (ssize_t)done;
}

static int raw_probe_dbnk_base(struct pcm_samp_engine_s *eng,
                               uint64_t disk_bytes, uint32_t ssize)
{
  char magic[4];
  uint8_t mbr[512];
  uint64_t candidates[4];
  unsigned n = 0;
  unsigned i;

  candidates[n++] = 0;
  /* Common DAILE layout: first partition @ 4 MiB on whole-disk /dev/sdcard0. */
  if (disk_bytes > (4ull * 1024u * 1024u))
    {
      candidates[n++] = 4ull * 1024u * 1024u;
    }

  /* Parse MBR partition 1 start LBA if present. */
  if (lseek(eng->raw_fd, 0, SEEK_SET) >= 0 &&
      read(eng->raw_fd, mbr, sizeof(mbr)) == (ssize_t)sizeof(mbr) &&
      mbr[510] == 0x55 && mbr[511] == 0xaa)
    {
      uint32_t lba = (uint32_t)mbr[0x1c6] |
                     ((uint32_t)mbr[0x1c7] << 8) |
                     ((uint32_t)mbr[0x1c8] << 16) |
                     ((uint32_t)mbr[0x1c9] << 24);
      uint32_t sectors = (uint32_t)mbr[0x1ca] |
                         ((uint32_t)mbr[0x1cb] << 8) |
                         ((uint32_t)mbr[0x1cc] << 16) |
                         ((uint32_t)mbr[0x1cd] << 24);
      if (mbr[0x1be] != 0 && lba != 0 && sectors != 0)
        {
          uint64_t off = (uint64_t)lba * ssize;
          if (off != candidates[0] &&
              (n < 2 || off != candidates[1]))
            {
              candidates[n++] = off;
            }
        }
    }

  for (i = 0; i < n; i++)
    {
      eng->raw_base = candidates[i];
      if (candidates[i] < disk_bytes)
        {
          eng->raw_fsize = disk_bytes - candidates[i];
        }
      else
        {
          continue;
        }
      if (raw_read_file(eng, 0, magic, sizeof(magic)) == sizeof(magic) &&
          memcmp(magic, "DBNK", 4) == 0)
        {
          if (candidates[i] != 0)
            {
              syslog(LOG_NOTICE,
                     "pcm_samp: DBNK found at disk offset %llu (0x%llx)\n",
                     (unsigned long long)candidates[i],
                     (unsigned long long)candidates[i]);
            }
          return 0;
        }
    }
  return -EINVAL;
}

static int raw_open_direct(struct pcm_samp_engine_s *eng, const char *path)
{
  struct geometry geo;
  uint64_t disk_bytes;
  int fd;

  memset(&geo, 0, sizeof(geo));
  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  if (ioctl(fd, BIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo) < 0 ||
      !geo.geo_available || geo.geo_nsectors == 0 ||
      geo.geo_sectorsize < 512)
    {
      close(fd);
      return -ENODEV;
    }

  eng->raw_fd = fd;
  eng->raw_ssize = (uint32_t)geo.geo_sectorsize;
  disk_bytes = (uint64_t)geo.geo_nsectors * eng->raw_ssize;
  eng->raw_fsize = disk_bytes;
  eng->raw_base = 0;
  eng->raw_contig = true;
  eng->raw_active = true;
  eng->raw_bounce = (uint8_t *)memalign(64, PCM_SAMP_HALF_SIZE + eng->raw_ssize);
  if (!eng->raw_bounce)
    {
      close(fd);
      eng->raw_fd = -1;
      eng->raw_active = false;
      return -ENOMEM;
    }

  if (raw_probe_dbnk_base(eng, disk_bytes, eng->raw_ssize) < 0)
    {
      syslog(LOG_ERR,
             "pcm_samp: no DBNK magic on %s (tried LBA0 / 4MiB / MBR p1)\n",
             path);
      free(eng->raw_bounce);
      eng->raw_bounce = NULL;
      close(fd);
      eng->raw_fd = -1;
      eng->raw_active = false;
      return -EINVAL;
    }

  syslog(LOG_NOTICE,
         "pcm_samp: RAW-ONLY bank=%s sectors=%lu ssize=%u "
         "base=%llu capacity=%llu MiB\n",
         path, (unsigned long)geo.geo_nsectors, eng->raw_ssize,
         (unsigned long long)eng->raw_base,
         (unsigned long long)(eng->raw_fsize / (1024u * 1024u)));
  return 0;
}

static int source_read_at(struct pcm_samp_engine_s *eng, uint64_t off,
                          void *dst, size_t bytes)
{
  if (eng->raw_active)
    {
      ssize_t got = raw_read_file(eng, off, dst, bytes);
      return got == (ssize_t)bytes ? 0 : -EIO;
    }
  if (!eng->fp || off > LONG_MAX ||
      fseek(eng->fp, (long)off, SEEK_SET) != 0 ||
      fread(dst, 1, bytes, eng->fp) != bytes)
    {
      return -EIO;
    }
  return 0;
}

static int pcm_switch_to_raw(struct pcm_samp_engine_s *eng)
{
  DWORD sclust = 0;
  FSIZE_t objsize = 0;
  BYTE fs_type = 0;
  BYTE stat = 0;
  WORD csize = 0;
  DWORD n_fatent = 0;
  LBA_t fatbase = 0;
  LBA_t database = 0;
  const char *devpath = NULL;
  struct geometry geo;
  char magic[4];
  int fd;
  int ret;

  ret = dig_fat_geom(eng->fp, &sclust, &objsize, &fs_type, &stat, &csize,
                     &n_fatent, &fatbase, &database);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "pcm_samp: dig FIL failed %d, keep FatFs\n", ret);
      return ret;
    }

  fclose(eng->fp);
  eng->fp = NULL;

  if (umount("/sdcard") != 0)
    {
      syslog(LOG_WARNING, "pcm_samp: umount /sdcard errno=%d\n", errno);
    }
  else
    {
      eng->raw_remount = true;
    }

  fd = raw_open_dev(&devpath, &geo);
  if (fd < 0)
    {
      syslog(LOG_ERR, "pcm_samp: raw open blockdev failed\n");
      if (eng->raw_remount)
        {
          mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
          eng->raw_remount = false;
        }
      return -ENODEV;
    }

  eng->raw_fd = fd;
  eng->raw_ssize = (uint32_t)geo.geo_sectorsize;
  eng->raw_bounce = (uint8_t *)memalign(64, PCM_SAMP_HALF_SIZE + eng->raw_ssize);
  if (!eng->raw_bounce)
    {
      close(fd);
      eng->raw_fd = -1;
      if (eng->raw_remount)
        {
          mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
          eng->raw_remount = false;
        }
      return -ENOMEM;
    }

  ret = raw_build_cluster_map(eng, fd, sclust, objsize, fs_type, stat, csize,
                              n_fatent, fatbase, database);
  if (ret < 0)
    {
      syslog(LOG_ERR, "pcm_samp: raw map failed %d\n", ret);
      free(eng->raw_bounce);
      eng->raw_bounce = NULL;
      close(fd);
      eng->raw_fd = -1;
      if (eng->raw_remount)
        {
          mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
          eng->raw_remount = false;
        }
      return ret;
    }

  if (raw_read_file(eng, 0, magic, 4) != 4 || memcmp(magic, "DBNK", 4) != 0)
    {
      syslog(LOG_ERR, "pcm_samp: raw LBA verify failed (not DBNK)\n");
      free(eng->raw_clba);
      eng->raw_clba = NULL;
      free(eng->raw_bounce);
      eng->raw_bounce = NULL;
      close(fd);
      eng->raw_fd = -1;
      if (eng->raw_remount)
        {
          mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL);
          eng->raw_remount = false;
        }
      return -EIO;
    }

  eng->raw_active = true;
  syslog(LOG_NOTICE,
         "pcm_samp: RAW SD %s ssize=%u csize=%u ncl=%u contig=%d "
         "base_lba=%llu fsize=%llu MiB\n",
         devpath, eng->raw_ssize, eng->raw_csize, eng->raw_ncl,
         (int)eng->raw_contig,
         (unsigned long long)(eng->raw_base / eng->raw_ssize),
         (unsigned long long)(eng->raw_fsize / (1024u * 1024u)));
  return 0;
}

static void worker_fill_one(struct pcm_samp_engine_s *eng,
                            const struct pcm_samp_req_s *req)
{
  struct pcm_samp_voice_s *v;
  struct pcm_samp_half_s *hh;
  uint8_t *dst;
  size_t want;
  size_t got;
  long seek_off;
  uint64_t file_off;
  uint64_t t0;
  uint64_t t1;
  uint32_t read_us;
  uint32_t lat_us;
  uint32_t q_before;

  if (req->voice >= PCM_SAMP_MAX_VOICES || req->half > 1u)
    {
      return;
    }

  v = &eng->voices[req->voice];
  hh = &v->half[req->half];
  if (!v->active || v->pp_gen != req->gen || hh->gen != req->gen ||
      hh->pcm_off != req->pcm_off ||
      atomic_load_explicit(&hh->state, memory_order_acquire) !=
        PCM_HALF_LOADING)
    {
      uint32_t n =
        atomic_fetch_add_explicit(&eng->diag_fill_stale, 1,
                                  memory_order_relaxed);
      if (n < 12u)
        {
          syslog(LOG_INFO,
                 "pcm_diag: FILL_STALE v=%u half=%u gen=%u/%u active=%d\n",
                 req->voice, req->half, req->gen, v->pp_gen,
                 (int)v->active);
        }
      return; /* stolen / superseded */
    }

  want = PCM_SAMP_HALF_SIZE;
  if (req->pcm_off + want > eng->header.pcm_bytes)
    {
      want = eng->header.pcm_bytes - req->pcm_off;
    }
  if (want < 2)
    {
      atomic_store_explicit(&hh->state, PCM_HALF_EMPTY, memory_order_release);
      return;
    }

  dst = v->pp_mem + (size_t)req->half * PCM_SAMP_HALF_SIZE;
  file_off = (uint64_t)eng->header.off_pcm + req->pcm_off;
  q_before = pcm_qdepth(eng);
  t0 = pcm_mono_us();

  if (eng->raw_active)
    {
      ssize_t n = raw_read_file(eng, file_off, dst, want);
      if (n < 2)
        {
          atomic_fetch_add_explicit(&eng->io_errors, 1, memory_order_relaxed);
          atomic_store_explicit(&hh->state, PCM_HALF_EMPTY,
                                memory_order_release);
          syslog(LOG_ERR,
                 "pcm_diag: IOERR raw v=%u half=%u off=%llu want=%u q=%u\n",
                 req->voice, req->half, (unsigned long long)file_off,
                 (unsigned)want, q_before);
          return;
        }
      got = (size_t)n;
    }
  else
    {
      seek_off = (long)file_off;
      if (!eng->fp || fseek(eng->fp, seek_off, SEEK_SET) != 0)
        {
          atomic_fetch_add_explicit(&eng->io_errors, 1, memory_order_relaxed);
          atomic_store_explicit(&hh->state, PCM_HALF_EMPTY,
                                memory_order_release);
          return;
        }
      got = fread(dst, 1, want, eng->fp);
      if (got < 2)
        {
          clearerr(eng->fp);
          atomic_fetch_add_explicit(&eng->io_errors, 1, memory_order_relaxed);
          atomic_store_explicit(&hh->state, PCM_HALF_EMPTY,
                                memory_order_release);
          return;
        }
    }

  t1 = pcm_mono_us();
  read_us = (uint32_t)(t1 - t0);
  lat_us = hh->load_t0_us ? (uint32_t)(t1 - hh->load_t0_us) : read_us;
  eng->diag_fill_us_sum += read_us;
  pcm_note_umax(&eng->diag_fill_us_max, read_us);
  pcm_note_umax(&eng->diag_fill_lat_max, lat_us);

  /* Drop result if note was stolen mid-read. */
  if (!v->active || v->pp_gen != req->gen || hh->pcm_off != req->pcm_off)
    {
      atomic_fetch_add_explicit(&eng->diag_fill_stale, 1, memory_order_relaxed);
      return;
    }

  got &= ~1u;
  if (got < want)
    {
      memset(dst + got, 0, want - got);
    }
  hh->nbytes = (uint32_t)got;
  atomic_store_explicit(&hh->state, PCM_HALF_READY, memory_order_release);
  atomic_fetch_add_explicit(&eng->fills, 1, memory_order_relaxed);

  /* Kick sibling half immediately — do not wait for the 4 KiB watermark. */
  voice_queue_next_half(eng, req->voice, req->half);

  if (read_us >= PCM_DIAG_FILL_SLOW_US ||
      lat_us >= PCM_DIAG_FILL_LAT_SLOW_US)
    {
      uint32_t n =
        atomic_fetch_add_explicit(&eng->diag_slow_fill, 1,
                                  memory_order_relaxed);
      if (n < 32u)
        {
          syslog(LOG_WARNING,
                 "pcm_diag: SLOW_FILL v=%u half=%u note=%d "
                 "read=%uus lat=%uus want=%u q=%u active=%d\n",
                 req->voice, req->half, (int)v->note, read_us, lat_us,
                 (unsigned)want, q_before, eng->active_count);
        }
    }
}

static void worker_drain(struct pcm_samp_engine_s *eng)
{
  uint32_t head;
  uint32_t tail;

  tail = atomic_load_explicit(&eng->req_tail, memory_order_relaxed);
  head = atomic_load_explicit(&eng->req_head, memory_order_acquire);
  while (tail != head)
    {
      struct pcm_samp_req_s req =
        eng->req[tail & (PCM_SAMP_REQ_COUNT - 1u)];
      tail++;
      atomic_store_explicit(&eng->req_tail, tail, memory_order_release);
      worker_fill_one(eng, &req);
      head = atomic_load_explicit(&eng->req_head, memory_order_acquire);
    }
}

static void *pcm_worker(void *arg)
{
  struct pcm_samp_engine_s *eng = arg;
  while (!atomic_load_explicit(&eng->stop_worker, memory_order_acquire))
    {
      if (sem_wait(&eng->worker_wake) != 0)
        {
          continue;
        }
      if (atomic_load_explicit(&eng->stop_worker, memory_order_acquire))
        {
          break;
        }
      worker_drain(eng);
    }
  return NULL;
}

/* CPU1: TF/raw fills only (SDMMC DMA + memcpy). Mix stays on CPU0. */
static int pcm_start_fill_worker(struct pcm_samp_engine_s *eng)
{
  pthread_attr_t attr;
  struct sched_param sparam;
  int ret;

  if (!eng->worker_wake_inited)
    {
      if (sem_init(&eng->worker_wake, 0, 0) != 0)
        {
          return -errno;
        }
      eng->worker_wake_inited = true;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 24 * 1024);
#ifdef CONFIG_SMP
  {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  }
#endif
  /* Highest FIFO on CPU1 so fill never yields to USB/MIDI helpers there.
   * Audio mix stays on CPU0 at max-9 and is unaffected.
   * SDMMC DMA wait uses HAL yield+sem (not busy-spin); this thread stays
   * max-prio so DMA completion immediately preempts any lower-prio FX. */
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  sparam.sched_priority = sched_get_priority_max(SCHED_FIFO);
  pthread_attr_setschedparam(&attr, &sparam);
  ret = pthread_create(&eng->worker, &attr, pcm_worker, eng);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      return -ret;
    }
  eng->worker_started = true;
  syslog(LOG_NOTICE,
         "pcm_samp: TF fill worker on CPU1 prio=max "
         "(SDMMC DMA yield; mix on CPU0; next-half eager; sem wake)\n");
  return 0;
}

static uint32_t env_duration(int16_t rate, uint32_t hz)
{
  int32_t r = 32767 - rate;
  uint32_t n;
  if (r < 0)
    {
      r = 0;
    }
  n = (uint32_t)(((uint64_t)r * hz * 2u) / 32767u);
  return n < 24 ? 24 : n;
}

static void env_segment(struct pcm_samp_env_s *env, uint8_t seg, uint32_t hz)
{
  int32_t delta;
  uint32_t dur;
  env->seg = seg;
  env->target = env->segs[seg].level_q15;
  if (env->target < 0)
    {
      env->target = 0;
    }
  delta = env->target - env->level;
  dur = env_duration(env->segs[seg].rate_q15, hz);
  env->inc = delta / (int32_t)dur;
  if (env->inc == 0 && delta != 0)
    {
      env->inc = delta > 0 ? 1 : -1;
    }
}

static void env_load(struct pcm_samp_env_s *env,
                     const struct pcm_samp_env_desc_s *desc, uint32_t hz)
{
  unsigned i;
  memset(env, 0, sizeof(*env));
  if (desc == NULL || desc->seg_count == 0)
    {
      env->nsegs = 4;
      env->sustain = 2;
      env->segs[0] = (struct pcm_samp_env_seg_s){32767, 0};
      env->segs[1] = (struct pcm_samp_env_seg_s){32767, 32767};
      env->segs[2] = (struct pcm_samp_env_seg_s){32440, 32767};
      env->segs[3] = (struct pcm_samp_env_seg_s){27852, 0};
    }
  else
    {
      env->nsegs = desc->seg_count > PCM_SAMP_ENV_SEGS ?
                   PCM_SAMP_ENV_SEGS : desc->seg_count;
      env->sustain = desc->sustain_point;
      if (env->sustain < 0)
        {
          env->sustain = 0;
        }
      if (env->sustain >= env->nsegs)
        {
          env->sustain = env->nsegs - 1;
        }
      for (i = 0; i < PCM_SAMP_ENV_SEGS; i++)
        {
          env->segs[i] = desc->segs[i];
        }
    }

  env->level = env->segs[0].level_q15;
  if (env->sustain == 0 || env->nsegs < 2)
    {
      env->phase = PCM_ENV_SUSTAIN;
    }
  else
    {
      env->phase = PCM_ENV_RUN;
      env_segment(env, 1, hz);
    }
}

static bool env_tick(struct pcm_samp_env_s *env, uint32_t hz)
{
  bool reached;
  uint8_t next;
  if (env->phase == PCM_ENV_IDLE)
    {
      return false;
    }
  if (env->phase == PCM_ENV_SUSTAIN)
    {
      return true;
    }

  env->level += env->inc;
  reached = env->inc >= 0 ? env->level >= env->target :
                            env->level <= env->target;
  if (!reached)
    {
      return true;
    }
  env->level = env->target;

  if (env->phase == PCM_ENV_RUN && env->seg >= (uint8_t)env->sustain)
    {
      env->phase = PCM_ENV_SUSTAIN;
      env->inc = 0;
      return true;
    }

  next = env->seg + 1u;
  if (next >= env->nsegs)
    {
      if (env->phase == PCM_ENV_RELEASE)
        {
          env->level = 0;
          env->phase = PCM_ENV_IDLE;
          return false;
        }
      env->phase = PCM_ENV_SUSTAIN;
      return true;
    }
  env_segment(env, next, hz);
  return true;
}

/* Same as env_tick, but apply n sample-periods (for control-rate ENV2/3). */
static bool env_tick_n(struct pcm_samp_env_s *env, uint32_t hz, unsigned n)
{
  if (n <= 1u)
    {
      return env_tick(env, hz);
    }
  if (env->phase == PCM_ENV_IDLE)
    {
      return false;
    }
  if (env->phase == PCM_ENV_SUSTAIN)
    {
      return true;
    }
  env->level += env->inc * (int32_t)(n - 1u);
  return env_tick(env, hz);
}

/* Force amp to 0 over ms (also accelerates an in-progress release). */
static void env_force_fade_ms(struct pcm_samp_env_s *env, uint32_t hz,
                              uint32_t ms)
{
  uint32_t dur;

  if (env->phase == PCM_ENV_IDLE || env->level <= 0)
    {
      env->level = 0;
      env->phase = PCM_ENV_IDLE;
      env->inc = 0;
      return;
    }
  if (ms < 1u)
    {
      ms = 1u;
    }
  dur = (hz * ms) / 1000u;
  if (dur < 1u)
    {
      dur = 1u;
    }
  env->phase = PCM_ENV_RELEASE;
  env->target = 0;
  /* Point past last segment so env_tick idles when level hits 0. */
  env->seg = env->nsegs > 0 ? (uint8_t)(env->nsegs - 1u) : 0;
  env->inc = -(env->level / (int32_t)dur);
  if (env->inc >= 0)
    {
      env->inc = -1;
    }
}

static void env_release_vel(struct pcm_samp_env_s *env, uint32_t hz,
                            int off_velocity)
{
  uint8_t next;
  uint32_t dur;
  int32_t scale_q8;
  int vel;
  int32_t old_inc;

  if (env->phase == PCM_ENV_IDLE)
    {
      return;
    }

  vel = off_velocity;
  if (vel < 1)
    {
      vel = 1;
    }
  if (vel > 127)
    {
      vel = 127;
    }

  /* Already releasing: only re-accelerate when a harder key-off arrives. */
  if (env->phase == PCM_ENV_RELEASE)
    {
      if (vel > 80 && env->level > 0)
        {
          uint32_t hard_ms = (uint32_t)(12 - ((vel - 80) / 8));
          if (hard_ms < 4u)
            {
              hard_ms = 4u;
            }
          env_force_fade_ms(env, hz, hard_ms);
        }
      return;
    }

  next = (uint8_t)env->sustain + 1u;
  env->phase = PCM_ENV_RELEASE;
  if (next < env->nsegs)
    {
      env_segment(env, next, hz);
    }
  else
    {
      env->target = 0;
      dur = hz / 5u;
      env->inc = -(env->level / (int32_t)(dur ? dur : 1));
      if (env->inc == 0)
        {
          env->inc = -1;
        }
    }

  /* Dream KeyOffEnvModulation: hard note-off damps faster, soft note-off
   * keeps more of the programmed release.  vel=64 ≈ authored rate.
   */
  /* scale ≈ 2.5 @vel1 → 1.0 @vel64 → 0.4 @vel127 on release duration. */
  scale_q8 = 640 - (vel * 6);
  if (scale_q8 < 100)
    {
      scale_q8 = 100;
    }
  if (scale_q8 > 640)
    {
      scale_q8 = 640;
    }
  old_inc = env->inc;
  if (old_inc != 0)
    {
      int64_t inc = ((int64_t)old_inc * 256) / scale_q8;
      if (inc == 0)
        {
          inc = old_inc > 0 ? 1 : -1;
        }
      if (inc > INT32_MAX)
        {
          inc = INT32_MAX;
        }
      if (inc < INT32_MIN)
        {
          inc = INT32_MIN;
        }
      env->inc = (int32_t)inc;
    }
}

static void env_release(struct pcm_samp_env_s *env, uint32_t hz)
{
  env_release_vel(env, hz, 64);
}

static int32_t lfo_wave(uint8_t wave, uint32_t phase)
{
  uint8_t idx = phase >> 24;
  switch (wave)
    {
      case 1:
        {
          uint32_t p = phase >> 14;
          return p < 65536u ? (int32_t)p - 32768 :
                              98304 - (int32_t)p;
        }
      case 2:
      case 4:
      case 5:
        return phase & 0x80000000u ? 32767 : -32767;
      case 3:
        return (int32_t)(phase >> 17) - 32768;
      default:
        {
          int32_t a = g_sine_q15[idx];
          int32_t b = g_sine_q15[(uint8_t)(idx + 1)];
          return a + (((b - a) * (int32_t)((phase >> 16) & 255u)) >> 8);
        }
    }
}

static void lfo_load(struct pcm_samp_lfo_s *lfo,
                     const struct pcm_samp_lfo_desc_s *desc, uint32_t hz)
{
  memset(lfo, 0, sizeof(*lfo));
  if (desc == NULL)
    {
      return;
    }
  lfo->waveform = desc->waveform;
  lfo->phase_inc = (uint32_t)(((uint64_t)(desc->freq_q8 ?
                                          desc->freq_q8 : 768u) << 24) / hz);
  lfo->delay_left = ((uint32_t)desc->delay_ms * hz) / 1000u;
  lfo->attack_len = ((uint32_t)desc->attack_ms * hz) / 1000u;
}

/* Advance LFO by n sample periods so control-rate ticks keep musical rate. */
static void lfo_tick_n(struct pcm_samp_lfo_s *lfo, unsigned n)
{
  int32_t val;
  if (n == 0)
    {
      return;
    }
  if (lfo->delay_left)
    {
      if (lfo->delay_left > n)
        {
          lfo->delay_left -= n;
          lfo->value = 0;
          return;
        }
      n -= lfo->delay_left;
      lfo->delay_left = 0;
      if (n == 0)
        {
          lfo->value = 0;
          return;
        }
    }
  lfo->phase += (uint32_t)((uint64_t)lfo->phase_inc * n);
  val = lfo_wave(lfo->waveform, lfo->phase);
  if (lfo->attack_len && lfo->attack_pos < lfo->attack_len)
    {
      uint32_t pos = lfo->attack_pos + n;
      if (pos > lfo->attack_len)
        {
          pos = lfo->attack_len;
        }
      lfo->attack_pos = pos;
      val = (val * (int32_t)((pos * 32767u) / lfo->attack_len)) >> 15;
    }
  lfo->value = val;
}

static int32_t vel_curve(int32_t t, uint8_t curve)
{
  int32_t t2 = (t * t) >> 15;
  switch (curve)
    {
      case 1:
        return t2;
      case 2:
        return (t2 * t) >> 15;
      case 3:
        t = (t << 1) - t2;
        return t > 32767 ? 32767 : t;
      case 4:
        t = 3 * t2 - (2 * ((t2 * t) >> 15));
        return t > 32767 ? 32767 : (t < 0 ? 0 : t);
      default:
        return t;
    }
}

static int32_t kbd_eval_q7(const struct pcm_samp_kbd_point_s *pts,
                          uint8_t count, int note)
{
  uint8_t i;
  int32_t num;

  if (!pts || count == 0)
    {
      return 0;
    }
  if (count > 4)
    {
      count = 4;
    }
  if (note <= pts[0].key)
    {
      return pts[0].value_q7;
    }
  for (i = 1; i < count; i++)
    {
      if (note <= pts[i].key)
        {
          int span = (int)pts[i].key - (int)pts[i - 1].key;
          if (span <= 0)
            {
              return pts[i].value_q7;
            }
          num = ((int32_t)pts[i].value_q7 -
                 (int32_t)pts[i - 1].value_q7) *
                (note - (int)pts[i - 1].key);
          return (int32_t)pts[i - 1].value_q7 + num / span;
        }
    }
  return pts[count - 1].value_q7;
}

static bool artic_extended(const struct pcm_samp_artic_s *a)
{
  return a && (a->amp_kbd_count & 0x80u) != 0;
}

static int32_t velocity_gain(const struct pcm_samp_artic_s *a, int velocity,
                             int note)
{
  int minx;
  int maxx;
  int32_t t;
  int32_t shape;
  int32_t y_q8;
  int32_t gain;
  int32_t sens;
  int32_t vol;
  double db;
  double db_gain;
  bool new_curve;

  if (a == NULL)
    {
      return (velocity * 32767) / 127;
    }
  minx = a->amp_vel_min_x;
  maxx = a->amp_vel_max_x;
  if (maxx <= minx)
    {
      minx = 1;
      maxx = 127;
    }
  t = ((velocity - minx) * 32767) / (maxx - minx);
  if (t < 0)
    {
      t = 0;
    }
  if (t > 32767)
    {
      t = 32767;
    }
  shape = vel_curve(t, a->amp_vel_curve);
  y_q8 = a->amp_vel_min_y_q8 +
         (((a->amp_vel_max_y_q8 - a->amp_vel_min_y_q8) * shape) >> 15);

  /* SND5000 new curves (LegacyVelocity=0): Min/MaxVelModY are dB packed as
   * Q8 (value*256).  Full XDI depth (e.g. -32 dB) is too extreme on this
   * guitar bank because 5 velocity layers already carry most dynamics —
   * applying the raw dB on top made typical mid-velocity playing ~20 dB
   * quieter than before.  Keep the curve shape, but use 45% depth so soft
   * notes stay clearly quieter without collapsing overall level.
   * Flat Y still uses the older VelOffset/VelSlope path.
   */
  new_curve = (a->amp_vel_min_y_q8 != 0 || a->amp_vel_max_y_q8 != 0);
  if (new_curve)
    {
      db = ((double)y_q8 / 256.0) * 0.45;
      db_gain = pow(10.0, db / 20.0);
      gain = (int32_t)(32767.0 * db_gain);
    }
  else
    {
      gain = 32767;
      sens = 128 + a->amp_vel_offset +
             ((a->amp_vel_slope_q8 * velocity) / 127);
      if (sens < 1)
        {
          sens = 1;
        }
      gain = (gain * sens) / 128;
    }
  if (gain < 0)
    {
      gain = 0;
    }
  if (gain > 32767)
    {
      gain = 32767;
    }

  vol = a->amp_volume_q8 == 0 ? 32767 :
        (a->amp_volume_q8 > 0 ? a->amp_volume_q8 << 7 :
                               32767 + (a->amp_volume_q8 << 7));
  if (vol < 0)
    {
      vol = 0;
    }
  if (vol > 32767)
    {
      vol = 32767;
    }
  gain = (gain * vol) >> 15;
  if (artic_extended(a))
    {
      db = (double)a->mixer_volume_q4 / 16.0;
      db += (double)kbd_eval_q7(a->amp_kbd,
                               a->amp_kbd_count & 0x0fu, note) / 128.0;
      db_gain = pow(10.0, db / 20.0);
      gain = (int32_t)(gain * db_gain);
      if (gain > 32767)
        {
          gain = 32767;
        }
      if (gain < 0)
        {
          gain = 0;
        }
    }
  return gain;
}

static void env_apply_kbd(struct pcm_samp_env_s *env,
                          const struct pcm_samp_artic_s *a,
                          int note, uint32_t hz)
{
  int32_t mod_q7;
  unsigned i;

  if (!env || !artic_extended(a) || a->env1_kbd_count == 0)
    {
      return;
    }
  mod_q7 = kbd_eval_q7(a->env1_kbd, a->env1_kbd_count, note);
  for (i = 0; i < env->nsegs; i++)
    {
      int32_t rate = env->segs[i].rate_q15;
      int32_t remain = 32767 - rate;
      /* Positive Dream table values shorten envelope time proportionally,
       * while preserving the segment curve and avoiding a hard 1.0 clamp.
       */
      remain -= (remain * mod_q7) / 128;
      if (remain < 0)
        {
          remain = 0;
        }
      if (remain > 65534)
        {
          remain = 65534;
        }
      env->segs[i].rate_q15 = (int16_t)(32767 - remain);
    }
  if (env->phase == PCM_ENV_RUN && env->seg < env->nsegs)
    {
      env_segment(env, env->seg, hz);
    }
}

/* Dream attack is usually segment 1 (seg0 = start level). */
static int env_attack_seg(const struct pcm_samp_env_s *env)
{
  if (!env || env->nsegs < 2 || env->sustain < 1)
    {
      return -1;
    }
  return 1;
}

/* depth_q7 > 0 shortens attack; < 0 lengthens. Same remain math as kbd. */
static void env_scale_attack(struct pcm_samp_env_s *env, int32_t depth_q7,
                             uint32_t hz)
{
  int seg;
  int32_t rate;
  int32_t remain;

  if (!env || depth_q7 == 0)
    {
      return;
    }
  if (depth_q7 > 127)
    {
      depth_q7 = 127;
    }
  if (depth_q7 < -127)
    {
      depth_q7 = -127;
    }
  seg = env_attack_seg(env);
  if (seg < 0)
    {
      return;
    }
  rate = env->segs[seg].rate_q15;
  remain = 32767 - rate;
  /* Near-instant authored attacks (remain≈0) still need a reference so
   * negative depth can lengthen; positive depth shortens from authored.
   */
  if (depth_q7 < 0 && remain < 1600)
    {
      remain = 1600;
    }
  remain -= (remain * depth_q7) / 128;
  if (remain < 0)
    {
      remain = 0;
    }
  if (remain > 65534)
    {
      remain = 65534;
    }
  env->segs[seg].rate_q15 = (int16_t)(32767 - remain);
  /* Sampled guitar often starts Env1 already at peak (delta≈0).  For slower
   * attacks, dip the start level so the lengthened segment is audible.
   */
  if (depth_q7 < 0)
    {
      int32_t peak = env->segs[seg].level_q15;
      int32_t start = peak + (peak * depth_q7) / 128;

      if (peak < 0)
        {
          peak = 0;
        }
      if (start < 0)
        {
          start = 0;
        }
      if (start > peak)
        {
          start = peak;
        }
      if (env->level > start)
        {
          env->level = start;
        }
    }
  if (env->phase == PCM_ENV_RUN && env->seg == (uint8_t)seg)
    {
      env_segment(env, (uint8_t)seg, hz);
    }
}

static int32_t env_atk_cc_depth(int cc_0_127)
{
  int32_t d;

  if (cc_0_127 < 0)
    {
      cc_0_127 = 0;
    }
  if (cc_0_127 > 127)
    {
      cc_0_127 = 127;
    }
  /* 64 = no change; 0 ≈ longest, 127 ≈ shortest. */
  d = (cc_0_127 - 64) * 2;
  if (d < -127)
    {
      d = -127;
    }
  if (d > 127)
    {
      d = 127;
    }
  return d;
}

static int32_t env_atk_vel_depth(int8_t artic_vel_scaling, int velocity,
                                 int vel_amt_0_100)
{
  int32_t scale = artic_vel_scaling;
  int32_t depth;

  if (velocity < 0)
    {
      velocity = 0;
    }
  if (velocity > 127)
    {
      velocity = 127;
    }
  if (scale == 0 && vel_amt_0_100 > 0)
    {
      /* Product depth: higher velocity shortens attack. */
      scale = (vel_amt_0_100 * 127) / 100;
      if (scale < 1)
        {
          scale = 1;
        }
      if (scale > 127)
        {
          scale = 127;
        }
    }
  if (scale == 0)
    {
      return 0;
    }
  depth = (scale * (velocity - 64)) / 64;
  if (depth > 127)
    {
      depth = 127;
    }
  if (depth < -127)
    {
      depth = -127;
    }
  return depth;
}

static void env_apply_attack_mods(struct pcm_samp_engine_s *eng,
                                  struct pcm_samp_env_s *env,
                                  int8_t artic_vel_scaling, int velocity,
                                  int env_id)
{
  int32_t depth;
  int cc;
  int vel_amt;

  if (!eng || !env || env_id < 0 || env_id > 2)
    {
      return;
    }
  vel_amt = atomic_load_explicit(&eng->vel_atk_amt, memory_order_relaxed);
  depth = env_atk_vel_depth(artic_vel_scaling, velocity, vel_amt);
  cc = atomic_load_explicit(&eng->env_atk_cc[env_id], memory_order_relaxed);
  depth += env_atk_cc_depth(cc);
  if (depth > 127)
    {
      depth = 127;
    }
  if (depth < -127)
    {
      depth = -127;
    }
  env_scale_attack(env, depth, eng->out_rate);
}

static void filter_load(struct pcm_samp_filter_s *f,
                        const struct pcm_samp_artic_s *a)
{
  static const uint8_t sections[4] = {1, 1, 2, 2};
  memset(f, 0, sizeof(*f));
  if (a == NULL || a->filter_type == 0)
    {
      return;
    }
  f->type = a->filter_type;
  f->nsec = a->filter_slope < 4 ? sections[a->filter_slope] : 1;
  f->soft_clip = a->soft_clip;
  f->mix_q15 = 32767;
  f->base_freq_q15 = a->filter_freq_q15 ?
                     a->filter_freq_q15 : 32767;
  f->base_reso_q15 = a->filter_reso_q15;
  f->env2_amt_q15 = a->filter_env2_amount_q15;
  f->lfo2_amt_q15 = a->filter_lfo2_amount_q15;
}

static void filter_update(struct pcm_samp_filter_s *f, int32_t env,
                          int32_t lfo)
{
  int32_t fq;
  int32_t damp;
  if (!f->type)
    {
      return;
    }
  fq = f->base_freq_q15 + ((f->env2_amt_q15 * env) >> 15) +
       ((f->lfo2_amt_q15 * lfo) >> 15);
  if (fq < 256)
    {
      fq = 256;
    }
  if (fq > 32767)
    {
      fq = 32767;
    }
  f->f_q15 = fq >> 2;
  damp = 32767 - ((int32_t)f->base_reso_q15 * 30000 / 32767);
  f->q_q15 = damp < 4096 ? 4096 : damp;
}

static int32_t filter_run(struct pcm_samp_filter_s *f, int32_t x)
{
  unsigned i;
  int32_t y = x;
  for (i = 0; i < f->nsec; i++)
    {
      int32_t lp = f->lp[i] + ((f->f_q15 * f->bp[i]) >> 15);
      int32_t hp = y - lp - ((f->q_q15 * f->bp[i]) >> 15);
      int32_t bp = f->bp[i] + ((f->f_q15 * hp) >> 15);
      f->lp[i] = lp > 65535 ? 65535 : (lp < -65536 ? -65536 : lp);
      f->bp[i] = bp > 65535 ? 65535 : (bp < -65536 ? -65536 : bp);
      y = f->type == 2 ? hp : (f->type == 3 ? bp :
          (f->type == 4 ? lp + hp : lp));
    }
  if (f->soft_clip)
    {
      int32_t x2 = (y * y) >> 15;
      y -= ((x2 * y) >> 15) / 3;
    }
  return y > 32767 ? 32767 : (y < -32768 ? -32768 : y);
}

static bool artic_mod_is_zero(const struct pcm_samp_artic_s *a)
{
  return a == NULL ||
         (a->filter_type == 0 &&
          a->filter_env2_amount_q15 == 0 &&
          a->filter_lfo2_amount_q15 == 0 &&
          a->wave_env3_amount_q15 == 0 &&
          a->wave_lfo1_amount_q15 == 0 &&
          a->amp_lfo2_amount_q15 == 0);
}

static void voice_clear_mod(struct pcm_samp_voice_s *v)
{
  memset(&v->env2, 0, sizeof(v->env2));
  memset(&v->env3, 0, sizeof(v->env3));
  memset(&v->lfo1, 0, sizeof(v->lfo1));
  memset(&v->lfo2, 0, sizeof(v->lfo2));
  memset(&v->filter, 0, sizeof(v->filter));
  v->env3_amt_q15 = 0;
  v->lfo1_amt_q15 = 0;
  v->amp_lfo2_amt_q15 = 0;
  v->mod_flags = 0;
  v->ctrl_div = 0;
}

static void voice_load_mod(struct pcm_samp_engine_s *eng,
                           struct pcm_samp_voice_s *v,
                           const struct pcm_samp_artic_s *a)
{
  struct pcm_samp_artic_s trial;
  const struct pcm_samp_artic_s *use = a;
  bool trial_used = false;

  voice_clear_mod(v);
  if (!eng || !atomic_load_explicit(&eng->xdi_mod_enable,
                                     memory_order_relaxed))
    {
      return;
    }

  if (artic_mod_is_zero(a))
    {
      memset(&trial, 0, sizeof(trial));
      if (a)
        {
          trial = *a;
        }

      /* Conservative guitar preset: a mostly-dry LP tint that opens on the
       * pick then warms over ~280 ms, plus ~1.5% tremolo after the attack.
       * Pitch modulation stays off so sampled guitar intonation is preserved.
       */
      trial.filter_type = 1;              /* low pass */
      trial.filter_slope = 0;             /* one section / 6 dB */
      trial.filter_freq_q15 = 30000;
      trial.filter_reso_q15 = 0;
      trial.filter_env2_amount_q15 = 2500;
      trial.filter_lfo2_amount_q15 = 0;
      trial.env2.seg_count = 3;
      trial.env2.sustain_point = 1;
      trial.env2.loop_start = -1;
      trial.env2.vel_scaling = 0;
      trial.env2.segs[0] =
        (struct pcm_samp_env_seg_s){32767, 32767};
      trial.env2.segs[1] =
        (struct pcm_samp_env_seg_s){28180, 0}; /* ~280 ms decay */
      trial.env2.segs[2] =
        (struct pcm_samp_env_seg_s){30801, 0}; /* ~120 ms release */
      trial.lfo2.waveform = 0;             /* sine */
      trial.lfo2.freq_q8 = 5u * 256u;      /* 5 Hz */
      trial.lfo2.delay_ms = 180;
      trial.lfo2.attack_ms = 500;
      trial.amp_lfo2_amount_q15 = 500;     /* ~1.5% */
      use = &trial;
      trial_used = true;
    }

  env_load(&v->env2, &use->env2, eng->out_rate);
  env_load(&v->env3, &use->env3, eng->out_rate);
  env_apply_attack_mods(eng, &v->env2, use->env2.vel_scaling,
                        v->velocity, 1);
  env_apply_attack_mods(eng, &v->env3, use->env3.vel_scaling,
                        v->velocity, 2);
  lfo_load(&v->lfo1, &use->lfo1, eng->out_rate);
  lfo_load(&v->lfo2, &use->lfo2, eng->out_rate);
  filter_load(&v->filter, use);
  if (trial_used && v->filter.type)
    {
      v->filter.mix_q15 = 10000; /* ~30% wet: audible but not muffled */
    }
  v->env3_amt_q15 = use->wave_env3_amount_q15;
  v->lfo1_amt_q15 = use->wave_lfo1_amount_q15;
  v->amp_lfo2_amt_q15 = use->amp_lfo2_amount_q15;
  if (v->filter.type)
    {
      v->mod_flags |= MOD_FILTER;
      filter_update(&v->filter, v->env2.level, v->lfo2.value);
    }
  if (v->env3_amt_q15 != 0 || v->lfo1_amt_q15 != 0)
    {
      v->mod_flags |= MOD_PITCH;
    }
  if (v->amp_lfo2_amt_q15 != 0)
    {
      v->mod_flags |= MOD_AMP_LFO;
    }
}

static void rebuild_active(struct pcm_samp_engine_s *eng)
{
  int i;
  int n = 0;
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      if (eng->voices[i].active)
        {
          eng->active_list[n++] = i;
        }
    }
  eng->active_count = n;
  if (n > eng->peak_active)
    {
      eng->peak_active = n;
    }
}

static uint32_t rr_random(struct pcm_samp_engine_s *eng)
{
  uint32_t x = eng->rr_state;
  if (x == 0)
    {
      x = 0x6d2b79f5u;
    }
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  eng->rr_state = x;
  return x;
}

static const struct pcm_samp_region_s *
resolve_region(struct pcm_samp_engine_s *eng, int instrument,
               int note, int velocity, bool vary)
{
  const struct pcm_samp_instrument_s *inst;
  const struct pcm_samp_region_s *best = NULL;
  const struct pcm_samp_region_s *hits[16];
  int best_dist = 1000;
  int nhit = 0;
  int pick;
  uint16_t i;
  if (instrument < 0 || instrument >= eng->header.instrument_count)
    {
      return NULL;
    }
  inst = &eng->instruments[instrument];
  for (i = 0; i < inst->region_count; i++)
    {
      const struct pcm_samp_region_s *r =
        &eng->regions[inst->region_begin + i];
      int dist;
      if (note < r->key_lo || note > r->key_hi ||
          velocity < r->vel_lo || velocity > r->vel_hi)
        {
          continue;
        }
      dist = note - ((int)r->unity_note + (int)r->coarse_tune);
      if (dist < 0)
        {
          dist = -dist;
        }
      /* Prefer nearer recorded pitch; later matches win ties. */
      if (!best || dist <= best_dist)
        {
          best = r;
          best_dist = dist;
        }
    }

  /* ASV group zero is a normal layer.  A non-zero group means that matching
   * regions are alternatives (Dream Auto Sample Variation / RR).  Only vary
   * among equally close root samples so RR never worsens transposition.
   */
  if (!vary || !best || best->asv_group == 0)
    {
      return best;
    }
  for (i = 0; i < inst->region_count && nhit < 16; i++)
    {
      const struct pcm_samp_region_s *r =
        &eng->regions[inst->region_begin + i];
      int dist;
      if (r->asv_group != best->asv_group ||
          note < r->key_lo || note > r->key_hi ||
          velocity < r->vel_lo || velocity > r->vel_hi)
        {
          continue;
        }
      dist = note - ((int)r->unity_note + (int)r->coarse_tune);
      if (dist < 0)
        {
          dist = -dist;
        }
      if (dist == best_dist)
        {
          hits[nhit++] = r;
        }
    }
  if (nhit < 2)
    {
      return best;
    }

  pick = (int)(rr_random(eng) % (uint32_t)nhit);
  if (instrument < 256 &&
      hits[pick]->sample_id == eng->rr_last_sample[instrument])
    {
      pick = (pick + 1 + (int)(rr_random(eng) % (uint32_t)(nhit - 1))) %
             nhit;
    }
  best = hits[pick];
  if (instrument < 256)
    {
      eng->rr_last_sample[instrument] = best->sample_id;
    }
  return best;
}

int pcm_samp_best_instrument(struct pcm_samp_engine_s *eng, int note,
                             int velocity)
{
  int best_inst = -1;
  int best_dist = 1000;
  int inst;

  if (!eng || note < 0 || note > 127)
    {
      return -EINVAL;
    }
  if (velocity < 1)
    {
      velocity = 1;
    }
  if (velocity > 127)
    {
      velocity = 127;
    }

  for (inst = 0; inst < eng->header.instrument_count; inst++)
    {
      const struct pcm_samp_region_s *r =
        resolve_region(eng, inst, note, velocity, false);
      int dist;
      if (!r)
        {
          continue;
        }
      dist = note - ((int)r->unity_note + (int)r->coarse_tune);
      if (dist < 0)
        {
          dist = -dist;
        }
      /* On equal transpose, prefer lower (wound) strings: higher inst id
       * in this bank (0=high E ... 5=low E).
       */
      if (best_inst < 0 || dist < best_dist ||
          (dist == best_dist && inst > best_inst))
        {
          best_inst = inst;
          best_dist = dist;
        }
    }
  return best_inst;
}

static uint32_t isqrt_u32(uint32_t x);

static void voice_deactivate(struct pcm_samp_engine_s *eng, int slot)
{
  struct pcm_samp_voice_s *v;

  if (!eng || slot < 0 || slot >= PCM_SAMP_MAX_VOICES)
    {
      return;
    }
  v = &eng->voices[slot];
  if (!v->active && v->legato_role == PCM_SAMP_LEGATO_NONE)
    {
      return;
    }
  v->active = false;
  v->pp_gen++;
  if (v->pp_gen == 0)
    {
      v->pp_gen = 1;
    }
  voice_half_reset(v);
  v->legato_role = PCM_SAMP_LEGATO_NONE;
  v->legato_pair = -1;
  v->legato_xfade_len = 0;
  v->legato_xfade_pos = 0;
  v->legato_new_fade_len = 0;
  v->legato_new_fade_pos = 0;
}

/* Current dual-source blend weights for an in-flight MAIN voice. */
static void legato_blend_scales(const struct pcm_samp_voice_s *main_v,
                                int32_t *new_scale_out, int32_t *g_new_out,
                                int32_t *g_old_out)
{
  int32_t new_scale = 32767;
  int32_t g_new = 32767;
  int32_t g_old = 0;

  if (main_v->legato_new_fade_len != 0 &&
      main_v->legato_new_fade_pos < main_v->legato_new_fade_len)
    {
      uint32_t np = main_v->legato_new_fade_pos;
      uint32_t nl = main_v->legato_new_fade_len;
      int32_t nt = nl > 1u ? (int32_t)((np * 32767u) / (nl - 1u)) : 32767;
      new_scale = (int32_t)(((int64_t)nt * nt) / 32767);
    }
  if (main_v->legato_xfade_len != 0 &&
      main_v->legato_xfade_pos < main_v->legato_xfade_len)
    {
      uint32_t xp = main_v->legato_xfade_pos;
      uint32_t xl = main_v->legato_xfade_len;
      int32_t t = xl > 1u ? (int32_t)((xp * 32767u) / (xl - 1u)) : 32767;
      g_new = (int32_t)isqrt_u32((uint32_t)t * 32767u);
      g_old = (int32_t)isqrt_u32((uint32_t)(32767 - t) * 32767u);
    }
  if (new_scale_out)
    {
      *new_scale_out = new_scale;
    }
  if (g_new_out)
    {
      *g_new_out = g_new;
    }
  if (g_old_out)
    {
      *g_old_out = g_old;
    }
}

/* Detach Acoustic TAIL from MAIN and let it fade as a normal voice.
 * Starts TAIL at the live g_old weight; MAIN continues from new_scale*g_new
 * via attack_q15 so the blend does not jump to full level.
 */
static void legato_detach_tail_fade(struct pcm_samp_engine_s *eng,
                                    struct pcm_samp_voice_s *main_v,
                                    uint32_t ms)
{
  int tail;
  struct pcm_samp_voice_s *tv;
  int32_t new_scale;
  int32_t g_new;
  int32_t g_old;
  int32_t cont;
  uint32_t rem;

  if (!eng || !main_v || main_v->legato_role != PCM_SAMP_LEGATO_MAIN)
    {
      return;
    }
  legato_blend_scales(main_v, &new_scale, &g_new, &g_old);
  cont = (int32_t)(((int64_t)new_scale * g_new) >> 15);
  if (cont < 1)
    {
      cont = 1;
    }
  if (cont > 32767)
    {
      cont = 32767;
    }

  tail = main_v->legato_pair;
  main_v->legato_pair = -1;
  main_v->legato_xfade_len = 0;
  main_v->legato_xfade_pos = 0;
  main_v->legato_new_fade_len = 0;
  main_v->legato_new_fade_pos = 0;
  main_v->legato_role = PCM_SAMP_LEGATO_NONE;
  /* Continue the interrupted fade-in as a short attack ramp. */
  if (cont < 32767)
    {
      rem = (eng->out_rate * (ms ? ms : PCM_SAMP_TAIL_DETACH_MS)) / 1000u;
      if (rem < 1u)
        {
          rem = 1u;
        }
      main_v->attack_q15 = cont;
      main_v->attack_inc = (32767 - cont) / (int32_t)rem;
      if (main_v->attack_inc < 1)
        {
          main_v->attack_inc = 1;
        }
    }

  if (tail < 0 || tail >= PCM_SAMP_MAX_VOICES)
    {
      return;
    }
  tv = &eng->voices[tail];
  if (!tv->active)
    {
      return;
    }
  tv->legato_role = PCM_SAMP_LEGATO_NONE;
  tv->legato_pair = -1;
  tv->note = -1;
  tv->gain_q15 = main_v->gain_q15;
  tv->env1 = main_v->env1;
  /* Hand off at the old-source contribution, not full MAIN level. */
  tv->env1.level = (int32_t)(((int64_t)tv->env1.level * g_old) >> 15);
  env_force_fade_ms(&tv->env1, eng->out_rate, ms);
  env_force_fade_ms(&tv->env2, eng->out_rate, ms);
  env_force_fade_ms(&tv->env3, eng->out_rate, ms);
}

/* Fade a voice out in place (keeps slot active until env hits idle). */
static void voice_soft_kill(struct pcm_samp_engine_s *eng, int slot,
                            uint32_t ms)
{
  struct pcm_samp_voice_s *v;

  if (!eng || slot < 0 || slot >= PCM_SAMP_MAX_VOICES)
    {
      return;
    }
  v = &eng->voices[slot];
  if (!v->active)
    {
      return;
    }
  if (v->legato_role == PCM_SAMP_LEGATO_MAIN && v->legato_pair >= 0)
    {
      legato_detach_tail_fade(eng, v, ms);
    }
  else if (v->legato_role == PCM_SAMP_LEGATO_TAIL && v->legato_pair >= 0)
    {
      /* MAIN still owns the pair; clear MAIN's link then fade this TAIL. */
      struct pcm_samp_voice_s *mv = &eng->voices[v->legato_pair];
      if (mv->legato_pair == slot)
        {
          mv->legato_pair = -1;
          mv->legato_role = PCM_SAMP_LEGATO_NONE;
          mv->legato_xfade_len = 0;
        }
      v->legato_role = PCM_SAMP_LEGATO_NONE;
      v->legato_pair = -1;
    }
  else
    {
      v->legato_role = PCM_SAMP_LEGATO_NONE;
      v->legato_pair = -1;
    }
  v->note = -1;
  env_force_fade_ms(&v->env1, eng->out_rate, ms);
  env_force_fade_ms(&v->env2, eng->out_rate, ms);
  env_force_fade_ms(&v->env3, eng->out_rate, ms);
}

static void voice_deactivate_with_pair(struct pcm_samp_engine_s *eng, int slot)
{
  struct pcm_samp_voice_s *v;
  int pair;

  if (!eng || slot < 0 || slot >= PCM_SAMP_MAX_VOICES)
    {
      return;
    }
  v = &eng->voices[slot];
  pair = v->legato_pair;
  if (pair >= 0 && pair < PCM_SAMP_MAX_VOICES && pair != slot)
    {
      eng->voices[pair].legato_pair = -1;
      eng->voices[pair].legato_role = PCM_SAMP_LEGATO_NONE;
      voice_deactivate(eng, pair);
    }
  voice_deactivate(eng, slot);
}

static int alloc_voice_excluding(struct pcm_samp_engine_s *eng, bool *stolen,
                                 int exclude)
{
  int i;
  int best = -1;
  int quiet = -1;
  int32_t score = INT32_MAX;
  int32_t quiet_level = INT32_MAX;
  *stolen = false;
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      if (i == exclude)
        {
          continue;
        }
      if (!eng->voices[i].active)
        {
          return i;
        }
    }

  /* Prefer hard-replacing a nearly-silent / releasing voice (inaudible). */
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *v = &eng->voices[i];
      int32_t lvl;
      if (i == exclude || !v->active)
        {
          continue;
        }
      if (v->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue; /* owned by MAIN; steal MAIN instead */
        }
      lvl = v->env1.level;
      if (lvl < 0)
        {
          lvl = 0;
        }
      if (lvl < PCM_SAMP_STEAL_QUIET ||
          (v->env1.phase == PCM_ENV_RELEASE &&
           lvl < (PCM_SAMP_STEAL_QUIET * 2)))
        {
          if (lvl < quiet_level)
            {
              quiet_level = lvl;
              quiet = i;
            }
        }
    }
  if (quiet >= 0)
    {
      *stolen = true;
      eng->steals++;
      if (eng->steals <= 24u || (eng->steals % 8u) == 0u)
        {
          struct pcm_samp_voice_s *sv = &eng->voices[quiet];
          syslog(LOG_NOTICE,
                 "pcm_diag: STEAL(quiet) slot=%d old_note=%d lvl=%ld "
                 "active=%d steals=%lu\n",
                 quiet, (int)sv->note, (long)sv->env1.level,
                 eng->active_count, (unsigned long)eng->steals);
        }
      voice_deactivate_with_pair(eng, quiet);
      eng->voices[quiet].active = false;
      return quiet;
    }

  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *v = &eng->voices[i];
      int32_t s;
      if (i == exclude)
        {
          continue;
        }
      if (v->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue;
        }
      s = v->env1.level;
      if (v->legato_role != PCM_SAMP_LEGATO_NONE)
        {
          /* Prefer not to break an in-flight Acoustic crossfade. */
          s += 80000;
        }
      if (v->env1.phase == PCM_ENV_RELEASE)
        {
          s -= 20000;
        }
      else if (v->env1.phase == PCM_ENV_RUN &&
               v->env1.seg < (uint8_t)v->env1.sustain)
        {
          s += 100000;
        }
      else
        {
          s -= (int32_t)((v->age > 200000 ? 200000 : v->age) / 64u);
        }
      if (s < score)
        {
          score = s;
          best = i;
        }
    }
  if (best < 0)
    {
      best = (exclude == 0) ? 1 : 0;
    }
  *stolen = true;
  eng->steals++;
  if (eng->steals <= 24u || (eng->steals % 8u) == 0u)
    {
      struct pcm_samp_voice_s *sv = &eng->voices[best];
      syslog(LOG_NOTICE,
             "pcm_diag: STEAL slot=%d old_note=%d age=%lu active=%d q=%u "
             "steals=%lu\n",
             best, (int)sv->note, (unsigned long)sv->age,
             eng->active_count, pcm_qdepth(eng),
             (unsigned long)eng->steals);
    }
  /* Last resort: must free the slot. Soft-kill cannot free under full poly;
   * new note already gets a stolen soft-attack ramp.
   */
  voice_deactivate_with_pair(eng, best);
  eng->voices[best].active = false;
  return best;
}

static int alloc_voice(struct pcm_samp_engine_s *eng, bool *stolen)
{
  return alloc_voice_excluding(eng, stolen, -1);
}

static int64_t channel_bend_cents(const struct pcm_samp_engine_s *eng)
{
  int32_t bend = (int32_t)eng->pitch_bend - 8192;
  return ((int64_t)bend * (int64_t)eng->pitch_bend_range_cents) / 8192;
}

/* Rebuild playback rate from base + per-voice bend (+ optional artic pitch mod). */
static void voice_apply_rate(struct pcm_samp_engine_s *eng,
                             struct pcm_samp_voice_s *v)
{
  int64_t cents = (int64_t)v->bend_cents;
  double ratio;

  (void)eng;
  if (v->mod_flags & MOD_PITCH)
    {
      cents += ((int64_t)v->env3_amt_q15 * v->env3.level * 1200) /
               (32767ll * 32767ll);
      cents += ((int64_t)v->lfo1_amt_q15 * v->lfo1.value * 1200) /
               (32767ll * 32767ll);
    }

  /* ±2 octaves is plenty for slides; keeps pow() sane. */
  if (cents > 2400)
    {
      cents = 2400;
    }
  if (cents < -2400)
    {
      cents = -2400;
    }

  if (cents == 0)
    {
      v->rate_inc = v->rate_inc_base;
    }
  else
    {
      ratio = pow(2.0, (double)cents / 1200.0);
      v->rate_inc = (uint64_t)((double)v->rate_inc_base * ratio);
    }
  v->aa_a_q15 = aa_coeff(v->rate_inc);
}

static int read_tables(struct pcm_samp_engine_s *eng)
{
  size_t ni = eng->header.instrument_count;
  size_t nr = eng->header.region_count;
  size_t ns = eng->header.sample_count;
  size_t na = eng->header.artic_count;

  eng->instruments = calloc(ni ? ni : 1, sizeof(*eng->instruments));
  eng->regions = calloc(nr ? nr : 1, sizeof(*eng->regions));
  eng->samples = calloc(ns ? ns : 1, sizeof(*eng->samples));
  eng->artics = calloc(na ? na : 1, sizeof(*eng->artics));
  if (!eng->instruments || !eng->regions || !eng->samples || !eng->artics)
    {
      return -ENOMEM;
    }

#define READ_TABLE(off, ptr, count) \
  do { \
    size_t _bytes = (size_t)(count) * sizeof(*(ptr)); \
    if (_bytes && source_read_at(eng, (uint64_t)(off), (ptr), _bytes) < 0) \
      { \
        return -EIO; \
      } \
  } while (0)
  READ_TABLE(eng->header.off_instruments, eng->instruments, ni);
  READ_TABLE(eng->header.off_regions, eng->regions, nr);
  READ_TABLE(eng->header.off_samples, eng->samples, ns);
  READ_TABLE(eng->header.off_artics, eng->artics, na);
#undef READ_TABLE
  return 0;
}


struct __attribute__((packed)) pcm_phead_header_s
{
  char magic[4];                    /* "PH01" */
  uint32_t head_frames;
  uint16_t sample_count;
  uint16_t sample_bytes;
  uint32_t pcm_bytes;
  uint32_t sample_rate;
  uint32_t head_bytes;
};

_Static_assert(sizeof(struct pcm_phead_header_s) == 24, "PHEAD header");

static bool phead_path(const char *bank_path, char *out, size_t out_size)
{
  const char *slash;
  const char *dot;
  size_t stem;

  if (!bank_path || !out || out_size == 0)
    {
      return false;
    }
  slash = strrchr(bank_path, '/');
  dot = strrchr(bank_path, '.');
  if (dot && slash && dot < slash)
    {
      dot = NULL;
    }
  stem = dot ? (size_t)(dot - bank_path) : strlen(bank_path);
  if (stem + sizeof(".phead") > out_size)
    {
      return false;
    }
  memcpy(out, bank_path, stem);
  memcpy(out + stem, ".phead", sizeof(".phead"));
  return true;
}

/* P4-style ahead cache: header + lengths + all fixed-stride heads. */
static bool phead_try_load(struct pcm_samp_engine_s *eng,
                           const char *bank_path, size_t head_bytes)
{
  struct pcm_phead_header_s hdr;
  char path[PATH_MAX];
  FILE *fp;
  uint32_t i;
  uint32_t rate = eng->header.pcm_rate ? eng->header.pcm_rate : 44100;
  size_t lens_bytes = (size_t)eng->header.sample_count * sizeof(uint32_t);
  bool ok = false;

  if (!phead_path(bank_path, path, sizeof(path)))
    {
      return false;
    }
  fp = fopen(path, "rb");
  if (!fp)
    {
      syslog(LOG_INFO, "pcm_samp: no PHEAD, fallback to DBNK seeks: %s\n",
             path);
      return false;
    }
  if (fread(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr) &&
      memcmp(hdr.magic, "PH01", 4) == 0 &&
      hdr.head_frames == eng->head_frames &&
      hdr.sample_count == eng->header.sample_count &&
      hdr.sample_bytes == sizeof(int16_t) &&
      hdr.pcm_bytes == eng->header.pcm_bytes &&
      hdr.sample_rate == rate &&
      hdr.head_bytes == head_bytes &&
      fread(eng->head_lens, 1, lens_bytes, fp) == lens_bytes &&
      fread(eng->heads, 1, head_bytes, fp) == head_bytes)
    {
      ok = true;
      for (i = 0; i < eng->header.sample_count; i++)
        {
          if (eng->head_lens[i] > eng->head_frames ||
              eng->head_lens[i] > eng->samples[i].frames)
            {
              ok = false;
              break;
            }
        }
    }
  fclose(fp);
  if (ok)
    {
      syslog(LOG_NOTICE,
             "pcm_samp: PHEAD HIT %s, sequential %lu KiB\n",
             path, (unsigned long)(head_bytes / 1024u));
    }
  else
    {
      syslog(LOG_WARNING,
             "pcm_samp: PHEAD stale/invalid, fallback to DBNK: %s\n", path);
    }
  return ok;
}

/* Preload first 60 ms of every sample into contiguous RAM (44100 target). */
static int preload_sample_heads(struct pcm_samp_engine_s *eng,
                                const char *bank_path)
{
  uint32_t i;
  uint32_t rate = eng->header.pcm_rate ? eng->header.pcm_rate : 44100;
  uint32_t head_frames;
  size_t bytes;
  uint32_t loaded = 0;
  uint64_t total_bytes = 0;

  if (rate != 44100)
    {
      syslog(LOG_WARNING,
             "pcm_samp: bank rate=%lu (head still %u frames / %u B; design target 44100)\n",
             (unsigned long)rate, PCM_SAMP_HEAD_FRAMES, PCM_SAMP_HEAD_BYTES);
    }

  /* Fixed 8 KiB head (matches stream half), independent of bank rate. */
  head_frames = PCM_SAMP_HEAD_FRAMES;
  if (head_frames < 1u)
    {
      head_frames = 1u;
    }
  eng->head_frames = head_frames;
  bytes = (size_t)eng->header.sample_count * (size_t)head_frames *
          sizeof(int16_t);
  eng->heads = (int16_t *)malloc(bytes);
  eng->head_lens = (uint32_t *)calloc(eng->header.sample_count,
                                      sizeof(uint32_t));
  if (!eng->heads || !eng->head_lens)
    {
      return -ENOMEM;
    }
  memset(eng->heads, 0, bytes);
  if (!eng->raw_active && phead_try_load(eng, bank_path, bytes))
    {
      return 0;
    }
  memset(eng->head_lens, 0,
         (size_t)eng->header.sample_count * sizeof(*eng->head_lens));

  for (i = 0; i < eng->header.sample_count; i++)
    {
      const struct pcm_samp_sample_s *s = &eng->samples[i];
      uint32_t n = head_frames;
      size_t got;
      uint64_t seek_off;

      if (s->frames < 1u)
        {
          continue;
        }
      if (n > s->frames)
        {
          n = s->frames;
        }
      if (s->pcm_offset + n * 2u > eng->header.pcm_bytes)
        {
          continue;
        }
      seek_off = (uint64_t)eng->header.off_pcm + s->pcm_offset;
      if (source_read_at(eng, seek_off,
                         eng->heads + (size_t)i * head_frames,
                         (size_t)n * sizeof(int16_t)) < 0)
        {
          atomic_fetch_add_explicit(&eng->io_errors, 1, memory_order_relaxed);
          continue;
        }
      got = n;
      eng->head_lens[i] = (uint32_t)got;
      if (got > 0)
        {
          loaded++;
          total_bytes += got * 2u;
        }
    }

  syslog(LOG_NOTICE,
         "pcm_samp: heads %u/%u samples, %u frames (~%u ms @%lu, %u KiB/smp), %lu KiB\n",
         loaded, eng->header.sample_count, head_frames,
         (unsigned)((head_frames * 1000u) / (rate ? rate : 1u)),
         (unsigned long)rate, (unsigned)(PCM_SAMP_HEAD_BYTES / 1024u),
         (unsigned long)(total_bytes / 1024u));
  return 0;
}

int pcm_samp_open(struct pcm_samp_engine_s *eng, const char *path,
                  uint32_t out_rate, size_t cache_bytes)
{
  uint32_t i;
  int ret;
  (void)cache_bytes; /* fixed voice_buf pool; arg kept for API compat */

  memset(eng, 0, sizeof(*eng));
  eng->raw_fd = -1;
  eng->rr_state = 0x6d2b79f5u;
  memset(eng->rr_last_sample, 0xff, sizeof(eng->rr_last_sample));
  if (strncmp(path, "/dev/", 5) == 0)
    {
      ret = raw_open_direct(eng, path);
      /* MBR boards sometimes never register /dev/sdcard0; whole-disk
       * /dev/mmcsd0 still has the DBNK (often at the first partition). */
      if (ret < 0 && strcmp(path, "/dev/sdcard0") == 0)
        {
          syslog(LOG_WARNING,
                 "pcm_samp: %s open %d, fallback /dev/mmcsd0\n", path, ret);
          ret = raw_open_direct(eng, "/dev/mmcsd0");
        }
      if (ret < 0)
        {
          return ret;
        }
    }
  else
    {
      eng->fp = fopen(path, "rb");
      if (!eng->fp)
        {
          return -errno;
        }
    }
  if (source_read_at(eng, 0, &eng->header, sizeof(eng->header)) < 0)
    {
      ret = -EIO;
      goto fail;
    }
  if (memcmp(eng->header.magic, "DBNK", 4) != 0 ||
      eng->header.version != 2 || !(eng->header.flags & 2u) ||
      eng->header.pcm_bytes < 2)
    {
      ret = -EINVAL;
      goto fail;
    }
  /* Bank page_size is PCM packing alignment (often 4 KiB).  Runtime I/O
   * uses PCM_SAMP_HALF_SIZE (8 KiB); accept any power-of-two >= 512.
   */
  if (eng->header.page_size < 512u ||
      (eng->header.page_size & (eng->header.page_size - 1u)) != 0)
    {
      ret = -EINVAL;
      goto fail;
    }
  ret = read_tables(eng);
  if (ret < 0)
    {
      goto fail;
    }
  log_mallinfo("before-voice-pool");
  syslog(LOG_NOTICE, "pcm_samp: allocating voice_buf=%u x %uKiB (align=%u)\n",
         PCM_SAMP_MAX_VOICES, (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u),
         64u);
  /* 64B align so each 8 KiB half can be an SDMMC DMA target. */
  eng->voice_pool = (uint8_t *)memalign(64, PCM_SAMP_VOICE_POOL);
  if (!eng->voice_pool)
    {
      ret = -ENOMEM;
      goto fail;
    }
  memset(eng->voice_pool, 0, PCM_SAMP_VOICE_POOL);
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      eng->voices[i].pp_mem = eng->voice_pool + i * PCM_SAMP_VOICE_BYTES;
      eng->voices[i].legato_pair = -1;
      eng->voices[i].legato_role = PCM_SAMP_LEGATO_NONE;
      voice_half_reset(&eng->voices[i]);
    }
  log_mallinfo("after-voice-pool");

  atomic_init(&eng->req_head, 0);
  atomic_init(&eng->req_tail, 0);
  atomic_init(&eng->stop_worker, false);
  atomic_init(&eng->cache_hits, 0);
  atomic_init(&eng->cache_misses, 0);
  atomic_init(&eng->underruns, 0);
  atomic_init(&eng->io_errors, 0);
  atomic_init(&eng->fills, 0);
  atomic_init(&eng->diag_req_drop, 0);
  atomic_init(&eng->diag_fill_stale, 0);
  atomic_init(&eng->diag_slow_fill, 0);
  atomic_init(&eng->diag_slow_render, 0);
  atomic_init(&eng->diag_q_peak, 0);
  atomic_init(&eng->diag_fill_us_max, 0);
  atomic_init(&eng->diag_fill_lat_max, 0);
  atomic_init(&eng->diag_render_us_max, 0);
  atomic_init(&eng->diag_underrun_logs, 0);
  atomic_init(&eng->sample_start_ms, 0);
  atomic_init(&eng->env_atk_cc[0], 64); /* CC70 Env1 Attack */
  atomic_init(&eng->env_atk_cc[1], 64); /* CC71 Env2 Attack */
  atomic_init(&eng->env_atk_cc[2], 64); /* CC72 Env3 Attack */
  atomic_init(&eng->vel_atk_amt, 0); /* CC73; off until host enables */
  atomic_init(&eng->vel_spoint_amt, 0); /* CC74; off until host enables */
  atomic_init(&eng->gt_res_amount, 0); /* CC28; off until host enables */
  atomic_init(&eng->xdi_mod_enable, false); /* CC29; preserve legacy tone */
  eng->diag_fill_us_sum = 0;
  eng->diag_render_us_sum = 0;
  eng->diag_render_frames = 0;
  eng->out_rate = out_rate ? out_rate : 44100;
  eng->master_q15 = 17000; /* after dB velocity curve; was 12000 when all notes ≈ full */
  eng->pitch_bend = 8192;           /* MIDI center */
  eng->pitch_bend_range_cents = 1200; /* ±12 semitones for guitar slides */
  eng->bus_peak_env = 0;
  eng->start_fade_remain = 0;
  eng->start_fade_total = 0;
  fx_pool_init();
  xmos_fx_init();
  syslog(LOG_NOTICE,
         "pcm_samp: bus dry (legacy EQ/Body/Comp/Room/Gate removed); "
         "XMOS FX + soft_bus16 only (fx_pool used=%lu KiB free=%lu KiB)\n",
         (unsigned long)(fx_pool_used() / 1024u),
         (unsigned long)(fx_pool_free() / 1024u));
  eng->quality_level = 0;
  eng->quality_relax_blocks = 0;
  eng->quality_changes = 0;
  eng->last_render_us = 0;

  ret = preload_sample_heads(eng, path);
  if (ret < 0)
    {
      goto fail;
    }
  log_mallinfo("after-heads");

  /* A regular bank starts via FatFs then switches to raw sectors.  A block
   * device path is already a raw-only bank and needs no mount or mapping.
   */
  if (!eng->raw_active)
    {
      ret = pcm_switch_to_raw(eng);
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "pcm_samp: raw switch failed %d, reopening FatFs path\n",
                 ret);
          eng->fp = fopen(path, "rb");
          if (!eng->fp)
            {
              ret = -errno;
              goto fail;
            }
          eng->raw_active = false;
        }
    }

  if (!g_sine_ready)
    {
      for (i = 0; i < 256; i++)
        {
          g_sine_q15[i] = (int16_t)(sin(2.0 * M_PI * i / 256.0) *
                                    32767.0);
        }
      g_sine_ready = true;
    }

  ret = pcm_start_fill_worker(eng);
  if (ret < 0)
    {
      goto fail;
    }
#if PCM_SAMP_DUAL_VOICE_MIX
  ret = pcm_start_mix_helper(eng);
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "pcm_samp: mix helper start failed %d (single-core mix)\n",
             ret);
    }
#else
  eng->mix_started = false;
#endif
  syslog(LOG_NOTICE,
         "pcm_samp: DBNK v2 inst=%u reg=%u smp=%u artic=%u pcm=%lu "
         "voice_buf=%ux%uKiB heads=%ufr io=%s dual_mix=%d "
         "affinity=mixCPU0/fillCPU1\n",
         eng->header.instrument_count, eng->header.region_count,
         eng->header.sample_count, eng->header.artic_count,
         (unsigned long)eng->header.pcm_bytes,
         PCM_SAMP_MAX_VOICES,
         (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u), eng->head_frames,
         eng->raw_active ? "raw" : "fatfs",
         (int)eng->mix_started);
  return 0;

fail:
  pcm_samp_close(eng);
  return ret;
}

void pcm_samp_close(struct pcm_samp_engine_s *eng)
{
  if (!eng)
    {
      return;
    }
  atomic_store_explicit(&eng->stop_worker, true, memory_order_release);
  if (eng->worker_wake_inited)
    {
      sem_post(&eng->worker_wake);
    }
  if (eng->mix_started)
    {
      sem_post(&eng->mix_go);
      pthread_join(eng->mix_thread, NULL);
      sem_destroy(&eng->mix_go);
      sem_destroy(&eng->mix_done);
      eng->mix_started = false;
    }
  if (eng->worker_started)
    {
      pthread_join(eng->worker, NULL);
      eng->worker_started = false;
    }
  if (eng->worker_wake_inited)
    {
      sem_destroy(&eng->worker_wake);
      eng->worker_wake_inited = false;
    }
  if (eng->fp)
    {
      fclose(eng->fp);
      eng->fp = NULL;
    }
  if (eng->raw_fd >= 0)
    {
      close(eng->raw_fd);
      eng->raw_fd = -1;
    }
  free(eng->raw_clba);
  free(eng->raw_bounce);
  if (eng->raw_remount)
    {
      if (mount("/dev/sdcard0", "/sdcard", "fatfs", 0, NULL) != 0)
        {
          syslog(LOG_WARNING, "pcm_samp: remount /sdcard errno=%d\n", errno);
        }
      eng->raw_remount = false;
    }
  free(eng->instruments);
  free(eng->regions);
  free(eng->samples);
  free(eng->artics);
  free(eng->heads);
  free(eng->head_lens);
  free(eng->voice_pool);
  free(eng->mixbuf);
  free(eng->mixbuf_b);
  fx_pool_reset();
  memset(eng, 0, sizeof(*eng));
  eng->raw_fd = -1;
}

static uint32_t voice_sample_start(const struct pcm_samp_engine_s *eng,
                                   const struct pcm_samp_region_s *r,
                                   const struct pcm_samp_sample_s *s,
                                   uint32_t authored_offset, int velocity)
{
  uint32_t sample_rate;
  uint32_t extra_ms;
  int vel_amt;
  int vel;
  uint64_t frame;

  if (!eng || !r || !s || s->frames < 2u)
    {
      return 0;
    }

  sample_rate = s->sample_rate ? s->sample_rate : eng->header.pcm_rate;
  extra_ms = atomic_load_explicit(&eng->sample_start_ms,
                                  memory_order_relaxed);
  vel_amt = atomic_load_explicit(&eng->vel_spoint_amt, memory_order_relaxed);
  if (vel_amt > 0)
    {
      vel = velocity;
      if (vel < 0)
        {
          vel = 0;
        }
      if (vel > 127)
        {
          vel = 127;
        }
      /* Up to 55 ms extra at vel=127 when amount=100. */
      extra_ms += (uint32_t)((vel_amt * vel * 55) / (100 * 127));
      if (extra_ms > 55u)
        {
          extra_ms = 55u;
        }
    }
  frame = (uint64_t)r->play_start + authored_offset;
  frame += ((uint64_t)sample_rate * extra_ms) / 1000u;
  if (frame >= s->frames)
    {
      frame = s->frames - 2u;
    }
  return (uint32_t)frame;
}

int pcm_samp_note_on(struct pcm_samp_engine_s *eng, int instrument,
                     int note, int velocity)
{
  const struct pcm_samp_region_s *r;
  const struct pcm_samp_sample_s *s;
  const struct pcm_samp_artic_s *a = NULL;
  struct pcm_samp_voice_s *v;
  uint8_t *pp_mem;
  bool stolen;
  bool retrig = false;
  int slot;
  int i;
  double semis;
  double rate;
  uint32_t play_frame;
  uint32_t head_len;
  uint32_t stream_frame;

  if (!eng || !eng->voice_pool ||
      (!eng->raw_active && !eng->fp))
    {
      return -EINVAL;
    }
  if (velocity < 1)
    {
      velocity = 1;
    }
  if (velocity > 127)
    {
      velocity = 127;
    }
  r = resolve_region(eng, instrument, note, velocity, true);
  if (!r || r->sample_id >= eng->header.sample_count)
    {
      return -ENOENT;
    }
  s = &eng->samples[r->sample_id];
  if (s->frames < 2 || s->pcm_offset + s->frames * 2u >
      eng->header.pcm_bytes)
    {
      return -EINVAL;
    }
  if (r->artic_id < eng->header.artic_count)
    {
      a = &eng->artics[r->artic_id];
    }

  /* Same MIDI note still sounding (or releasing): duck it before the new
   * pluck so mute→retrigger / fingerstyle repeats do not click.
   */
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *old = &eng->voices[i];
      if (!old->active || old->note != note)
        {
          continue;
        }
      if (old->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue;
        }
      if (instrument >= 0 && old->instrument_id != instrument)
        {
          continue;
        }
      if (old->legato_role == PCM_SAMP_LEGATO_MAIN && old->legato_pair >= 0)
        {
          legato_detach_tail_fade(eng, old, PCM_SAMP_TAIL_DETACH_MS);
        }
      env_force_fade_ms(&old->env1, eng->out_rate, PCM_SAMP_RETRIG_KILL_MS);
      env_force_fade_ms(&old->env2, eng->out_rate, PCM_SAMP_RETRIG_KILL_MS);
      env_force_fade_ms(&old->env3, eng->out_rate, PCM_SAMP_RETRIG_KILL_MS);
      old->note = -1; /* ignore further NoteOff / retrigger matches */
      retrig = true;
    }

  slot = alloc_voice(eng, &stolen);
  v = &eng->voices[slot];
  {
    uint32_t next_gen = v->pp_gen + 1u;
    if (next_gen == 0)
      {
        next_gen = 1u;
      }
    pp_mem = v->pp_mem;
    memset(v, 0, sizeof(*v));
    v->pp_mem = pp_mem;
    v->pp_gen = next_gen;
  }
  voice_half_reset(v);
  v->active = true;
  v->sample_id = r->sample_id;
  v->instrument_id = instrument;
  v->note = note;
  v->velocity = velocity;
  v->frames = s->frames;
  v->legato_role = PCM_SAMP_LEGATO_NONE;
  v->legato_pair = -1;
  v->is_resonance = 0;
  v->res_src_note = -1;
  play_frame = voice_sample_start(eng, r, s, 0, velocity);
  v->pos = (uint64_t)play_frame << 32;
  semis = note - (int)r->unity_note + r->coarse_tune +
          (double)r->fine_tune_cents / 100.0;
  rate = ((double)(s->sample_rate ? s->sample_rate :
                   eng->header.pcm_rate) / eng->out_rate) *
         pow(2.0, semis / 12.0);
  if (rate < 0.01)
    {
      rate = 0.01;
    }
  v->rate_inc = (uint64_t)(rate * 4294967296.0);
  v->rate_inc_base = v->rate_inc;
  v->bend_cents = 0; /* fresh pluck at written pitch; slides target newest voice */
  voice_apply_rate(eng, v);
  v->gain_q15 = (velocity_gain(a, velocity, note) * r->gain_q8) >> 8;
  if (v->gain_q15 > 32767)
    {
      v->gain_q15 = 32767;
    }
  env_load(&v->env1, a ? &a->env1 : NULL, eng->out_rate);
  env_apply_kbd(&v->env1, a, note, eng->out_rate);
  env_apply_attack_mods(eng, &v->env1, a ? a->env1.vel_scaling : 0,
                        velocity, 0);
  /* CC29 off preserves the existing bus-EQ tone.  When enabled, authored
   * XDI modulation is loaded; zeroed artics receive the trial preset.
   */
  voice_load_mod(eng, v, a);
  if (stolen || retrig)
    {
      uint32_t atk = stolen ? PCM_SAMP_ATTACK_SAMPLES : PCM_SAMP_RETRIG_ATTACK;
      if (atk < 1u)
        {
          atk = 1u;
        }
      v->attack_q15 = 0;
      v->attack_inc = 32767 / (int32_t)atk;
      if (v->attack_inc < 1)
        {
          v->attack_inc = 1;
        }
    }
  else
    {
      v->attack_q15 = 32767;
      v->attack_inc = 0;
    }
  rebuild_active(eng);

  /* Stream half0 from first frame after the resident head (or play_start). */
  head_len = (eng->head_lens && r->sample_id < eng->header.sample_count) ?
             eng->head_lens[r->sample_id] : 0;
  stream_frame = play_frame > head_len ? play_frame : head_len;
  if (stream_frame < s->frames)
    {
      v->cur_half = 0;
      voice_request_half(eng, (uint8_t)slot, 0,
                         s->pcm_offset + stream_frame * 2u);
    }
  return slot;
}

void pcm_samp_note_off(struct pcm_samp_engine_s *eng, int instrument, int note)
{
  pcm_samp_note_off_vel(eng, instrument, note, 64);
}

void pcm_samp_note_off_vel(struct pcm_samp_engine_s *eng, int instrument,
                           int note, int off_velocity)
{
  int i;
  int vel = off_velocity;

  if (!eng)
    {
      return;
    }
  if (vel < 1)
    {
      vel = 1;
    }
  if (vel > 127)
    {
      vel = 127;
    }
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *v = &eng->voices[i];
      if (!v->active)
        {
          continue;
        }
      /* Release the fretted note itself, or any open-string resonance it
       * excited (res_src_note).  TAIL voices stay owned by MAIN.
       */
      if (v->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue;
        }
      if (v->note == note)
        {
          if (instrument >= 0 && v->instrument_id != instrument)
            {
              continue;
            }
        }
      else if (v->is_resonance && v->res_src_note == note)
        {
          /* Resonance follows the exciting note's NoteOff. */
        }
      else
        {
          continue;
        }
      /* Ending fretted note: detach TAIL and fade it independently. */
      if (v->legato_role == PCM_SAMP_LEGATO_MAIN && v->legato_pair >= 0)
        {
          legato_detach_tail_fade(eng, v, PCM_SAMP_TAIL_DETACH_MS);
          rebuild_active(eng);
        }
      env_release_vel(&v->env1, eng->out_rate, vel);
      env_release_vel(&v->env2, eng->out_rate, vel);
      env_release_vel(&v->env3, eng->out_rate, vel);

      /* KeyOffVolModulation: hard mute ducks residual gain a bit. */
      if (vel > 64)
        {
          int32_t duck = 32767 - ((vel - 64) * 180);
          if (duck < 16000)
            {
              duck = 16000;
            }
          v->gain_q15 = (int32_t)(((int64_t)v->gain_q15 * duck) >> 15);
        }
    }
}

/* Integer sqrt for equal-power crossfade gains (0..~32767). */
static uint32_t isqrt_u32(uint32_t x)
{
  uint32_t root = 0;
  uint32_t bit = 1u << 30;

  while (bit > x)
    {
      bit >>= 2;
    }
  while (bit != 0)
    {
      if (x >= root + bit)
        {
          x -= root + bit;
          root = (root >> 1) + bit;
        }
      else
        {
          root >>= 1;
        }
      bit >>= 2;
    }
  return root;
}

int pcm_samp_legato(struct pcm_samp_engine_s *eng, int instrument,
                    int from_note, int to_note, int velocity)
{
  const struct pcm_samp_region_s *r;
  const struct pcm_samp_sample_s *s;
  const struct pcm_samp_artic_s *a = NULL;
  struct pcm_samp_voice_s *old_v = NULL;
  struct pcm_samp_voice_s *new_v;
  int old_slot = -1;
  int new_slot;
  int i;
  int delta;
  double ratio;
  double semis;
  double rate;
  uint32_t play_frame;
  uint32_t head_len;
  uint32_t stream_frame;
  uint32_t fade_samples;
  int sel_vel;
  int32_t keep_gain;
  bool stolen;
  uint8_t *pp_mem;
  struct pcm_samp_env_s env_keep;

  if (!eng || to_note < 0 || to_note > 127)
    {
      return -EINVAL;
    }
  if (velocity < 1)
    {
      velocity = 1;
    }
  if (velocity > 127)
    {
      velocity = 127;
    }

  /* Prefer the voice still sounding the fretted-from pitch on this string. */
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *cand = &eng->voices[i];
      if (!cand->active || cand->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue;
        }
      if (instrument >= 0 && cand->instrument_id != instrument)
        {
          continue;
        }
      if (from_note >= 0 && cand->note != from_note)
        {
          continue;
        }
      old_v = cand;
      old_slot = i;
      break;
    }

  /* Fallback: any active non-tail voice on the same string. */
  if (!old_v && from_note >= 0)
    {
      for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
        {
          struct pcm_samp_voice_s *cand = &eng->voices[i];
          if (!cand->active || cand->legato_role == PCM_SAMP_LEGATO_TAIL)
            {
              continue;
            }
          if (instrument >= 0 && cand->instrument_id != instrument)
            {
              continue;
            }
          old_v = cand;
          old_slot = i;
          break;
        }
    }

  if (!old_v)
    {
      return pcm_samp_note_on(eng, instrument, to_note, velocity);
    }

  /* If this voice is already MAIN in a crossfade, fade its old TAIL out. */
  if (old_v->legato_role == PCM_SAMP_LEGATO_MAIN &&
      old_v->legato_pair >= 0)
    {
      legato_detach_tail_fade(eng, old_v, PCM_SAMP_TAIL_DETACH_MS);
      rebuild_active(eng);
    }

  /* Acoustic hammer/pull: keep fretted-from velocity layer, no RR, keep gain,
   * and run old+new sample streams in parallel for the transition.
   */
  sel_vel = old_v->velocity;
  keep_gain = old_v->gain_q15;
  if (sel_vel < 1)
    {
      sel_vel = velocity;
    }
  if (sel_vel > 127)
    {
      sel_vel = 127;
    }

  r = resolve_region(eng, instrument, to_note, sel_vel, false);
  if (!r)
    {
      r = resolve_region(eng, instrument, to_note, velocity, false);
    }
  if (r && r->legato_start_offset &&
      r->sample_id < eng->header.sample_count)
    {
      s = &eng->samples[r->sample_id];
      if (r->artic_id < eng->header.artic_count)
        {
          a = &eng->artics[r->artic_id];
        }
      if (s->frames < 2 || s->pcm_offset + s->frames * 2u >
          eng->header.pcm_bytes)
        {
          return pcm_samp_note_on(eng, instrument, to_note, velocity);
        }

      play_frame = voice_sample_start(eng, r, s,
                                      r->legato_start_offset, sel_vel);

      fade_samples = (eng->out_rate * 8u) / 1000u;
      if (fade_samples < 1u)
        {
          fade_samples = 1u;
        }
      if (fade_samples > PCM_SAMP_LEGATO_XFADE_MAX)
        {
          fade_samples = PCM_SAMP_LEGATO_XFADE_MAX;
        }
      new_slot = alloc_voice_excluding(eng, &stolen, old_slot);
      new_v = &eng->voices[new_slot];
      {
        uint32_t next_gen = new_v->pp_gen + 1u;
        if (next_gen == 0)
          {
            next_gen = 1u;
          }
        pp_mem = new_v->pp_mem;
        env_keep = old_v->env1; /* preserve amp envelope continuity */
        memset(new_v, 0, sizeof(*new_v));
        new_v->pp_mem = pp_mem;
        new_v->pp_gen = next_gen;
      }
      voice_half_reset(new_v);
      new_v->active = true;
      new_v->sample_id = r->sample_id;
      new_v->instrument_id = (int16_t)instrument;
      new_v->note = (int16_t)to_note;
      new_v->velocity = (int16_t)sel_vel;
      new_v->frames = s->frames;
      new_v->pos = (uint64_t)play_frame << 32;
      new_v->env1 = env_keep;
      new_v->gain_q15 = keep_gain < 1 ? 1 : keep_gain;
      if (new_v->gain_q15 > 32767)
        {
          new_v->gain_q15 = 32767;
        }
      semis = to_note - (int)r->unity_note + r->coarse_tune +
              (double)r->fine_tune_cents / 100.0;
      rate = ((double)(s->sample_rate ? s->sample_rate :
                       eng->header.pcm_rate) / eng->out_rate) *
             pow(2.0, semis / 12.0);
      if (rate < 0.01)
        {
          rate = 0.01;
        }
      new_v->rate_inc = (uint64_t)(rate * 4294967296.0);
      new_v->rate_inc_base = new_v->rate_inc;
      new_v->bend_cents = 0;
      voice_load_mod(eng, new_v, a);
      voice_apply_rate(eng, new_v);
      new_v->attack_q15 = 32767;
      new_v->attack_inc = 0;
      new_v->legato_role = PCM_SAMP_LEGATO_MAIN;
      new_v->legato_pair = (int8_t)old_slot;
      new_v->legato_xfade_len = (uint16_t)fade_samples;
      new_v->legato_xfade_pos = 0;
      /* The equal-power xfade is the only MAIN fade; a second long
       * quadratic fade dulls the target sample and collapses stereo detail.
       */
      new_v->legato_new_fade_len = 0;
      new_v->legato_new_fade_pos = 0;

      /* Old voice keeps streaming as TAIL; hide it from MIDI note matching. */
      old_v->legato_role = PCM_SAMP_LEGATO_TAIL;
      old_v->legato_pair = (int8_t)new_slot;
      old_v->note = -1;
      old_v->legato_xfade_len = (uint16_t)fade_samples;
      old_v->legato_xfade_pos = 0;
      old_v->legato_new_fade_len = 0;
      old_v->legato_new_fade_pos = 0;

      head_len = (eng->head_lens &&
                  r->sample_id < eng->header.sample_count) ?
                 eng->head_lens[r->sample_id] : 0;
      stream_frame = play_frame > head_len ? play_frame : head_len;
      if (stream_frame < s->frames)
        {
          new_v->cur_half = 0;
          voice_request_half(eng, (uint8_t)new_slot, 0,
                             s->pcm_offset + stream_frame * 2u);
        }
      rebuild_active(eng);
      (void)stolen;
      return new_slot;
    }

  /* Keep sample position + amp envelope; only retune playback rate. */
  delta = to_note - (int)old_v->note;
  if (delta != 0)
    {
      ratio = pow(2.0, (double)delta / 12.0);
      old_v->rate_inc_base =
        (uint64_t)((double)old_v->rate_inc_base * ratio);
      if (old_v->rate_inc_base < 1ull)
        {
          old_v->rate_inc_base = 1ull;
        }
      old_v->note = (int16_t)to_note;
      old_v->bend_cents = 0;
      voice_apply_rate(eng, old_v);
    }

  return old_slot;
}

void pcm_samp_all_off(struct pcm_samp_engine_s *eng)
{
  int i;
  if (!eng)
    {
      return;
    }
  for (i = 0; i < PCM_SAMP_MAX_VOICES; i++)
    {
      struct pcm_samp_voice_s *v = &eng->voices[i];
      if (!v->active)
        {
          continue;
        }
      /* Soft panic: fade everything instead of hard memset kill. */
      voice_soft_kill(eng, i, PCM_SAMP_ALL_OFF_MS);
    }
  rebuild_active(eng);
  eng->pitch_bend = 8192;
}

void pcm_samp_set_pitch_bend(struct pcm_samp_engine_s *eng, uint16_t value14)
{
  int i;
  int best = -1;
  uint32_t best_age = UINT32_MAX;
  int64_t cents;

  if (!eng)
    {
      return;
    }
  if (value14 > 16383u)
    {
      value14 = 16383u;
    }
  eng->pitch_bend = value14;
  cents = channel_bend_cents(eng);

  /* Guitar-safe: bend only the newest sounding voice so bass/inners stay put. */
  for (i = 0; i < eng->active_count; i++)
    {
      struct pcm_samp_voice_s *v = &eng->voices[eng->active_list[i]];
      if (!v->active)
        {
          continue;
        }
      if (v->age <= best_age)
        {
          best_age = v->age;
          best = eng->active_list[i];
        }
    }

  if (value14 == 8192u)
    {
      for (i = 0; i < eng->active_count; i++)
        {
          struct pcm_samp_voice_s *v = &eng->voices[eng->active_list[i]];
          if (v->active && v->bend_cents != 0)
            {
              v->bend_cents = 0;
              voice_apply_rate(eng, v);
            }
        }
      return;
    }

  if (best >= 0)
    {
      struct pcm_samp_voice_s *v = &eng->voices[best];
      if (cents > 32767)
        {
          cents = 32767;
        }
      if (cents < -32768)
        {
          cents = -32768;
        }
      v->bend_cents = (int16_t)cents;
      voice_apply_rate(eng, v);
    }
}

void pcm_samp_set_sample_start_ms(struct pcm_samp_engine_s *eng,
                                  unsigned milliseconds)
{
  if (!eng)
    {
      return;
    }
  if (milliseconds > 55u)
    {
      milliseconds = 55u;
    }
  atomic_store_explicit(&eng->sample_start_ms, milliseconds,
                        memory_order_relaxed);
}

void pcm_samp_set_env_attack_cc(struct pcm_samp_engine_s *eng, int env_id,
                                int cc_0_127)
{
  if (!eng || env_id < 0 || env_id > 2)
    {
      return;
    }
  if (cc_0_127 < 0)
    {
      cc_0_127 = 0;
    }
  if (cc_0_127 > 127)
    {
      cc_0_127 = 127;
    }
  atomic_store_explicit(&eng->env_atk_cc[env_id], cc_0_127,
                        memory_order_relaxed);
}

void pcm_samp_set_vel_attack(struct pcm_samp_engine_s *eng, int amount_0_100)
{
  if (!eng)
    {
      return;
    }
  if (amount_0_100 < 0)
    {
      amount_0_100 = 0;
    }
  if (amount_0_100 > 100)
    {
      amount_0_100 = 100;
    }
  atomic_store_explicit(&eng->vel_atk_amt, amount_0_100,
                        memory_order_relaxed);
}

void pcm_samp_set_vel_spoint(struct pcm_samp_engine_s *eng, int amount_0_100)
{
  if (!eng)
    {
      return;
    }
  if (amount_0_100 < 0)
    {
      amount_0_100 = 0;
    }
  if (amount_0_100 > 100)
    {
      amount_0_100 = 100;
    }
  atomic_store_explicit(&eng->vel_spoint_amt, amount_0_100,
                        memory_order_relaxed);
}

void pcm_samp_set_gt_resonance(struct pcm_samp_engine_s *eng, int amount_0_100)
{
  if (!eng)
    {
      return;
    }
  if (amount_0_100 < 0)
    {
      amount_0_100 = 0;
    }
  if (amount_0_100 > 100)
    {
      amount_0_100 = 100;
    }
  atomic_store_explicit(&eng->gt_res_amount, amount_0_100,
                        memory_order_relaxed);
}

void pcm_samp_set_xdi_mod(struct pcm_samp_engine_s *eng, bool enable)
{
  if (!eng)
    {
      return;
    }
  /* Applies to newly started voices.  Existing voices keep their current
   * state so toggling the CC never produces a mid-note filter discontinuity.
   */
  atomic_store_explicit(&eng->xdi_mod_enable, enable,
                        memory_order_relaxed);
}

/* AeroSynth / LovableSynth Gt Body: harmonic open-string fan-out. */
void pcm_samp_open_resonance(struct pcm_samp_engine_s *eng, int played_note,
                             int played_velocity)
{
  static const int pool[6] = {40, 45, 50, 55, 59, 64}; /* E2..E4 */
  struct
  {
    int note;
    int vel;
  } hits[6];
  int hit_count = 0;
  int amount;
  int i;
  int j;
  uint8_t seen[128];
  uint32_t atk;

  if (!eng || played_note < 0 || played_note > 127)
    {
      return;
    }
  if (played_velocity < 40)
    {
      return; /* soft plucks do not wake the body */
    }
  amount = atomic_load_explicit(&eng->gt_res_amount, memory_order_relaxed);
  if (amount <= 0)
    {
      return;
    }
  if (amount > 100)
    {
      amount = 100;
    }

  memset(seen, 0, sizeof(seen));
  for (i = 0; i < 6; i++)
    {
      int cand = pool[i];
      int dist;
      int oct;
      int coef_q8;
      int vel;
      int already = 0;

      if (cand == played_note || cand < 0 || cand > 127 || seen[cand])
        {
          continue;
        }
      dist = played_note - cand;
      if (dist < 0)
        {
          dist = -dist;
        }
      switch (dist % 12)
        {
          case 0:
            coef_q8 = 256;
            break; /* unison/octave */
          case 7:
            coef_q8 = 166;
            break; /* fifth ~0.65 */
          case 5:
            coef_q8 = 128;
            break; /* fourth ~0.50 */
          case 4:
          case 3:
            coef_q8 = 90;
            break; /* thirds ~0.35 */
          default:
            continue;
        }
      oct = dist / 12;
      vel = (played_velocity * coef_q8 * amount) / (100 * 256);
      while (oct-- > 0)
        {
          vel = (vel * 154) / 256; /* ~0.6 per octave */
        }
      if (vel < 8)
        {
          continue;
        }
      if (vel > 42)
        {
          vel = 42; /* keep resonances in the background */
        }
      for (j = 0; j < PCM_SAMP_MAX_VOICES; j++)
        {
          struct pcm_samp_voice_s *v = &eng->voices[j];
          if (v->active && v->note == cand &&
              v->legato_role != PCM_SAMP_LEGATO_TAIL)
            {
              already = 1;
              break;
            }
        }
      if (already)
        {
          continue;
        }
      seen[cand] = 1;
      hits[hit_count].note = cand;
      hits[hit_count].vel = vel;
      hit_count++;
    }

  /* Prefer the strongest intervals; cap voice cost. */
  for (i = 0; i < hit_count; i++)
    {
      for (j = i + 1; j < hit_count; j++)
        {
          if (hits[j].vel > hits[i].vel)
            {
              int tn = hits[i].note;
              int tv = hits[i].vel;
              hits[i] = hits[j];
              hits[j].note = tn;
              hits[j].vel = tv;
            }
        }
    }
  if (hit_count > 3)
    {
      hit_count = 3;
    }

  atk = (eng->out_rate * 12u) / 1000u; /* soft body bloom, not a stacked pick */
  if (atk < 1u)
    {
      atk = 1u;
    }

  for (i = 0; i < hit_count; i++)
    {
      int inst;
      int slot;

      inst = pcm_samp_best_instrument(eng, hits[i].note, hits[i].vel);
      if (inst < 0)
        {
          continue;
        }
      slot = pcm_samp_note_on(eng, inst, hits[i].note, hits[i].vel);
      if (slot < 0 || slot >= PCM_SAMP_MAX_VOICES)
        {
          continue;
        }
      eng->voices[slot].is_resonance = 1;
      eng->voices[slot].res_src_note = (int16_t)played_note;
      eng->voices[slot].attack_q15 = 0;
      eng->voices[slot].attack_inc = 32767 / (int32_t)atk;
      if (eng->voices[slot].attack_inc < 1)
        {
          eng->voices[slot].attack_inc = 1;
        }
    }
}

void pcm_samp_arm_start_fade(struct pcm_samp_engine_s *eng)
{
  uint32_t samples;

  if (!eng || eng->out_rate == 0)
    {
      return;
    }

  /* ~350 ms linear ramp from digital mute to full bus gain. */
  samples = (eng->out_rate * 350u) / 1000u;
  if (samples < 64u)
    {
      samples = 64u;
    }
  eng->start_fade_total = samples;
  eng->start_fade_remain = samples;
  syslog(LOG_NOTICE, "pcm_samp: arm start fade %u samples (~350ms)\n",
         samples);
}

void pcm_samp_wait_start_fade(struct pcm_samp_engine_s *eng,
                              unsigned timeout_ms)
{
  unsigned waited = 0;

  if (!eng)
    {
      return;
    }

  while (eng->start_fade_remain > 0 && waited < timeout_ms)
    {
      usleep(5000);
      waited += 5u;
    }

  syslog(LOG_NOTICE,
         "pcm_samp: start fade %s after %ums (remain=%u)\n",
         eng->start_fade_remain == 0 ? "done" : "timeout",
         waited, eng->start_fade_remain);
}

/* Map sample frame -> contiguous int16 window (head or one ping-pong half).
 * No atomics in the common READY hit after the first acquire load.
 */
static bool voice_map_window(struct pcm_samp_engine_s *eng,
                             struct pcm_samp_voice_s *v,
                             const struct pcm_samp_sample_s *sample,
                             uint32_t frame, uint32_t head_len,
                             const int16_t **out_base, uint32_t *out_first,
                             uint32_t *out_count)
{
  unsigned h;
  uint32_t byte_off;

  if (frame < head_len && eng->heads)
    {
      *out_base = eng->heads + (size_t)v->sample_id * eng->head_frames;
      *out_first = 0;
      *out_count = head_len;
      return true;
    }

  byte_off = sample->pcm_offset + frame * 2u;
  for (h = 0; h < 2; h++)
    {
      struct pcm_samp_half_s *hh = &v->half[h];
      uint32_t st = atomic_load_explicit(&hh->state, memory_order_acquire);
      uint32_t nframes;

      if (st != PCM_HALF_READY || hh->gen != v->pp_gen ||
          byte_off < hh->pcm_off ||
          byte_off + 1u >= hh->pcm_off + hh->nbytes)
        {
          continue;
        }

      nframes = hh->nbytes >> 1;
      if (nframes == 0)
        {
          continue;
        }

      *out_base = (const int16_t *)(v->pp_mem + h * PCM_SAMP_HALF_SIZE);
      *out_first = (hh->pcm_off - sample->pcm_offset) >> 1;
      *out_count = nframes;
      if (h != v->cur_half)
        {
          v->cur_half = (uint8_t)h;
        }
      return true;
    }

  return false;
}

static bool fetch_voice_sample(struct pcm_samp_engine_s *eng,
                               uint8_t voice_idx,
                               const struct pcm_samp_sample_s *sample,
                               uint32_t frame, int16_t *value,
                               bool do_prefetch, uint32_t *hits,
                               uint32_t *misses)
{
  struct pcm_samp_voice_s *v;
  const int16_t *base;
  uint32_t first;
  uint32_t count;
  uint32_t head_len;
  uint32_t byte_off;

  if (!sample || !value || voice_idx >= PCM_SAMP_MAX_VOICES)
    {
      return false;
    }

  v = &eng->voices[voice_idx];
  head_len = (eng->head_lens && v->sample_id < eng->header.sample_count) ?
             eng->head_lens[v->sample_id] : 0;
  if (voice_map_window(eng, v, sample, frame, head_len, &base, &first,
                       &count) &&
      frame >= first && frame - first < count)
    {
      *value = base[frame - first];
      if (hits)
        {
          (*hits)++;
        }
      if (do_prefetch)
        {
          voice_maybe_prefetch(eng, voice_idx, sample, frame);
        }
      return true;
    }

  if (misses)
    {
      (*misses)++;
    }
  byte_off = sample->pcm_offset + frame * 2u;
  voice_request_cover(eng, voice_idx, byte_off);
  return false;
}

/* Mix active_list[v0..v1) into mixbuf. Each voice owned by one core only. */
static void mix_voices_range(struct pcm_samp_engine_s *eng, int32_t *mixbuf,
                             unsigned frames, int v0, int v1, bool dense,
                             bool fast, bool *dropped_out, uint32_t *under_out,
                             uint64_t *cubic_out, uint64_t *linear_out)
{
  int j;
  bool dropped = false;
  uint32_t under = 0;
  uint64_t cubic = 0;
  uint64_t linear = 0;
  uint32_t hits = 0;
  uint32_t misses = 0;
  int nvoices = eng->active_count;

  for (j = v0; j < v1; j++)
    {
      uint8_t vi = eng->active_list[j];
      struct pcm_samp_voice_s *v = &eng->voices[vi];
      const struct pcm_samp_sample_s *sm;
      uint32_t head_len;
      unsigned n;
      const int16_t *win = NULL;
      uint32_t win_first = 0;
      uint32_t win_count = 0;
      bool win_valid = false;

      if (!v->active)
        {
          continue;
        }
      /* TAIL is mixed by its MAIN voice on the same core (avoids dual-core
       * races on the shared crossfade counter).
       */
      if (v->legato_role == PCM_SAMP_LEGATO_TAIL)
        {
          continue;
        }
      sm = &eng->samples[v->sample_id];
      head_len = (eng->head_lens && v->sample_id < eng->header.sample_count) ?
                 eng->head_lens[v->sample_id] : 0;

      for (n = 0; n < frames; n++)
        {
          uint32_t i0;
          uint32_t frac;
          uint32_t end;
          uint32_t i1;
          int16_t y0;
          int16_t y1;
          int32_t value;
          int32_t env;
          int32_t gain;
          bool pref = !fast || ((n & 31u) == 0u);

          i0 = (uint32_t)(v->pos >> 32);
          end = (sm->loop_type && v->env1.phase != PCM_ENV_RELEASE &&
                 sm->loop_end > sm->loop_start + 1u) ?
                sm->loop_end : sm->frames;
          if (i0 >= end)
            {
              if (end == sm->loop_end)
                {
                  uint32_t looplen = sm->loop_end - sm->loop_start;
                  i0 = sm->loop_start + ((i0 - sm->loop_start) % looplen);
                  v->pos = ((uint64_t)i0 << 32) | (uint32_t)v->pos;
                  win_valid = false;
                }
              else
                {
                  v->active = false;
                  dropped = true;
                  break;
                }
            }
          frac = (uint32_t)v->pos;
          i1 = (i0 + 1u < sm->frames) ? (i0 + 1u) : i0;

          /* Fast window: contiguous int16* for y0 and preferably y1. */
          if (!win_valid || i0 < win_first || i0 - win_first >= win_count)
            {
              win_valid = voice_map_window(eng, v, sm, i0, head_len, &win,
                                           &win_first, &win_count);
              if (!win_valid)
                {
                  uint32_t byte_off = sm->pcm_offset + i0 * 2u;
                  misses++;
                  voice_request_cover(eng, vi, byte_off);
                  {
                    uint32_t und =
                      atomic_fetch_add_explicit(&eng->underruns, 1,
                                                memory_order_relaxed);
                    under++;
                    if (atomic_load_explicit(&eng->diag_underrun_logs,
                                             memory_order_relaxed) <
                          PCM_DIAG_UNDER_LOG_MAX &&
                        (und < 8u || (und % 16u) == 0u))
                      {
                        atomic_fetch_add_explicit(&eng->diag_underrun_logs, 1,
                                                  memory_order_relaxed);
                        syslog(LOG_WARNING,
                               "pcm_diag: UNDERRUN#%lu v=%u note=%d "
                               "frame=%lu q=%u active=%d\n",
                               (unsigned long)(und + 1u), vi, (int)v->note,
                               (unsigned long)i0, pcm_qdepth(eng), nvoices);
                      }
                  }
                  v->pos += v->rate_inc;
                  continue;
                }
            }

          y0 = win[i0 - win_first];
          if (i1 >= win_first && i1 - win_first < win_count)
            {
              y1 = win[i1 - win_first];
              hits += 2u;
            }
          else if (fetch_voice_sample(eng, vi, sm, i1, &y1, false, &hits,
                                      &misses))
            {
              hits++;
            }
          else
            {
              uint32_t und =
                atomic_fetch_add_explicit(&eng->underruns, 1,
                                          memory_order_relaxed);
              under++;
              if (atomic_load_explicit(&eng->diag_underrun_logs,
                                       memory_order_relaxed) <
                    PCM_DIAG_UNDER_LOG_MAX &&
                  (und < 8u || (und % 16u) == 0u))
                {
                  atomic_fetch_add_explicit(&eng->diag_underrun_logs, 1,
                                            memory_order_relaxed);
                  syslog(LOG_WARNING,
                         "pcm_diag: UNDERRUN#%lu v=%u note=%d frame=%lu "
                         "q=%u active=%d\n",
                         (unsigned long)(und + 1u), vi, (int)v->note,
                         (unsigned long)i0, pcm_qdepth(eng), nvoices);
                }
              v->pos += v->rate_inc;
              continue;
            }

          if (pref)
            {
              voice_maybe_prefetch(eng, vi, sm, i0);
            }

          if (!dense)
            {
              int16_t ym1;
              int16_t y2;
              if (i0 > 0 && i0 + 2u < sm->frames &&
                  fetch_voice_sample(eng, vi, sm, i0 - 1u, &ym1, false,
                                     &hits, &misses) &&
                  fetch_voice_sample(eng, vi, sm, i0 + 2u, &y2, false,
                                     &hits, &misses))
                {
                  value = hermite4(ym1, y0, y1, y2, frac);
                  cubic++;
                }
              else
                {
                  value = linear4(y0, y1, frac);
                  linear++;
                }
            }
          else
            {
              value = linear4(y0, y1, frac);
              linear++;
            }

          {
            int32_t new_scale = 32767;
            int32_t g_new = 32767;
            int32_t g_old = 0;
            bool in_xfade = false;
            int32_t tail_mix = 0;

            if (v->legato_role == PCM_SAMP_LEGATO_MAIN)
              {
                if (v->legato_new_fade_len != 0 &&
                    v->legato_new_fade_pos < v->legato_new_fade_len)
                  {
                    uint32_t np = v->legato_new_fade_pos;
                    uint32_t nl = v->legato_new_fade_len;
                    int32_t nt = nl > 1u ?
                                 (int32_t)((np * 32767u) / (nl - 1u)) : 32767;
                    new_scale = (int32_t)(((int64_t)nt * nt) / 32767);
                    v->legato_new_fade_pos++;
                  }
                if (v->legato_xfade_pos < v->legato_xfade_len)
                  {
                    uint32_t xp = v->legato_xfade_pos;
                    uint32_t xl = v->legato_xfade_len;
                    int32_t t = xl > 1u ?
                                (int32_t)((xp * 32767u) / (xl - 1u)) : 32767;
                    in_xfade = true;
                    g_new =
                      (int32_t)isqrt_u32((uint32_t)t * 32767u);
                    g_old = (int32_t)isqrt_u32((uint32_t)(32767 - t) * 32767u);
                    v->legato_xfade_pos++;
                  }

                /* Live-stream the old source in parallel with the new one. */
                if (in_xfade && v->legato_pair >= 0 &&
                    v->legato_pair < PCM_SAMP_MAX_VOICES)
                  {
                    struct pcm_samp_voice_s *tv =
                      &eng->voices[v->legato_pair];
                    if (tv->active &&
                        tv->legato_role == PCM_SAMP_LEGATO_TAIL &&
                        tv->sample_id < eng->header.sample_count)
                      {
                        const struct pcm_samp_sample_s *ts =
                          &eng->samples[tv->sample_id];
                        uint32_t ti0 = (uint32_t)(tv->pos >> 32);
                        uint32_t tfrac = (uint32_t)tv->pos;
                        uint32_t tend =
                          (ts->loop_type &&
                           tv->env1.phase != PCM_ENV_RELEASE &&
                           ts->loop_end > ts->loop_start + 1u) ?
                          ts->loop_end : ts->frames;
                        int16_t ty0 = 0;
                        int16_t ty1 = 0;
                        if (ti0 >= tend)
                          {
                            tv->active = false;
                          }
                        else if (fetch_voice_sample(eng,
                                                    (uint8_t)v->legato_pair,
                                                    ts, ti0, &ty0, false,
                                                    &hits, &misses))
                          {
                            uint32_t ti1 =
                              ti0 + 1u < ts->frames ? ti0 + 1u : ti0;
                            if (!fetch_voice_sample(eng,
                                                    (uint8_t)v->legato_pair,
                                                    ts, ti1, &ty1, false,
                                                    &hits, &misses))
                              {
                                ty1 = ty0;
                              }
                            tail_mix = linear4(ty0, ty1, tfrac);
                            tv->pos += tv->rate_inc;
                            if (tv->age != UINT32_MAX)
                              {
                                tv->age++;
                              }
                          }
                      }
                  }

                /* Xfade done: old source has reached zero, so it is safe
                 * to drop TAIL without changing the mixed level.
                 */
                if (v->legato_xfade_len != 0 &&
                    v->legato_xfade_pos >= v->legato_xfade_len &&
                    v->legato_pair >= 0)
                  {
                    int tail = v->legato_pair;
                    v->legato_pair = -1;
                    voice_deactivate(eng, tail);
                    dropped = true;
                  }
                if (v->legato_pair < 0 &&
                    (v->legato_new_fade_len == 0 ||
                     v->legato_new_fade_pos >= v->legato_new_fade_len))
                  {
                    v->legato_role = PCM_SAMP_LEGATO_NONE;
                    v->legato_xfade_len = 0;
                    v->legato_new_fade_len = 0;
                  }

                value = (int32_t)(((int64_t)value * new_scale) >> 15);
                if (in_xfade)
                  {
                    int32_t newv =
                      (int32_t)(((int64_t)value * v->gain_q15) >> 15);
                    int32_t oldv =
                      (int32_t)(((int64_t)tail_mix * v->gain_q15) >> 15);
                    newv = (int32_t)(((int64_t)newv * g_new) >> 15);
                    oldv = (int32_t)(((int64_t)oldv * g_old) >> 15);
                    value = newv + oldv;
                  }
              }

            /* Keep one-pole AA even on fast path (cheap, kills pitch fold). */
            if (v->aa_a_q15)
              {
                v->aa_z += (v->aa_a_q15 * (value - v->aa_z)) >> 15;
                value = v->aa_z;
              }
            else
              {
                v->aa_z = value;
              }

            if (!env_tick(&v->env1, eng->out_rate))
              {
                if (v->legato_pair >= 0)
                  {
                    voice_deactivate(eng, v->legato_pair);
                  }
                v->active = false;
                v->legato_role = PCM_SAMP_LEGATO_NONE;
                v->legato_pair = -1;
                dropped = true;
                break;
              }

            if (!fast)
              {
                /* LFO + filter/pitch coeffs at control rate; IIR stays
                 * audio-rate below. */
                if (v->mod_flags && ++v->ctrl_div >= PCM_SAMP_CTRL_PERIOD)
                  {
                    v->ctrl_div = 0;
                    if (v->mod_flags & MOD_FILTER)
                      {
                        env_tick_n(&v->env2, eng->out_rate,
                                   PCM_SAMP_CTRL_PERIOD);
                      }
                    if (v->mod_flags & MOD_PITCH)
                      {
                        env_tick_n(&v->env3, eng->out_rate,
                                   PCM_SAMP_CTRL_PERIOD);
                        lfo_tick_n(&v->lfo1, PCM_SAMP_CTRL_PERIOD);
                      }
                    if (v->mod_flags & (MOD_FILTER | MOD_AMP_LFO))
                      {
                        lfo_tick_n(&v->lfo2, PCM_SAMP_CTRL_PERIOD);
                      }
                    voice_apply_rate(eng, v);
                    filter_update(&v->filter, v->env2.level, v->lfo2.value);
                  }
                if (v->mod_flags & MOD_FILTER)
                  {
                    int32_t dry = value;
                    int32_t wet = filter_run(&v->filter, value);
                    value = dry +
                      (int32_t)(((int64_t)(wet - dry) *
                                 v->filter.mix_q15) >> 15);
                  }
              }

            env = v->env1.level;
            if (end == sm->frames && sm->frames - i0 < PCM_SAMP_END_FADE)
              {
                env = (env * (int32_t)(((sm->frames - i0) * 32767u) /
                                       PCM_SAMP_END_FADE)) >> 15;
              }
            if (v->attack_inc && v->attack_q15 < 32767)
              {
                v->attack_q15 += v->attack_inc;
                if (v->attack_q15 >= 32767)
                  {
                    v->attack_q15 = 32767;
                    v->attack_inc = 0;
                  }
              }
            env = (env * v->attack_q15) >> 15;
            /* During dual-source xfade, gain is already baked into value. */
            if (in_xfade)
              {
                gain = env;
              }
            else
              {
                gain = (v->gain_q15 * env) >> 15;
              }
            if (!fast && (v->mod_flags & MOD_AMP_LFO))
              {
                int32_t depth = (v->amp_lfo2_amt_q15 * v->lfo2.value) >> 15;
                gain = (gain * (32767 + depth)) >> 15;
              }
            mixbuf[n] += (value * gain) >> 15;
          }
          v->pos += v->rate_inc;
          if (v->age != UINT32_MAX)
            {
              v->age++;
            }
        }
    }

  if (hits)
    {
      atomic_fetch_add_explicit(&eng->cache_hits, hits, memory_order_relaxed);
    }
  if (misses)
    {
      atomic_fetch_add_explicit(&eng->cache_misses, misses,
                                memory_order_relaxed);
    }

  if (dropped_out)
    {
      *dropped_out = dropped;
    }
  if (under_out)
    {
      *under_out = under;
    }
  if (cubic_out)
    {
      *cubic_out = cubic;
    }
  if (linear_out)
    {
      *linear_out = linear;
    }
}

#if PCM_SAMP_DUAL_VOICE_MIX
static void *pcm_mix_helper(void *arg)
{
  struct pcm_samp_engine_s *eng = arg;

  while (!atomic_load_explicit(&eng->stop_worker, memory_order_acquire))
    {
      bool dropped = false;
      uint32_t under = 0;
      uint64_t cubic = 0;
      uint64_t linear = 0;
      uint64_t t_start;
      uint64_t t_end;

      if (sem_wait(&eng->mix_go) != 0)
        {
          continue;
        }
      if (atomic_load_explicit(&eng->stop_worker, memory_order_acquire))
        {
          break;
        }

      t_start = pcm_mono_us();
      eng->mix_kick_lat_us = (uint32_t)(t_start - eng->mix_kick_us);
      mix_voices_range(eng, eng->mixbuf_b, eng->mix_frames, eng->mix_v0,
                       eng->mix_v1, eng->mix_dense, eng->mix_fast, &dropped,
                       &under, &cubic, &linear);
      t_end = pcm_mono_us();
      eng->mix_dropped = dropped;
      eng->mix_under = under;
      eng->mix_cubic = cubic;
      eng->mix_linear = linear;
      eng->mix_b_us = (uint32_t)(t_end - t_start);
      sem_post(&eng->mix_done);
    }
  return NULL;
}

static int pcm_start_mix_helper(struct pcm_samp_engine_s *eng)
{
  pthread_attr_t attr;
  int ret;

  if (eng->mix_started)
    {
      return 0;
    }
  if (sem_init(&eng->mix_go, 0, 0) != 0 ||
      sem_init(&eng->mix_done, 0, 0) != 0)
    {
      return -errno;
    }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 16 * 1024);
#ifdef CONFIG_SMP
  {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  }
#endif
  ret = pthread_create(&eng->mix_thread, &attr, pcm_mix_helper, eng);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      sem_destroy(&eng->mix_go);
      sem_destroy(&eng->mix_done);
      return -ret;
    }
  eng->mix_started = true;
  syslog(LOG_NOTICE, "pcm_samp: dual-core mixer helper on CPU1\n");
  return 0;
}
#endif /* PCM_SAMP_DUAL_VOICE_MIX */

void pcm_samp_render(struct pcm_samp_engine_s *eng, int16_t *out,
                     unsigned frames)
{
  unsigned n;
  int nvoices = eng->active_count;
  int32_t master = eng->master_q15;
  bool dropped = false;
  bool dense;
  bool fast;
  bool dual;
  uint64_t t0 = pcm_mono_us();
  uint64_t t_mark;
  uint64_t t_after_clear;
  uint64_t t_after_mix_a;
  uint64_t t_after_wait;
  uint64_t t_after_merge;
  uint64_t t_after_sat;
  uint32_t budget_us;
  uint32_t render_us;
  uint32_t clear_us = 0;
  uint32_t mix_a_us = 0;
  uint32_t wait_tail_us = 0;
  uint32_t merge_us = 0;
  uint32_t sat_us = 0;
  uint32_t mix_b_us = 0;
  uint32_t kick_lat_us = 0;
  uint32_t under_this = 0;
  uint32_t hits0;
  uint32_t miss0;
  uint32_t hits1;
  uint32_t miss1;
  uint64_t cubic = 0;
  uint64_t linear = 0;
  int32_t *mixbuf;
  int split;
  int nv_a = 0;
  int nv_b = 0;
  static uint32_t s_break_logs;
  static uint32_t s_near_logs;

  if (frames == 0 || !out)
    {
      return;
    }
  if (frames > 2048u)
    {
      frames = 2048u;
    }
  budget_us = eng->out_rate ?
              (uint32_t)((uint64_t)frames * 1000000ull / eng->out_rate) : 0;

  /* Use the previous block to choose this block's quality.  Voice thresholds
   * are hard floors; timing pressure can only degrade farther.  Recovery is
   * deliberately slow so quality does not chatter between adjacent blocks.
   */
  {
    uint8_t floor = nvoices >= PCM_SAMP_FAST_VOICES ? 2u :
                    (nvoices >= PCM_SAMP_ADAPT_VOICES ? 1u : 0u);
    uint8_t level = eng->quality_level;

    if (level < floor)
      {
        level = floor;
        eng->quality_relax_blocks = 0;
        eng->quality_changes++;
      }
    if (budget_us && eng->last_render_us > (budget_us * 3u) / 4u &&
        level < 2u)
      {
        level++;
        eng->quality_relax_blocks = 0;
        eng->quality_changes++;
      }
    else if (budget_us && eng->last_render_us < (budget_us * 9u) / 20u &&
             level > floor)
      {
        if (++eng->quality_relax_blocks >= 16u)
          {
            level--;
            eng->quality_relax_blocks = 0;
            eng->quality_changes++;
          }
      }
    else
        {
        eng->quality_relax_blocks = 0;
        }
    eng->quality_level = level;
    dense = level >= 1u;
    fast = level >= 2u;
  }

  /* Voice-count trim removed: auto-gain follows mix peak below. */
  hits0 = atomic_load_explicit(&eng->cache_hits, memory_order_relaxed);
  miss0 = atomic_load_explicit(&eng->cache_misses, memory_order_relaxed);

  if (!eng->mixbuf)
    {
      eng->mixbuf = (int32_t *)malloc(2048u * sizeof(int32_t));
      eng->mixbuf_b = (int32_t *)malloc(2048u * sizeof(int32_t));
      if (!eng->mixbuf || !eng->mixbuf_b)
        {
          free(eng->mixbuf);
          free(eng->mixbuf_b);
          eng->mixbuf = NULL;
          eng->mixbuf_b = NULL;
          memset(out, 0, frames * sizeof(int16_t));
          return;
        }
    }
  mixbuf = eng->mixbuf;
  t_mark = pcm_mono_us();
  memset(mixbuf, 0, frames * sizeof(int32_t));

  dual = (nvoices >= 8) && eng->mix_started && eng->mixbuf_b;
  split = dual ? (nvoices / 2) : nvoices;
  nv_a = dual ? split : nvoices;
  nv_b = dual ? (nvoices - split) : 0;

  if (dual)
    {
      memset(eng->mixbuf_b, 0, frames * sizeof(int32_t));
      t_after_clear = pcm_mono_us();
      clear_us = (uint32_t)(t_after_clear - t_mark);

      eng->mix_frames = frames;
      eng->mix_v0 = split;
      eng->mix_v1 = nvoices;
      eng->mix_dense = dense;
      eng->mix_fast = fast;
      eng->mix_dropped = false;
      eng->mix_under = 0;
      eng->mix_cubic = 0;
      eng->mix_linear = 0;
      eng->mix_b_us = 0;
      eng->mix_kick_lat_us = 0;
      eng->mix_kick_us = pcm_mono_us();
      sem_post(&eng->mix_go);

      mix_voices_range(eng, mixbuf, frames, 0, split, dense, fast, &dropped,
                       &under_this, &cubic, &linear);
      t_after_mix_a = pcm_mono_us();
      mix_a_us = (uint32_t)(t_after_mix_a - eng->mix_kick_us);

      sem_wait(&eng->mix_done);
      t_after_wait = pcm_mono_us();
      wait_tail_us = (uint32_t)(t_after_wait - t_after_mix_a);
      mix_b_us = eng->mix_b_us;
      kick_lat_us = eng->mix_kick_lat_us;

      dropped = dropped || eng->mix_dropped;
      under_this += eng->mix_under;
      cubic += eng->mix_cubic;
      linear += eng->mix_linear;
      for (n = 0; n < frames; n++)
        {
          mixbuf[n] += eng->mixbuf_b[n];
        }
      t_after_merge = pcm_mono_us();
      merge_us = (uint32_t)(t_after_merge - t_after_wait);
    }
  else
    {
      t_after_clear = pcm_mono_us();
      clear_us = (uint32_t)(t_after_clear - t_mark);
      mix_voices_range(eng, mixbuf, frames, 0, nvoices, dense, fast, &dropped,
                       &under_this, &cubic, &linear);
      t_after_mix_a = pcm_mono_us();
      mix_a_us = (uint32_t)(t_after_mix_a - t_after_clear);
      t_after_merge = t_after_mix_a;
    }

  eng->cubic_samples += cubic;
  eng->linear_samples += linear;

  /* Auto bus trim from mix peak: only turn down when the sum is actually
   * hot. Sparse soft notes keep full master; dense loud stacks get reduced.
   */
  {
    int32_t peak = 1;
    int32_t projected;
    const int32_t target = 18000; /* headroom before soft_bus / XMOS */

    for (n = 0; n < frames; n++)
      {
        int32_t a = mixbuf[n] >= 0 ? mixbuf[n] : -mixbuf[n];
        if (a > peak)
          {
            peak = a;
          }
      }
    if (peak > eng->bus_peak_env)
      {
        eng->bus_peak_env += (peak - eng->bus_peak_env) >> 2; /* fast up */
      }
    else
      {
        eng->bus_peak_env += (peak - eng->bus_peak_env) >> 6; /* slow down */
      }
    projected = (int32_t)(((int64_t)eng->bus_peak_env * master) >> 15);
    if (projected > target && eng->bus_peak_env > 0)
      {
        master = (int32_t)(((int64_t)master * target) / projected);
        if (master < 2048)
          {
            master = 2048; /* floor so it never fully mutes */
          }
      }
  }

  for (n = 0; n < frames; n++)
    {
      int32_t x = (int32_t)(((int64_t)mixbuf[n] * master) >> 15);
      if (eng->start_fade_remain > 0 && eng->start_fade_total > 0)
        {
          uint32_t done = eng->start_fade_total - eng->start_fade_remain;
          int32_t fg = (int32_t)(((uint64_t)done * 32767ull) /
                                 eng->start_fade_total);
          x = (int32_t)(((int64_t)x * fg) >> 15);
          eng->start_fade_remain--;
        }
      out[n] = soft_bus16(x);
    }
  xmos_fx_process_mono_i16(out, frames);
  t_after_sat = pcm_mono_us();
  sat_us = (uint32_t)(t_after_sat - t_after_merge);

  if (dropped)
    {
      rebuild_active(eng);
    }

  render_us = (uint32_t)(pcm_mono_us() - t0);
  eng->last_render_us = render_us;
  eng->diag_render_us_sum += render_us;
  eng->diag_render_frames++;
  pcm_note_umax(&eng->diag_render_us_max, render_us);
  hits1 = atomic_load_explicit(&eng->cache_hits, memory_order_relaxed);
  miss1 = atomic_load_explicit(&eng->cache_misses, memory_order_relaxed);

  /* Breakdown: first high-poly blocks + near/over budget frames. */
  {
    bool near = budget_us && render_us > (budget_us * 4u) / 5u;
    bool sample_hi = (nvoices >= 32 && s_break_logs < 16u);
    bool log_near = near && s_near_logs < 24u;
    if (near)
      {
        atomic_fetch_add_explicit(&eng->diag_slow_render, 1,
                                  memory_order_relaxed);
      }
    if (sample_hi || log_near)
      {
        uint64_t nsamp = (uint64_t)nvoices * (uint64_t)frames;
        /* ns_per: nanoseconds per (voice*output-sample) of wall time. */
        uint32_t ns_per = nsamp ?
                          (uint32_t)(((uint64_t)render_us * 1000ull) /
                                     nsamp) : 0;
        uint32_t span_us = mix_a_us;
        if (dual && mix_b_us > span_us)
          {
            span_us = mix_b_us;
          }
        if (sample_hi)
          {
            s_break_logs++;
          }
        if (log_near)
          {
            s_near_logs++;
          }
        syslog(LOG_WARNING,
               "pcm_diag: RENDER_BREAK frames=%u voices=%d split=%d/%d "
               "dense=%d fast=%d qlvl=%u dual=%d "
               "total=%uus budget=%uus "
               "clear=%u kick=%u mixA=%u mixB=%u wait=%u merge=%u sat=%u "
               "span=%u under=%u dhit=%u dmiss=%u "
               "nsamp=%lu ns/samp=%u q=%u\n",
               frames, nvoices, nv_a, nv_b, (int)dense, (int)fast,
               eng->quality_level, (int)dual, render_us, budget_us,
               clear_us, kick_lat_us, mix_a_us, mix_b_us, wait_tail_us,
               merge_us, sat_us, span_us, under_this,
               hits1 - hits0, miss1 - miss0,
               (unsigned long)nsamp, ns_per, pcm_qdepth(eng));
      }
  }

  /* ~1s LOAD sampler: CPU0 mix occupancy + TF fill pressure. */
  if (nvoices > eng->load_peak_voices)
    {
      eng->load_peak_voices = (uint32_t)nvoices;
    }
  eng->load_win_frames++;
  eng->load_win_render_sum += render_us;
  if (render_us > eng->load_win_render_max)
    {
      eng->load_win_render_max = render_us;
    }
  eng->load_out_samples += frames;
  if (eng->out_rate && eng->load_out_samples >= eng->out_rate)
    {
      uint32_t fills = atomic_load_explicit(&eng->fills, memory_order_relaxed);
      uint32_t under = atomic_load_explicit(&eng->underruns,
                                            memory_order_relaxed);
      uint32_t miss = atomic_load_explicit(&eng->cache_misses,
                                           memory_order_relaxed);
      uint32_t hit = atomic_load_explicit(&eng->cache_hits,
                                          memory_order_relaxed);
      uint32_t fill_us_max =
        atomic_load_explicit(&eng->diag_fill_us_max, memory_order_relaxed);
      uint32_t fill_lat_max =
        atomic_load_explicit(&eng->diag_fill_lat_max, memory_order_relaxed);
      uint32_t qpeak =
        atomic_load_explicit(&eng->diag_q_peak, memory_order_relaxed);
      uint32_t req_drop =
        atomic_load_explicit(&eng->diag_req_drop, memory_order_relaxed);
      uint32_t dfill = fills - eng->load_fills0;
      uint32_t dunder = under - eng->load_under0;
      uint32_t dmiss = miss - eng->load_miss0;
      uint32_t dhit = hit - eng->load_hit0;
      uint32_t avg_us = eng->load_win_frames ?
                        (uint32_t)(eng->load_win_render_sum /
                                   eng->load_win_frames) : 0;
      uint32_t cpu0_pct = budget_us ?
                          (uint32_t)(((uint64_t)avg_us * 100ull) /
                                     budget_us) : 0;
      uint32_t cpu0_peak = budget_us ?
                           (uint32_t)(((uint64_t)eng->load_win_render_max *
                                       100ull) / budget_us) : 0;
      uint32_t fill_kib_s =
        (uint32_t)(((uint64_t)dfill * PCM_SAMP_HALF_SIZE) / 1024u);
      uint32_t avg_fill = fills ?
                          (uint32_t)(eng->diag_fill_us_sum / fills) : 0;
      bool io_hot = (fill_us_max > eng->load_fill_us_max0 &&
                     fill_us_max >= 4000u) ||
                    (qpeak - eng->load_qpeak0 > 0 && qpeak >= 8u) ||
                    dunder != 0 || dmiss != 0 ||
                    (req_drop != eng->load_req_drop0);

      eng->load_sec++;
      syslog(LOG_NOTICE,
             "pcm_diag: LOAD t=%us act=%d peak=%u qlvl=%u qchg=%u "
             "cpu0=%u%% "
             "cpu0_peak=%u%% render(avg/max/last)=%u/%u/%uus "
             "budget=%uus fill=%u/s (%uKiB/s) fill_us(avg/max)=%u/%u "
             "fill_lat_max=%u q=%u qpeak=%u under+=%u miss+=%u "
             "hit+=%u req_drop=%u io_hot=%d\n",
             eng->load_sec, nvoices, eng->load_peak_voices,
             eng->quality_level, eng->quality_changes,
             cpu0_pct, cpu0_peak, avg_us, eng->load_win_render_max,
             render_us, budget_us, dfill, fill_kib_s, avg_fill, fill_us_max,
             fill_lat_max, pcm_qdepth(eng), qpeak, dunder, dmiss, dhit,
             req_drop, (int)io_hot);

      eng->load_out_samples = 0;
      eng->load_win_frames = 0;
      eng->load_win_render_sum = 0;
      eng->load_win_render_max = 0;
      eng->load_fills0 = fills;
      eng->load_under0 = under;
      eng->load_miss0 = miss;
      eng->load_hit0 = hit;
      eng->load_fill_us_max0 = fill_us_max;
      eng->load_fill_lat_max0 = fill_lat_max;
      eng->load_qpeak0 = qpeak;
      eng->load_req_drop0 = req_drop;
      eng->load_peak_voices = (uint32_t)nvoices;
    }
}

int pcm_samp_demo(struct pcm_samp_engine_s *eng)
{
  static const int notes[] = {48, 55, 60, 64, 67, 72};
  unsigned i;
  int count = 0;
  for (i = 0; i < sizeof(notes) / sizeof(notes[0]); i++)
    {
      if (pcm_samp_note_on(eng, 0, notes[i], 82 + (int)i * 5) >= 0)
        {
          count++;
        }
    }
  return count;
}

int pcm_samp_selftest(struct pcm_samp_engine_s *eng, unsigned milliseconds)
{
  enum { BLOCK = 128 };
  int16_t out[BLOCK];
  uint32_t blocks;
  uint32_t i;
  uint32_t nonzero = 0;
  if (pcm_samp_demo(eng) == 0)
    {
      return -ENOENT;
    }
  usleep(30000); /* test thread may wait; render thread never does */
  blocks = ((uint64_t)milliseconds * eng->out_rate / 1000u + BLOCK - 1u) /
           BLOCK;
  for (i = 0; i < blocks; i++)
    {
      unsigned j;
      pcm_samp_render(eng, out, BLOCK);
      for (j = 0; j < BLOCK; j++)
        {
          nonzero += out[j] != 0;
        }
      usleep((BLOCK * 1000000u) / eng->out_rate);
    }
  pcm_samp_stats(eng, "selftest");
  if (nonzero)
    {
      syslog(LOG_NOTICE, "pcm_samp: selftest PASS (%ums, nonzero=%lu)\n",
             milliseconds, (unsigned long)nonzero);
      return 0;
    }

  syslog(LOG_ERR, "pcm_samp: selftest FAIL (%ums, silence)\n",
         milliseconds);
  return -EIO;
}

void pcm_samp_stats(struct pcm_samp_engine_s *eng, const char *where)
{
  char line[384];
  uint32_t fills = atomic_load(&eng->fills);
  uint32_t avg_fill = fills ?
                      (uint32_t)(eng->diag_fill_us_sum / fills) : 0;
  uint32_t avg_render = eng->diag_render_frames ?
                        (uint32_t)(eng->diag_render_us_sum /
                                   eng->diag_render_frames) : 0;

  snprintf(line, sizeof(line),
           "pcm_samp %s: active=%d peak=%d voice_buf=%ux%uKiB hit=%lu "
           "miss=%lu underrun=%lu ioerr=%lu fill=%lu cubic=%llu "
           "linear=%llu steal=%lu",
           where ? where : "stats", eng->active_count, eng->peak_active,
           PCM_SAMP_MAX_VOICES,
           (unsigned)(PCM_SAMP_VOICE_BYTES / 1024u),
           (unsigned long)atomic_load(&eng->cache_hits),
           (unsigned long)atomic_load(&eng->cache_misses),
           (unsigned long)atomic_load(&eng->underruns),
           (unsigned long)atomic_load(&eng->io_errors),
           (unsigned long)fills,
           (unsigned long long)eng->cubic_samples,
           (unsigned long long)eng->linear_samples,
           (unsigned long)eng->steals);
  printf("%s\n", line);
  syslog(LOG_NOTICE, "%s\n", line);
  syslog(LOG_NOTICE,
         "pcm_diag %s: qpeak=%u req_drop=%u fill_stale=%u "
         "slow_fill=%u slow_render=%u qlvl=%u qchg=%u fill_us(avg/max)=%u/%u "
         "fill_lat_max=%u render_us(avg/max)=%u/%u\n",
         where ? where : "stats",
         atomic_load(&eng->diag_q_peak),
         atomic_load(&eng->diag_req_drop),
         atomic_load(&eng->diag_fill_stale),
         atomic_load(&eng->diag_slow_fill),
         atomic_load(&eng->diag_slow_render),
         eng->quality_level, eng->quality_changes,
         avg_fill, atomic_load(&eng->diag_fill_us_max),
         atomic_load(&eng->diag_fill_lat_max),
         avg_render, atomic_load(&eng->diag_render_us_max));
}
