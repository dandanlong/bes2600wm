/****************************************************************************
 * Single A-core LC3 decode stress @ 48 kHz / 124 kbps / 10 ms.
 * Uses google liblc3 already vendored under rtos/external/lc3.
 ****************************************************************************/

#include "lc3_bench.h"

#include <nuttx/config.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <lc3.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#define BENCH_SR_HZ      48000
#define BENCH_DT_US      10000
#define BENCH_BITRATE    124000
#define BENCH_MAX_STREAMS 64

enum { PCM64_CMU_FREQ_780M = 11 };
int hal_cmu_dsp_set_freq(int freq);
int hal_cmu_dsp_get_freq(void);
uint32_t hal_sys_timer_calc_cpu_freq(uint32_t osc_intvl_ms, int high_res);

static uint64_t mono_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull +
         (uint64_t)ts.tv_nsec / 1000ull;
}

static int make_probe_packet(uint8_t *pkt, int pkt_cap, int *pkt_len,
                             int16_t *pcm, int nsamp, int nbytes)
{
  lc3_encoder_mem_48k_t emem;
  lc3_encoder_t enc;
  int i;
  int ret;

  enc = lc3_setup_encoder(BENCH_DT_US, BENCH_SR_HZ, 0, &emem);
  if (!enc)
    {
      return -1;
    }
  for (i = 0; i < nsamp; i++)
    {
      float t = (float)i / (float)BENCH_SR_HZ;
      pcm[i] = (int16_t)(sinf(2.f * (float)M_PI * 440.f * t) * 16000.f);
    }
  if (nbytes > pkt_cap)
    {
      return -1;
    }
  ret = lc3_encode(enc, LC3_PCM_FORMAT_S16, pcm, 1, nbytes, pkt);
  if (ret != 0)
    {
      return -1;
    }
  *pkt_len = nbytes;
  return 0;
}

static void bench_single(const uint8_t *pkt, int nbytes, int nsamp, int iters)
{
  lc3_decoder_mem_48k_t dmem;
  lc3_decoder_t dec;
  int16_t *pcm;
  int i;
  int fail = 0;
  uint64_t t0;
  uint64_t dt;
  double us_each;
  double fps;
  double max_streams;

  dec = lc3_setup_decoder(BENCH_DT_US, BENCH_SR_HZ, 0, &dmem);
  pcm = (int16_t *)malloc((size_t)nsamp * sizeof(int16_t));
  if (!dec || !pcm)
    {
      syslog(LOG_ERR, "lc3_bench: single alloc fail\n");
      free(pcm);
      return;
    }

  for (i = 0; i < 32; i++)
    {
      (void)lc3_decode(dec, pkt, nbytes, LC3_PCM_FORMAT_S16, pcm, 1);
    }

  t0 = mono_us();
  for (i = 0; i < iters; i++)
    {
      if (lc3_decode(dec, pkt, nbytes, LC3_PCM_FORMAT_S16, pcm, 1) < 0)
        {
          fail++;
        }
    }
  dt = mono_us() - t0;
  us_each = (double)dt / (double)iters;
  fps = 1e6 / us_each;
  max_streams = (double)BENCH_DT_US / us_each;

  syslog(LOG_NOTICE,
         "lc3_bench: SINGLE iters=%d fail=%d total=%llu us "
         "avg=%.1f us/frame  %.0f frame/s  => max_streams@10ms ≈ %.1f\n",
         iters, fail, (unsigned long long)dt, us_each, fps, max_streams);
  syslog(LOG_NOTICE,
         "lc3_bench:   verdict 8x:%s 12x:%s 16x:%s 24x:%s 32x:%s 48x:%s 64x:%s\n",
         max_streams >= 8.0 ? "YES" : "NO",
         max_streams >= 12.0 ? "YES" : "NO",
         max_streams >= 16.0 ? "YES" : "NO",
         max_streams >= 24.0 ? "YES" : "NO",
         max_streams >= 32.0 ? "YES" : "NO",
         max_streams >= 48.0 ? "YES" : "NO",
         max_streams >= 64.0 ? "YES" : "NO");
  printf("SINGLE avg=%.1fus/fr  %.0f fps  max≈%.1fx @10ms\n",
         us_each, fps, max_streams);

  free(pcm);
}

static void bench_poly(const uint8_t *pkt, int nbytes, int nsamp,
                       int streams, int slots)
{
  lc3_decoder_mem_48k_t *dmems;
  lc3_decoder_t decs[BENCH_MAX_STREAMS];
  int16_t *pcm;
  int s;
  int slot;
  int fail = 0;
  int opened = 0;
  uint64_t t0;
  uint64_t dt;
  double us_each;
  double us_slot;
  double load;

  if (streams > BENCH_MAX_STREAMS)
    {
      streams = BENCH_MAX_STREAMS;
    }

  dmems = (lc3_decoder_mem_48k_t *)
    malloc((size_t)streams * sizeof(lc3_decoder_mem_48k_t));
  pcm = (int16_t *)malloc((size_t)nsamp * sizeof(int16_t));
  if (!dmems || !pcm)
    {
      syslog(LOG_ERR, "lc3_bench: poly alloc fail streams=%d\n", streams);
      free(dmems);
      free(pcm);
      return;
    }

  memset(decs, 0, sizeof(decs));
  for (s = 0; s < streams; s++)
    {
      decs[s] = lc3_setup_decoder(BENCH_DT_US, BENCH_SR_HZ, 0, &dmems[s]);
      if (!decs[s])
        {
          break;
        }
      opened++;
      (void)lc3_decode(decs[s], pkt, nbytes, LC3_PCM_FORMAT_S16, pcm, 1);
    }
  streams = opened;
  if (streams < 1)
    {
      free(dmems);
      free(pcm);
      return;
    }

  t0 = mono_us();
  for (slot = 0; slot < slots; slot++)
    {
      for (s = 0; s < streams; s++)
        {
          if (lc3_decode(decs[s], pkt, nbytes, LC3_PCM_FORMAT_S16, pcm, 1) < 0)
            {
              fail++;
            }
        }
    }
  dt = mono_us() - t0;
  us_each = (double)dt / (double)(streams * slots);
  us_slot = (double)dt / (double)slots;
  load = us_slot / (double)BENCH_DT_US * 100.0;

  syslog(LOG_NOTICE,
         "lc3_bench: POLY %2dx  avg=%.1fus/fr  slot=%.0fus/10ms  "
         "load≈%.0f%%  %s  (fail=%d)\n",
         streams, us_each, us_slot, load,
         us_slot < (double)BENCH_DT_US ? "PASS" : "FAIL", fail);
  printf("POLY %2dx  slot=%.0fus  load≈%.0f%%  %s\n",
         streams, us_slot, load,
         us_slot < (double)BENCH_DT_US ? "PASS" : "FAIL");

  free(dmems);
  free(pcm);
}

struct lc3_bench_arg_s
{
  int done;
  int rc;
};

static void *lc3_bench_thread(void *arg)
{
  struct lc3_bench_arg_s *ba = arg;
  uint8_t pkt[400];
  int16_t pcm[480];
  int nbytes;
  int nsamp;
  int pkt_len = 0;
  const int sweep[] = { 8, 12, 16, 20, 24, 32, 48, 64 };
  unsigned i;

  nbytes = lc3_frame_bytes(BENCH_DT_US, BENCH_BITRATE);
  nsamp = lc3_frame_samples(BENCH_DT_US, BENCH_SR_HZ);
  if (nbytes < 20 || nsamp < 1)
    {
      syslog(LOG_ERR, "lc3_bench: bad params nbytes=%d nsamp=%d\n",
             nbytes, nsamp);
      ba->rc = -1;
      ba->done = 1;
      return NULL;
    }

  syslog(LOG_NOTICE,
         "lc3_bench: === LC3 decode  %d Hz / %d kbps / %d us  "
         "nbyte=%d nsamp=%d ===\n",
         BENCH_SR_HZ, BENCH_BITRATE / 1000, BENCH_DT_US, nbytes, nsamp);
  printf("lc3_bench: %dHz %dkbps %dus nbyte=%d\n",
         BENCH_SR_HZ, BENCH_BITRATE / 1000, BENCH_DT_US, nbytes);

  if (make_probe_packet(pkt, (int)sizeof(pkt), &pkt_len, pcm, nsamp,
                        nbytes) != 0)
    {
      syslog(LOG_ERR, "lc3_bench: encode probe fail\n");
      ba->rc = -1;
      ba->done = 1;
      return NULL;
    }
  syslog(LOG_NOTICE, "lc3_bench: probe packet %d bytes\n", pkt_len);

  bench_single(pkt, pkt_len, nsamp, 4000);

  for (i = 0; i < sizeof(sweep) / sizeof(sweep[0]); i++)
    {
      bench_poly(pkt, pkt_len, nsamp, sweep[i], 100);
      usleep(5000);
    }

  syslog(LOG_NOTICE, "lc3_bench: === done ===\n");
  printf("lc3_bench: done\n");
  ba->rc = 0;
  ba->done = 1;
  return NULL;
}

int lc3_bench_run(void)
{
  pthread_attr_t attr;
  struct sched_param sp;
  pthread_t th;
  struct lc3_bench_arg_s ba;
  int ret;
  uint32_t hz;
  int cf;

  /* Same boost as pcm64 -bench: single A-core at 780 MHz. */
  hal_cmu_dsp_set_freq(PCM64_CMU_FREQ_780M);
  hz = hal_sys_timer_calc_cpu_freq(5, 0);
  cf = hal_cmu_dsp_get_freq();
  syslog(LOG_NOTICE,
         "lc3_bench: force 780M, calc=%lu Hz cmu_freq=%d\n",
         (unsigned long)hz, cf);
  printf("lc3_bench: force 780M, calc=%lu Hz cmu_freq=%d\n",
         (unsigned long)hz, cf);

  memset(&ba, 0, sizeof(ba));
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 48 * 1024);
#ifdef CONFIG_SMP
  {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  }
#endif
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
  pthread_attr_setschedparam(&attr, &sp);

  ret = pthread_create(&th, &attr, lc3_bench_thread, &ba);
  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      syslog(LOG_ERR, "lc3_bench: pthread_create %d\n", ret);
      return -ret;
    }

  pthread_join(th, NULL);
  return ba.rc;
}
