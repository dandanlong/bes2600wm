/****************************************************************************
 * DAILE I2S1 mirror — pinmux IO20/21/22/23 + AF stream tee from DAC path.
 ****************************************************************************/

#include "daile_i2s_mirror.h"

#include <string.h>
#include <syslog.h>

#include "hal_iomux.h"
#include "hal_cmu.h"
#include "audioflinger.h"
#include "hal_aud.h"

#ifndef CONFIG_AUDIO_BES_FRAME_MS
#define CONFIG_AUDIO_BES_FRAME_MS 10
#endif

/* Enough for 48 kHz / 32 ms / stereo / 16-bit / double-buffer. */
#define DAILE_I2S_MAX_BYTES \
  (48 * 2 * 2 * 32 * 2)

static uint8_t g_i2s_dma[DAILE_I2S_MAX_BYTES]
  __attribute__((aligned(32)));

/* Latest period for I2S DMA to copy (stereo interleaved int16). */
static uint8_t g_shadow[DAILE_I2S_MAX_BYTES / 2]
  __attribute__((aligned(32)));
static volatile uint32_t g_shadow_bytes;
static volatile int g_started;
static uint32_t g_rate;
static uint8_t g_bits;
static uint32_t g_feed_n;
static uint32_t g_dma_n;

static void daile_i2s1_pinmux_io202123(void)
{
  static const struct HAL_IOMUX_PIN_FUNCTION_MAP pinmux[] = {
    {HAL_IOMUX_PIN_P2_2, HAL_IOMUX_FUNC_I2S1_WS, HAL_IOMUX_PIN_VOLTAGE_VIO,
     HAL_IOMUX_PIN_NOPULL},
    {HAL_IOMUX_PIN_P2_3, HAL_IOMUX_FUNC_I2S1_SCK, HAL_IOMUX_PIN_VOLTAGE_VIO,
     HAL_IOMUX_PIN_NOPULL},
    {HAL_IOMUX_PIN_P2_1, HAL_IOMUX_FUNC_I2S1_SDO0, HAL_IOMUX_PIN_VOLTAGE_VIO,
     HAL_IOMUX_PIN_NOPULL},
    {HAL_IOMUX_PIN_P2_0, HAL_IOMUX_FUNC_I2S_MCLK, HAL_IOMUX_PIN_VOLTAGE_VIO,
     HAL_IOMUX_PIN_NOPULL},
  };

  hal_iomux_init(pinmux, sizeof(pinmux) / sizeof(pinmux[0]));
}

void daile_i2s_mirror_pinmux(void)
{
  daile_i2s1_pinmux_io202123();
}

static uint32_t daile_i2s_dma_handler(uint8_t *buf, uint32_t len)
{
  uint32_t n = g_shadow_bytes;
  uint32_t copy;

  if (buf == NULL || len == 0)
    {
      return 0;
    }

  /* DAC period is often 44100*20ms (=882 frames) while I2S open used
   * rate/1000*ms (=880). n>len used to memset(0) the whole buffer →
   * codec silent while on-chip DAC still played. Always copy what fits. */
  if (n == 0)
    {
      memset(buf, 0, len);
      return len;
    }

  copy = (n < len) ? n : len;
  memcpy(buf, g_shadow, copy);
  if (copy < len)
    {
      memset(buf + copy, 0, len - copy);
    }

  g_dma_n++;
  if ((g_dma_n % 100u) == 1u)
    {
      const int16_t *s = (const int16_t *)g_shadow;
      uint32_t frames = copy / (2u * sizeof(int16_t));
      int16_t peak = 0;
      uint32_t i;
      for (i = 0; i < frames && i < 64u; i++)
        {
          int16_t a = s[i * 2u];
          if (a < 0)
            {
              a = (int16_t)(-a);
            }
          if (a > peak)
            {
              peak = a;
            }
        }
      syslog(LOG_INFO, "daile_i2s: dma#%lu len=%lu shadow=%lu peak=%d\n",
             (unsigned long)g_dma_n, (unsigned long)len,
             (unsigned long)n, (int)peak);
    }
  return len;
}

int daile_i2s_mirror_start(uint32_t sample_rate, uint8_t bits,
                           uint8_t src_channels)
{
  struct AF_STREAM_CONFIG_T cfg;
  uint32_t frame_ms = CONFIG_AUDIO_BES_FRAME_MS;
  uint32_t data_size;
  int ret;

  (void)src_channels;

  if (sample_rate < 8000 || sample_rate > 96000)
    {
      sample_rate = 44100;
    }
  /* Mirror path is int16 PCM; keep AF/DMA at 16-bit words. Many external
   * codecs expect 32 BCLK/channel (slot_cycles=32) even for 16-bit audio. */
  bits = 16;

  if (g_started && g_rate == sample_rate && g_bits == bits)
    {
      return 0;
    }

  if (g_started)
    {
      daile_i2s_mirror_stop();
    }

  daile_i2s1_pinmux_io202123();

  /* Always drive I2S as stereo; mono sources are expanded in feed().
   * Prefer true frame count (rate*ms/1000) so 44100/20ms = 882, not 880.
   * AF uses fixed AUDIO_BUFFER_COUNT=4 and align=4, so data_size must be
   * divisible by 16. Stereo i16 double-buf => frames must be even
   * (44100@10ms would be 441 -> 3528 -> desc_xfer 882 % 4 != 0 -> assert/reboot). */
  {
    uint32_t frames = (sample_rate * frame_ms) / 1000u;
    if (frames == 0)
      {
        frames = sample_rate / 50u;
      }
    frames &= ~1u;
    if (frames == 0)
      {
        frames = 2u;
      }
    data_size = frames * 2u * (bits / 8u) * 2u; /* stereo * bytes * double-buf */
  }
  if (data_size > sizeof(g_i2s_dma) || data_size == 0)
    {
      syslog(LOG_ERR, "daile_i2s: data_size %lu too big\n",
             (unsigned long)data_size);
      return -1;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.bits = bits;
  cfg.slot_cycles = 32; /* BCLK = fs * 32 * 2 = 64*fs */
  cfg.sample_rate = sample_rate;
  cfg.channel_num = AUD_CHANNEL_NUM_2;
  cfg.channel_map = AUD_CHANNEL_MAP_CH0 | AUD_CHANNEL_MAP_CH1;
  cfg.device = AUD_STREAM_USE_I2S1_MASTER;
  cfg.io_path = AUD_IO_PATH_NULL;
  cfg.vol = TGT_VOLUME_LEVEL_MAX;
  cfg.handler = daile_i2s_dma_handler;
  cfg.data_ptr = g_i2s_dma;
  cfg.data_size = data_size;

  af_lock_thread();
  ret = (int)af_stream_open(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK, &cfg);
  if (ret != 0)
    {
      af_stream_stop(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
      af_stream_close(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
      ret = (int)af_stream_open(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK, &cfg);
    }
  if (ret != 0)
    {
      af_unlock_thread();
      syslog(LOG_ERR, "daile_i2s: open failed %d\n", ret);
      return -1;
    }

  ret = (int)af_stream_start(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
  af_unlock_thread();
  if (ret != 0)
    {
      af_stream_close(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
      syslog(LOG_ERR, "daile_i2s: start failed %d\n", ret);
      return -1;
    }

  /* hal_i2s_open(I2S1) calls hal_iomux_set_i2s1(); re-apply DAILE mux
   * so WS/SCK/DOUT/MCLK stay on IO22/23/21/20 after open/start. */
  daile_i2s1_pinmux_io202123();

  /* Pinmux alone is not enough: AP build has no I2S_MCLK_FROM_* so
   * hal_i2s never gates MCLK onto the pad. Route audio PLL → P2_0.
   * best2003 only implements hal_iomux_set_i2s0_mclk(); with
   * I2S_MCLK_IOMUX_INDEX=20 that still selects P2_0. */
  (void)hal_cmu_i2s_mclk_enable(HAL_CMU_I2S_MCLK_PLLCODEC);
  hal_iomux_set_i2s0_mclk();

  g_rate = sample_rate;
  g_bits = bits;
  g_shadow_bytes = 0;
  g_feed_n = 0;
  g_dma_n = 0;
  g_started = 1;
  syslog(LOG_NOTICE,
         "daile_i2s: mirror on I2S1 IO20/21/22/23 rate=%lu bits=%u "
         "slot=32 stereo frame=%ums mclk=on\n",
         (unsigned long)sample_rate, bits, (unsigned)frame_ms);
  return 0;
}

void daile_i2s_mirror_stop(void)
{
  if (!g_started)
    {
      return;
    }
  af_lock_thread();
  af_stream_stop(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
  af_stream_close(AUD_STREAM_ID_1, AUD_STREAM_PLAYBACK);
  af_unlock_thread();
  hal_cmu_i2s_mclk_disable();
  g_started = 0;
  g_shadow_bytes = 0;
  syslog(LOG_NOTICE, "daile_i2s: mirror stopped\n");
}

void daile_i2s_mirror_feed(const void *pcm, uint32_t bytes,
                           uint8_t src_channels)
{
  const int16_t *in;
  int16_t *out;
  uint32_t frames;
  uint32_t i;
  uint32_t out_bytes;

  if (!g_started || pcm == NULL || bytes == 0 || g_bits != 16)
    {
      return;
    }

  in = (const int16_t *)pcm;
  out = (int16_t *)g_shadow;

  if (src_channels >= 2)
    {
      if (bytes > sizeof(g_shadow))
        {
          bytes = sizeof(g_shadow);
        }
      memcpy(g_shadow, pcm, bytes);
      g_shadow_bytes = bytes;
      out_bytes = bytes;
      frames = bytes / (2u * sizeof(int16_t));
    }
  else
    {
      /* Mono → stereo L/R duplicate. */
      frames = bytes / sizeof(int16_t);
      out_bytes = frames * 2u * sizeof(int16_t);
      if (out_bytes > sizeof(g_shadow))
        {
          frames = sizeof(g_shadow) / (2u * sizeof(int16_t));
          out_bytes = frames * 2u * sizeof(int16_t);
        }
      for (i = 0; i < frames; i++)
        {
          int16_t s = in[i];
          out[i * 2u] = s;
          out[i * 2u + 1u] = s;
        }
      g_shadow_bytes = out_bytes;
    }

  g_feed_n++;
  if ((g_feed_n % 50u) == 1u)
    {
      int16_t peak = 0;
      const int16_t *s = (const int16_t *)g_shadow;
      uint32_t nframes = out_bytes / (2u * sizeof(int16_t));
      for (i = 0; i < nframes && i < 64u; i++)
        {
          int16_t a = s[i * 2u];
          if (a < 0)
            {
              a = (int16_t)(-a);
            }
          if (a > peak)
            {
              peak = a;
            }
        }
      syslog(LOG_INFO,
             "daile_i2s: feed#%lu ch=%u in=%lu out=%lu peak=%d\n",
             (unsigned long)g_feed_n, src_channels,
             (unsigned long)bytes, (unsigned long)out_bytes, (int)peak);
    }
}

bool daile_i2s_mirror_running(void)
{
  return g_started != 0;
}
