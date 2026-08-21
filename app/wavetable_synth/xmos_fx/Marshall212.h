/*
 * @Author: llq
 * @Date: 2025-12-05 17:01:54
 * @LastEditors: llq
 * @LastEditTime: 2025-12-25 18:16:27
 * @Description: cabSim
 * @FilePath: \sw900_aero_1224\sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\Marshall212.h
 */
#ifndef Marshall212_H
#define Marshall212_H

void Marshall212_init();
void Marshall212_process(float *xin, float *xOut);

#endif // Marshall212_H