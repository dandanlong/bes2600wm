/****************************************************************************
 * DAILE: keep INT_CODEC (DAC) and mirror PCM to I2S1 on IO20/21/22/23.
 *
 *   IO20 / P2_0  → I2S_MCLK
 *   IO21 / P2_1  → I2S1_SDO0 (DOUT)
 *   IO22 / P2_2  → I2S1_WS   (LRCK)
 *   IO23 / P2_3  → I2S1_SCK  (BCLK)
 *   IO24 / P2_4  → free
 *   IO25 / P2_5  → free
 ****************************************************************************/

#ifndef __DAILE_I2S_MIRROR_H
#define __DAILE_I2S_MIRROR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void daile_i2s_mirror_pinmux(void);

/* Open+start I2S1 master (stereo). Safe to call more than once. */
int daile_i2s_mirror_start(uint32_t sample_rate, uint8_t bits,
                           uint8_t src_channels);

void daile_i2s_mirror_stop(void);

/* Tee one DAC period into the I2S shadow (mono expanded to L=R). */
void daile_i2s_mirror_feed(const void *pcm, uint32_t bytes,
                           uint8_t src_channels);

bool daile_i2s_mirror_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __DAILE_I2S_MIRROR_H */
