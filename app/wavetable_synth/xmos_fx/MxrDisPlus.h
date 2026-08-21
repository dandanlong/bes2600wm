/*
 * @Author: llq
 * @Date: 2026-01-13 10:15:07
 * @LastEditors: llq
 * @LastEditTime: 2026-01-13 10:16:46
 * @Description: cabSim
 * @FilePath: \sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\MxrDisPlus.h
 */
#ifndef MxrDisPlus_H
#define MxrDisPlus_H

void MxrDisPlus_init();
void MxrDisPlus_process(float *xin, float *xOut);

#endif // MxrDisPlus_H