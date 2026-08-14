/*
 * @Author: llq
 * @Date: 2026-04-15 13:59:59
 * @LastEditors: llq
 * @LastEditTime: 2026-04-15 14:50:05
 * @Description: cabSim
 * @FilePath: \sw_AB_FB265_Debuging_0202\src\algorithm\reverb.h
 */
#ifndef __REVERB_H__
#define __REVERB_H__

#define MAX_DELAY1_L 1101
#define MAX_DELAY1_R 1091
#define MAX_DELAY2_L 1061
#define MAX_DELAY2_R 1035
#define MAX_DELAY3_L 1054
#define MAX_DELAY3_R 1044
#define MAX_DELAY4_L 1255
#define MAX_DELAY4_R 1383
#define MAX_DELAY5_L 1579
#define MAX_DELAY5_R 1677
#define MAX_DELAY6_L 1881
#define MAX_DELAY6_R 1885

#define ER_TAPS 6
#define MAX_EARLY_DELAY 3870
#define L_TO_R_DELAY 14 // 0.3 ms at 48 kHz
#define R_TO_L_DELAY 14 // 0.3 ms at 48 kHz

enum {BIQUAD_RBJ_BW = 0, BIQUAD_RBJ_Q  = 1, BIQUAD_RBJ_S  = 2, };

typedef struct {
    int delayL[ER_TAPS];
    int delayR[ER_TAPS];
    float gainL[ER_TAPS];
    float gainR[ER_TAPS];
    float bufferL[MAX_EARLY_DELAY];
    float bufferR[MAX_EARLY_DELAY];
    int indexL;
    int indexR;
} EarlyReflections;


typedef enum {
    REVERB_TYPE_0,
    REVERB_TYPE_1,
    REVERB_TYPE_2,
    REVERB_TYPE_3,
    REVERB_TYPE_4,
    REVERB_TYPE_5
} ReverbType;

void reverb_T0_init();
void reverb_T1_init();

#endif /* __REVERB_H__ */