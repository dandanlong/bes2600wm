/*
 * @Author: llq
 * @Date: 2025-12-26 14:08:57
 * @LastEditors: llq
 * @LastEditTime: 2025-12-26 14:09:38
 * @Description: cabSim
 * @FilePath: \sw900_aero_1224\sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\FenderTwinReverb212.h
 */
#ifndef FenderTwinReverb212_H
#define FenderTwinReverb212_H

void FenderTwinReverb212_init();
void FenderTwinReverb212_process(float *xin, float *xOut);

#endif // FenderTwinReverb212_H