/*
 * @Author: llq
 * @Date: 2025-12-25 18:14:30
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 13:59:42
 * @Description: 2*12 Marshall
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Marshall212.c
 */

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Marshall212.h"

float xCabL_marshall212[13][3],yCabL_marshall212[12][3];
float numCab_marshall212[12][3] = { 
0.5650963f,-1.1222069f,0.5615089f,
0.5678459f,-1.1222069f,0.5620459f,
0.7063395f,-1.1222069f,0.4193723f,
0.6161314f,-1.1222069f,0.6020925f,
0.6941755f,-1.1222069f,0.4280325f,
0.5691790f,-1.1222069f,0.5647365f,
0.7098281f,-1.1222069f,0.4573776f,
1.1222069f,-0.4470115f,0.1192928f,
0.9052826f,-1.1222069f,0.7465394f,
1.1222069f,-0.0019152f,0.0000068f,
0.7664016f,1.1222069f,0.3815099f,
1.1222069f,1.0346701f,0.2373433f};

float denCab_marshall212[12][2] = { 
-1.9852127f,0.9856197f,
-1.9725429f,0.9926344f,
-0.9532888f,0.1141137f,
-1.8082797f,0.9590254f,
-1.9815304f,0.9891511f,
-1.9755580f,0.9885628f,
-1.5560929f,0.7487437f,
-1.4122665f,0.8728071f,
-0.7867219f,0.0434794f,
-0.2540172f,0.0445708f,
0.6558640f,0.2137553f,
-0.5310662f,0.0087314f};

void Marshall212_init()
{
    memset(xCabL_marshall212,0.0f,sizeof(xCabL_marshall212));
    memset(yCabL_marshall212,0.0f,sizeof(yCabL_marshall212));
}

void Marshall212_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float youtL;
    int j = 0;
	int idx = 0;
    float level_marshall212 = Marshall212_T1_knob[1]*d255;
    float masterVol_marshall212 = 20.0f * level_marshall212 * level_marshall212;
    int i=0;
        inputL = xin[0+i];
        inputR = xin[0+i];
        xCabL_marshall212[0][0] = inputL;
        // 宸﹀０閬?
        for (j = 0; j < 12; j++)
        {
            secondOrderFilter(&numCab_marshall212[j][0], &denCab_marshall212[j][0],&xCabL_marshall212[j][0],&yCabL_marshall212[j][0]);
            idx = j +1;
            xCabL_marshall212[idx][0] = yCabL_marshall212[j][0];
        }
        youtL = yCabL_marshall212[11][0] * masterVol_marshall212;


        xOut[0+i] = youtL;
        xOut[1+i] = youtL;
}