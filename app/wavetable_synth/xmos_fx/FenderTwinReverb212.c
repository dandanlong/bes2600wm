/*
 * @Author: llq
 * @Date: 2025-12-26 14:08:37
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 13:59:08
 * @Description: cabSim
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\FenderTwinReverb212.c
 */
#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "FenderTwinReverb212.h"

float xCabL_fenderTwinRev212[15][3],yCabL_fenderTwinRev212[14][3];
float numCab_fenderTwinRev212[14][3] = { 
0.7766541f,-1.2730632f,0.4964157f,
0.6396814f,-1.2730632f,0.6351665f,
0.6930719f,-0.7139052f,0.3888366f,
0.5757927f,-1.0729329f,0.5622981f,
0.6829763f,-1.2730632f,0.5900877f,
0.6852150f,-1.2730632f,0.6096407f,
0.8642277f,-1.2730632f,0.8004165f,
1.2730632f,0.4251560f,0.6279134f,
1.2730632f,1.0594001f,0.2203776f,
1.2730632f,1.1460023f,0.2064885f,
1.0366086f,-1.2730632f,0.5341168f,
1.2730632f,-0.1485291f,0.8781998f,
0.7276214f,-0.6799656f,0.5286805f,
0.3329179f,0.1390787f,0.0000000f};

float denCab_fenderTwinRev212[14][2] = { 
-1.7804153f,0.7822079f,
-1.3209846f,0.3340182f,
-1.9880411f,0.9908078f,
-1.2883548f,0.4063247f,
-1.8641577f,0.9665362f,
-1.4141491f,0.8493963f,
-1.2550898f,0.8431984f,
0.6219096f,0.0473852f,
-0.3286332f,0.1031611f,
-1.2492166f,0.3501117f,
-0.0300188f,0.4770231f,
-1.0789590f,0.3182753f,
0.7840933f,0.1197370f,
-0.7374847f,0.0000000f};

void FenderTwinReverb212_init()
{
    memset(xCabL_fenderTwinRev212,0.0f,sizeof(xCabL_fenderTwinRev212));
    memset(yCabL_fenderTwinRev212,0.0f,sizeof(yCabL_fenderTwinRev212));
}

void FenderTwinReverb212_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float level_fenderTwinRev212 = FenderTwinReverb212_T1_knob[1]*d255;
    float masterVol_fenderTwinRev212 = 20.0f * level_fenderTwinRev212 * level_fenderTwinRev212;

    float youtL;
    int j = 0;
	int idx = 0;

        int i=0;
        inputL = xin[0+i];
        inputR = xin[0+i];
        xCabL_fenderTwinRev212[0][0] = inputL;
        // 宸﹀０閬?
        for (j = 0; j < 14; j++)
        {
            secondOrderFilter(&numCab_fenderTwinRev212[j][0], &denCab_fenderTwinRev212[j][0],&xCabL_fenderTwinRev212[j][0],&yCabL_fenderTwinRev212[j][0]);
            idx = j +1;
            xCabL_fenderTwinRev212[idx][0] = yCabL_fenderTwinRev212[j][0];
        }
        youtL = yCabL_fenderTwinRev212[13][0] * masterVol_fenderTwinRev212;

        xOut[0+i] = youtL;
        xOut[1+i] = youtL;
}