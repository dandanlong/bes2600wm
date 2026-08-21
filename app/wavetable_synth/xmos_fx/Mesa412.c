/*
 * @Author: llq
 * @Date: 2025-12-25 17:54:16
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 14:15:19
 * @Description: 4*12 Mesa
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Mesa412.c
 */

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Mesa412.h"

#define MESA412_SOSNUM 21

//-----------------------------------------------D:\llq\绠楁硶\鎴翠箰绠变綋鏂囦欢\York Audio Guitar IR Library 28濂梊York Audio MES 412 OS-V2\48k (Fractal, Line 6, etc.)\Minimum Phase Singles\YA MES 412 OS U47-3.wav---------------------------\\

float xCabL_mesa412[MESA412_SOSNUM+1][3],yCabL_mesa412[MESA412_SOSNUM][3];
float numCab_mesa412[MESA412_SOSNUM][3] = { 
0.2535983f,-0.4271075f,0.1798305f,
0.7331818f,-1.3960759f,0.6803405f,
0.7067842f,-1.3960759f,0.7037108f,
0.7293724f,-1.3960759f,0.7013220f,
0.7202073f,-1.3960759f,0.6776885f,
0.7689645f,-1.3960759f,0.7318600f,
0.7045200f,-1.3960759f,0.6953749f,
0.7086703f,-1.3960759f,0.7057514f,
0.7445077f,-1.3960759f,0.6898920f,
0.8977439f,-1.3960759f,0.8315842f,
0.8086392f,-1.3960759f,0.5874867f,
0.7444537f,-1.3960759f,0.7275338f,
1.3960759f,-0.9765816f,0.3313676f,
0.7548130f,-1.3960759f,0.6589689f,
1.3960759f,-1.3915798f,1.0774110f,
0.8789463f,-1.3960759f,0.7158386f,
1.3960759f,1.0942089f,0.2414824f,
1.3960759f,0.2645011f,0.0954144f,
0.3621268f,0.3067125f,0.2203813f,
0.7376509f,0.4929292f,0.0495974f,
0.4362469f,-0.0280468f,0.3223825f};

float denCab_mesa412[MESA412_SOSNUM][2] = { 
-1.9766557f,0.9773187f,
-1.9719342f,0.9977875f,
-1.3825947f,0.7233352f,
-1.9496058f,0.9735511f,
-1.9751961f,0.9790602f,
-1.8386308f,0.9271719f,
-1.5314613f,0.6271399f,
-1.9768190f,0.9841186f,
-1.8536366f,0.9560685f,
-1.9426912f,0.9884472f,
-1.7332788f,0.8955339f,
-1.9720077f,0.9922556f,
-1.3537562f,0.7517743f,
-1.1740175f,0.1948560f,
-0.1126680f,0.0493799f,
-0.1209507f,0.0051930f,
-1.2878510f,0.6157815f,
-0.1585626f,0.0137162f,
-1.8363155f,0.8997723f,
-1.5067574f,0.8556021f,
0.6816116f,0.1524044f};


void Mesa412_init()
{
    memset(xCabL_mesa412,0.0f,sizeof(xCabL_mesa412));
    memset(yCabL_mesa412,0.0f,sizeof(yCabL_mesa412));
}

void Mesa412_process(float *xin, float *xOut)
{
    float input, inputR, inputL;
    float level_mesa412 = Mesa412_T1_knob[1]*d255;
    float masterVol_mesa412 = 20.0f * level_mesa412 * level_mesa412;
    float youtL;
    int j = 0;
	int idx = 0;
    int i=0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xCabL_mesa412[0][0] = inputL;
        for (j = 0; j < MESA412_SOSNUM; j++)
        {
            secondOrderFilter(&numCab_mesa412[j][0], &denCab_mesa412[j][0],&xCabL_mesa412[j][0],&yCabL_mesa412[j][0]);
            idx = j +1;
            xCabL_mesa412[idx][0] = yCabL_mesa412[j][0];
        }
        youtL = yCabL_mesa412[MESA412_SOSNUM-1][0] * masterVol_mesa412;

        xOut[0+i] = youtL;
        xOut[1+i] = youtL;

}


