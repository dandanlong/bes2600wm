/*
 * @Author: llq
 * @Date: 2026-05-13 10:30:22
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 11:28:38
 * @Description: cabSim
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Diezel412.c
 */
#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Diezel412.h"

#define DIEZEL412_SOSNUM 21

//-----------------------------------------------------------D:\llq\绠楁硶\鎴翠箰绠变綋鏂囦欢\Ownhammer闊冲搷\OwnHammer\412 DZL-RL\48\Wav-200ms\V30\Mics\OH 412 DZL-RL V30 57-00.wav----------------------------------------------------------------//

float xCabL_Diezel412[DIEZEL412_SOSNUM+1][3],yCabL_Diezel412[DIEZEL412_SOSNUM][3];
float numCab_Diezel412[DIEZEL412_SOSNUM][3] = { 
0.0320676f,-0.0551541f,0.0230896f,
0.9256446f,-1.8390622f,0.9191089f,
0.9307828f,-1.8390622f,0.9281104f,
1.1938817f,-1.8390622f,0.6473816f,
0.9614246f,-1.8390622f,0.9394363f,
0.1025558f,-0.1887235f,0.1011275f,
0.2049789f,-0.3616853f,0.1965946f,
0.9634030f,-1.8390622f,0.9531456f,
0.2829344f,-0.3070260f,0.1616049f,
0.6637111f,-0.8981977f,0.6292229f,
0.4348819f,0.0451639f,0.0006077f,
0.9782070f,-1.8390622f,0.9614516f,
1.0995224f,-1.8390622f,1.0307780f,
0.9324035f,-1.8390622f,0.9311869f,
1.8390622f,-1.0185056f,0.5895979f,
1.8390622f,-0.2110906f,0.0233249f,
1.2320820f,-1.8390622f,0.9926789f,
1.8390622f,-1.1845102f,0.3387027f,
0.7880336f,1.8390622f,1.3291458f,
1.8390622f,1.5853761f,0.3037924f,
1.8390622f,0.1211469f,0.0015328f};

float denCab_Diezel412[DIEZEL412_SOSNUM][2] = { 
-1.6610686f,0.6748436f,
-1.9882096f,0.9950649f,
-1.9865383f,0.9868284f,
-1.6580316f,0.8275350f,
-1.9754713f,0.9972897f,
-1.9103341f,0.9897326f,
-1.9728775f,0.9991398f,
-1.9088759f,0.9721729f,
-1.7879984f,0.9279560f,
-1.8818594f,0.9792944f,
-1.0930043f,0.5162799f,
-1.3611253f,0.9080645f,
-1.2983936f,0.9172814f,
-1.7181368f,0.9253835f,
-1.4709914f,0.8211425f,
0.6969206f,0.0806586f,
-0.6417616f,0.9377931f,
-1.2242869f,0.7543395f,
1.3232594f,0.8790036f,
-1.7568759f,0.9070160f,
-1.6426543f,0.9201915f};

void Diezel412_init(void)
{
    memset(xCabL_Diezel412,0.0f,sizeof(xCabL_Diezel412));
    memset(yCabL_Diezel412,0.0f,sizeof(yCabL_Diezel412));
}

void Diezel412_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float level_Diezel412 = Diezel412_T1_knob[1]*d255;
    float masterVol_Diezel412 = 20.0f * level_Diezel412 * level_Diezel412;

    float youtL;
    int j = 0;
	int idx = 0;


        inputL = xin[0];
        inputR = xin[0];
        xCabL_Diezel412[0][0] = inputL;
        for (j = 0; j < DIEZEL412_SOSNUM; j++)
        {
            secondOrderFilter(&numCab_Diezel412[j][0], &denCab_Diezel412[j][0],&xCabL_Diezel412[j][0],&yCabL_Diezel412[j][0]);
            idx = j +1;
            xCabL_Diezel412[idx][0] = yCabL_Diezel412[j][0];
        }
        youtL = yCabL_Diezel412[DIEZEL412_SOSNUM-1][0] * masterVol_Diezel412;

        xOut[0] = youtL;
        xOut[1] = youtL;
}