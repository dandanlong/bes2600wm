/*
 * @Author: llq
 * @Date: 2025-12-25 18:20:06
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 14:00:04
 * @Description: 4*12 Marshall
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Marshall412.c
 */

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Marshall412.h"

float xCabL_marshall412[13][3],yCabL_marshall412[12][3];
float numCab_marshall412[12][3] = { 
0.1570971f,-0.3001625f,0.1430700f,
0.2143306f,-0.4141132f,0.2020537f,
0.6964494f,-1.3917852f,0.6958748f,
0.7021654f,-1.3917852f,0.6929906f,
0.7896986f,-1.3917852f,0.7173133f,
0.7324106f,-1.3917852f,0.6939213f,
1.1893357f,-1.3917852f,0.8982006f,
1.3881933f,-1.3917852f,0.0567016f,
0.7098011f,-1.3917852f,0.7041624f,
1.3917852f,-0.5696478f,0.0379405f,
1.3917852f,0.7114120f,0.0650601f,
1.3917852f,1.0071382f,0.0154815f};

float denCab_marshall412[12][2] = { 
-1.9962538f,0.9970676f,
-1.9900241f,0.9902970f,
-1.9816687f,0.9855043f,
-1.9135146f,0.9507964f,
-1.7877513f,0.9442117f,
-1.5800378f,0.9208191f,
-1.9743799f,0.9827611f,
-1.9571727f,0.9872688f,
-0.1889848f,0.2240374f,
-1.8185868f,0.8906187f,
0.4324229f,0.0846421f,
-0.2839683f,0.0030294f};

void Marshall412_init()
{
    memset(xCabL_marshall412,0.0f,sizeof(xCabL_marshall412));
    memset(yCabL_marshall412,0.0f,sizeof(yCabL_marshall412));
}

void Marshall412_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float youtL;
    int j = 0;
	int idx = 0;
    float level_marshall412 = Marshall412_T1_knob[1]*d255;
    float masterVol_marshall412 = 20.0f * level_marshall412 * level_marshall412;

        int i=0;
        inputL = xin[0+i];
        inputR = xin[0+i];
        xCabL_marshall412[0][0] = inputL;
        for (j = 0; j < 12; j++)
        {
            secondOrderFilter(&numCab_marshall412[j][0], &denCab_marshall412[j][0],&xCabL_marshall412[j][0],&yCabL_marshall412[j][0]);
            idx = j +1;
            xCabL_marshall412[idx][0] = yCabL_marshall412[j][0];
        }
        youtL = yCabL_marshall412[11][0] * masterVol_marshall412;

        xOut[0+i] = youtL;
        xOut[1+i] = youtL;
}