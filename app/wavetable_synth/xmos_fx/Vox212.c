/*
 * @Author: llq
 * @Date: 2025-12-29 09:28:08
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 14:35:47
 * @Description: cabSim
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Vox212.c
 */
#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Vox212.h"

#define VOX212_SOSNUM 22


//-D:\llq\绠楁硶\鎴翠箰绠变綋鏂囦欢\Ownhammer闊冲搷\OwnHammer - (r)Evolution Debut Bundle\OwnHammer - (r)Evolution Debut Bundle\212 VC30 BLU-12C\OH_212-VC30_BLU-12C_Mono\OwnHammer IRs\Mono 48000 Hz\212 VC30\BLU-12C\Mics\OH 212 VC30 BLU-12C 87.wav-//
float xCabL_vox212[VOX212_SOSNUM+1][3],yCabL_vox212[VOX212_SOSNUM][3];

float numCab_vox212[VOX212_SOSNUM][3] = { 
0.9896968f,-1.9777193f,0.9883538f,
1.0135831f,-1.9777193f,0.9670373f,
0.0535785f,-0.0232450f,0.0007338f,
0.6921432f,-0.1145922f,0.0015906f,
0.0381941f,0.0108148f,0.0006659f,
0.3972289f,-0.6762347f,0.2790079f,
0.9924731f,-1.8256150f,0.9768341f,
1.1426849f,-1.9777193f,1.1217465f,
1.1566751f,-1.9777193f,1.0651220f,
0.1794721f,-0.3384837f,0.1677744f,
0.2371659f,-0.4145091f,0.2215296f,
0.4366377f,0.0256699f,0.0189979f,
1.0037445f,-1.9777193f,0.9931486f,
0.6562236f,-0.0921731f,1.9777193f,
1.0053340f,-1.9777193f,0.9902859f,
1.2356822f,-1.9777193f,1.1356624f,
1.9777193f,1.7163298f,0.2183361f,
1.0285646f,-1.9777193f,1.0111220f,
1.9777193f,0.8437436f,0.0746493f,
1.9777193f,-0.0803512f,0.0075742f,
1.9777193f,0.1630393f,0.0045553f,
1.9777193f,1.5133790f,0.1945439f};

float denCab_vox212[VOX212_SOSNUM][2] = { 
-1.8924470f,0.8953354f,
-1.9953410f,0.9957138f,
-1.3227333f,0.3552198f,
-1.9759672f,0.9935414f,
-1.2760247f,0.8791430f,
-1.8530332f,0.9188797f,
-1.9128258f,0.9168228f,
-1.7945410f,0.9391553f,
-1.4113605f,0.7570164f,
-1.6974751f,0.9459209f,
-1.9538001f,0.9745989f,
-1.9278691f,0.9860228f,
-0.6359003f,0.1173511f,
0.1283769f,0.1617880f,
-1.5684986f,0.8880015f,
-1.7173432f,0.9612426f,
0.6651636f,0.0988326f,
1.0185846f,0.1488584f,
-1.7535456f,0.9092571f,
-1.7203123f,0.9028467f,
0.3672885f,0.0213814f,
0.4234472f,0.0427846f};

void Vox212_init()
{
    memset(xCabL_vox212,0.0f,sizeof(xCabL_vox212));
    memset(yCabL_vox212,0.0f,sizeof(yCabL_vox212));
}

void Vox212_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float level_vox212 = Vox212_T1_knob[1]*d255;
    float masterVol_vox212 = 20.0f * level_vox212 * level_vox212;

    float youtL;
    int j = 0;
	int idx = 0;
    int i=0;
        inputL = xin[0+i];
        inputR = xin[0+i];
        xCabL_vox212[0][0] = inputL;
        for (j = 0; j < VOX212_SOSNUM; j++)
        {
            secondOrderFilter(&numCab_vox212[j][0], &denCab_vox212[j][0],&xCabL_vox212[j][0],&yCabL_vox212[j][0]);
            idx = j +1;
            xCabL_vox212[idx][0] = yCabL_vox212[j][0];
        }
        youtL = yCabL_vox212[VOX212_SOSNUM-1][0] * masterVol_vox212;

        xOut[0+i] = youtL;
        xOut[1+i] = youtL;
}