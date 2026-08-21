/****************************************************************************
 * DAILE: keep INT_CODEC (DAC) and mirror PCM to I2S0 on IO2/3/4/7.
 *
 *   IO2 / P0_2  → I2S0_WS   (LRCK)
 *   IO3 / P0_3  → I2S0_SCK  (BCLK)
 *   IO4 / P0_4  → I2S_MCLK
 *   IO7 / P0_7  → I2S0_SDO0 (DOUT)
 ****************************************************************************/
#ifndef __DAILE_I2S_MIRROR_H
#define __DAILE_I2S_MIRROR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void daile_i2s_mirror_pinmux(void);

/* Open+start I2S0 master (stereo). Safe to call more than once. */
int daile_i2s_mirror_start(uint32_t sample_rate, uint8_t bits,
                           uint8_t src_channels);

void daile_i2s_mirror_stop(void);

/* Push one DAC DMA period (mono or stereo PCM). Expands mono→L/R. */
void daile_i2s_mirror_feed(const void *pcm, uint32_t bytes,
                           uint8_t src_channels);

bool daile_i2s_mirror_running(void);

#ifdef __cplusplus
}
#endif

#endif
