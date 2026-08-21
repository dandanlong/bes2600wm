/*
 * @Author: llq
 * @Date: 2025-12-10 18:42:09
 * @LastEditors: llq
 * @LastEditTime: 2025-12-25 18:11:04
 * @Description: cabSim
 * @FilePath: \sw900_aero_1224\sw_usb_audio\app_usb_aud_xk_316_mc\src\algorithm\Vox212.h
 */
#ifndef Vox212_H
#define Vox212_H

void Vox212_init();
void Vox212_process(float *xin, float *xOut);

#endif // Vox212_H