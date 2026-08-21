/*
 * @Author: llq
 * @Date: 2025-12-24 10:58:59
 * @LastEditors: llq
 * @LastEditTime: 2025-12-25 18:01:05
 * @Description: cabSim
 * @FilePath: \sw900_aero_1224\sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\Mesa412.h
 */
#ifndef Mesa412_H
#define Mesa412_H

void Mesa412_init();
void Mesa412_process(float *xin, float *xOut);

#endif // Mesa412_H