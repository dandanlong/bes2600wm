/*
 * @Author: llq
 * @Date: 2025-12-26 11:57:35
 * @LastEditors: llq
 * @LastEditTime: 2025-12-26 11:58:40
 * @Description: Fender Twin Reverb
 * @FilePath: \sw900_aero_1224\sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\FenderTwinReverb.h
 */
#ifndef FenderTwinReverb_H
#define FenderTwinReverb_H

void FenderTwinReverb_init();
void FenderTwinReverb_process(float *xin, float *xOut);

#endif // FenderTwinReverb_H