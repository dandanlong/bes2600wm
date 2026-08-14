/*
 * @Author: llq
 * @Date: 2026-01-23 15:16:05
 * @LastEditors: llq
 * @LastEditTime: 2026-02-04 11:37:11
 * @Description: Compressor
 * @FilePath: \sw_AB_FB265_Debuging_0202\src\algorithm\CompressorA.h
 */
#ifndef Compressor_H
#define Compressor_H

#define LOOKAHEAD_SIZE (512)
void Compressor_init(void);
void t1_Compressor_fProcess(float* dataIn, float* dataOut);
#endif // Compressor_H