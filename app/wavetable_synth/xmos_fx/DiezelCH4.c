/*
 * @Author: llq
 * @Date: 2026-01-05 10:56:09
 * @LastEditors: llq
 * @LastEditTime: 2026-06-05 16:37:20
 * @Description: Diezel CH4
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\DiezelCH4.c
 */
#if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "DiezelCH4.h"

static float xInDe_diezelCH4[6];
static float yInDe_diezelCH4[6];

static float num2_diezelCH4[4] = {0.9683084507f, -2.904925352f, 2.904925352f, -0.9683084507f};
static float den2_diezelCH4[3] = {-2.93632746f, 2.873234417f, -0.9369057284f};
static float xPre2_diezelCH4[4];
static float yPre2_diezelCH4[4];

static float num3_diezelCH4[2] = {3.199389059f, -3.185504027f};
static float den3_diezelCH4[1] = {-0.9861149681f};
static float xPre3_diezelCH4[2];
static float yPre3_diezelCH4[2];

static float num4_diezelCH4[4] = {0.02249731256f, 0.06749193768f, 0.06749193768f, 0.02249731256f};
static float den4_diezelCH4[3] = {-0.7419033432f, -0.1058817838f, 0.02776362747f};
static float xPre4_diezelCH4[4];
static float yPre4_diezelCH4[4];

static float num6_diezelCH4[4];
static float den6_diezelCH4[4];
static float xPre6_diezelCH4[4];
static float yPre6_diezelCH4[4];

static float numPrese_diezelCH4[3];
static float denPrese_diezelCH4[3];
static float xPrePrese_diezelCH4[3];
static float yPrePrese_diezelCH4[3];


static float xNl_diezelCH4[3];
static float yNl_diezelCH4[3];
static float xPrev_diezelCH4 = 0.0f;

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

float nonlinear_diezelCH4(float xcurr, float xprev)
{
	float absx, signx, x, y, x1, x2;
	float xnl[8], ynl[8];
	int i = 0;
	int N = 8;
	float p1, p2, q1, q2;
    float diff;
    diff = (xcurr - xprev) * 0.125f;
    for (i = 0;i<N;i++){
        xnl[i] = xprev + diff * (float)(i+1);
    }

	p1 =   1.247e+05f;
	q1 =   1.716e+05f;
	q2 =        8536.0f;
	
	for (i = 0;i < N;i++)
	{
		x = xnl[i];
		absx = x < 0.0f ? -x:x;
		signx = x < 0.0f? -1.0f : 1.0f;
		x1 = absx;
		x2 = absx*absx;
    	ynl[i] = signx*((p1*x1)/(x2 + q1*x1 + q2));
        xNl_diezelCH4[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_diezelCH4, yNl_diezelCH4);
	}
	return yNl_diezelCH4[0];
}

void ParamCal1_diezelCH4(float Rb, float Rm, float Rt,float *num,float *den)
{
    float RBRM = Rb*Rm;
    float RBRT = Rb*Rt;
	num[0] = 0.01749767536f*Rb + 0.0006746259054f*Rm + 0.0009527181047f*Rt + 0.713011988f*RBRM + RBRT;
	num[1] = - 0.01539241435f*Rb - 0.0005831441023f*Rm - 0.0009412212892f*Rt - 2.041217116f*RBRM - 3.0f*RBRT;
	num[2] = 1.943398268f*Rb*Rm - 0.0006746259054f*Rm - 0.0009527181047f*Rt - 0.01749767536f*Rb + 3.0f*RBRT;
	num[3] = 0.01539241435f*Rb + 0.0005831441023f*Rm + 0.0009412212892f*Rt - 0.6151931402f*RBRM - RBRT;

	den[0] = 1.102048541f*Rb + 0.0006746259054f*Rm + 0.713011988f*RBRM + 0.001033271245f;
	den[1] = - 3.09994328f*Rb - 0.0005831441023f*Rm - 2.041217116f*RBRM - 0.0008596960829f;
	den[2] = 2.897951459f*Rb - 0.0006746259054f*Rm + 1.943398268f*RBRM - 0.001031327114f;
	den[3] = 0.0005831441023f*Rm - 0.9000567199f*Rb - 0.6151931402f*RBRM + 0.0008616402143f;

}

void ParamCal2_diezelCH4(float R, float *num, float *den)
{
	num[0] = 1.035392451f - 0.999421631f*R;
	num[1] = 1.998843262f*R - 2.063322077f;
	num[2] = 1.027929626f - 0.999421631f*R;

	den[0] = R - 1.014498698f;
	den[1] = 2.020331882f - 1.998843262f*R;
	den[2] = 0.998843262f*R - 1.005841821f;
}

void DiezelCH4_init()
{
    memset(xInDe_diezelCH4, 0.0f, sizeof(xInDe_diezelCH4));
    memset(yInDe_diezelCH4, 0.0f, sizeof(yInDe_diezelCH4));
    memset(xPre2_diezelCH4,0.0,sizeof(xPre2_diezelCH4));
    memset(yPre2_diezelCH4,0.0,sizeof(yPre2_diezelCH4));
    memset(xPre3_diezelCH4,0.0f,sizeof(xPre3_diezelCH4));
    memset(yPre3_diezelCH4,0.0f,sizeof(yPre3_diezelCH4));
    memset(xPre4_diezelCH4,0.0f,sizeof(xPre4_diezelCH4));
    memset(yPre4_diezelCH4,0.0f,sizeof(yPre4_diezelCH4));
    memset(num6_diezelCH4,0.0f,sizeof(num6_diezelCH4));
    memset(den6_diezelCH4,0.0f,sizeof(den6_diezelCH4));
    memset(xPre6_diezelCH4,0.0f,sizeof(xPre6_diezelCH4));
    memset(yPre6_diezelCH4,0.0f,sizeof(yPre6_diezelCH4));
    memset(numPrese_diezelCH4,0.0f,sizeof(numPrese_diezelCH4));
    memset(denPrese_diezelCH4,0.0f,sizeof(denPrese_diezelCH4));
    memset(xPrePrese_diezelCH4,0.0f,sizeof(xPrePrese_diezelCH4));
    memset(yPrePrese_diezelCH4,0.0f,sizeof(yPrePrese_diezelCH4));
    memset(xNl_diezelCH4,0.0f,sizeof(xNl_diezelCH4));
    memset(yNl_diezelCH4,0.0f,sizeof(yNl_diezelCH4));
}

void DiezelCH4_process(float *xin, float *xOut)
{
    float inputL, inputR, input;
    float xoutamp_diezelCH4;
	float youtamp_diezelCH4;
    float xnl_diezelCH4,ynl_diezelCH4;

    float knobGain = A25[DiezelCH4_T1_knob[0]];
    float knobBass = A25[DiezelCH4_T1_knob[1]];
    float knobMid = A25[DiezelCH4_T1_knob[2]];
    float knobTreble = A25[DiezelCH4_T1_knob[3]];
    float knobPresence = 0.8f + 0.2f*(DiezelCH4_T1_knob[4]*d255);
    float knobVol = A25[DiezelCH4_T1_knob[5]];

    ParamCal1_diezelCH4(knobBass,knobMid,knobTreble,num6_diezelCH4,den6_diezelCH4);
    ParamCal2_diezelCH4(knobPresence, numPrese_diezelCH4, denPrese_diezelCH4);

        inputL = xin[0];
        inputR = xin[0];

        xInDe_diezelCH4[0] = inputL;
        preproc(xInDe_diezelCH4, yInDe_diezelCH4);
        xPre2_diezelCH4[0] = yInDe_diezelCH4[3] * 2000.0f * knobGain;
        thirdOrderFilter(num2_diezelCH4,den2_diezelCH4,xPre2_diezelCH4,yPre2_diezelCH4);
        xPre3_diezelCH4[0] = yPre2_diezelCH4[0];
		firstOrderFilter(num3_diezelCH4,den3_diezelCH4,xPre3_diezelCH4,yPre3_diezelCH4);
        xPre4_diezelCH4[0] = yPre3_diezelCH4[0];
		thirdOrderFilter(num4_diezelCH4,den4_diezelCH4,xPre4_diezelCH4,yPre4_diezelCH4);
        xnl_diezelCH4 = yPre4_diezelCH4[0];
		ynl_diezelCH4 = nonlinear_diezelCH4(xnl_diezelCH4, xPrev_diezelCH4);
        xPrev_diezelCH4 = xnl_diezelCH4;
        xPre6_diezelCH4[0] = ynl_diezelCH4;
		thirdOrderFilterVA(num6_diezelCH4, den6_diezelCH4, xPre6_diezelCH4, yPre6_diezelCH4);
        xPrePrese_diezelCH4[0] = yPre6_diezelCH4[0];
		secondOrderFilterVA(numPrese_diezelCH4, denPrese_diezelCH4, xPrePrese_diezelCH4, yPrePrese_diezelCH4);
        xoutamp_diezelCH4 = yPrePrese_diezelCH4[0]*5.0f*knobVol;
    
        xOut[0] = xoutamp_diezelCH4;
        xOut[1] = xoutamp_diezelCH4;


}

#endif