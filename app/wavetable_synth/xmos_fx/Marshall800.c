/*
 * @Author: llq
 * @Date: 2025-12-25 11:55:29
 * @LastEditors: llq
 * @LastEditTime: 2026-05-15 15:06:57
 * @Description: cabSim
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\Marshall800.c
 */
#if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Marshall800.h"

#include "xcore_math.h"


static float xInDe_JCM800[6];
static float yInDe_JCM800[6];
static float numPf_JCM800[7] = {34.43264117f, 0.0f, -34.43264117f, 0.9810839243f, -0.8928348788f, 0.9804361892f, -0.8922087314f};
static float denPf_JCM800[4] = {-0.7996910652f, -0.1727519474f, -0.8701784462f, -0.8269706543f};
static float xPf_JCM800[7];
static float yPf_JCM800[7];
static float numEQ_JCM800[4];
static float denEQ_JCM800[4];
static float xEQ_JCM800[4];
static float yEQ_JCM800[4];
static float xNl_JCM800[3];
static float yNl_JCM800[3];
static float xPrev_JCM800 = 0.0f;

static float numPres_JCM800[3];
static float denPres_JCM800[2];
static float xPres_JCM800[3];
static float yPres_JCM800[3];

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

static void prefilter(float *xin, float *yin){
	secondOrderFilter(numPf_JCM800,denPf_JCM800,xin,yin);
	xin[3] = yin[0];
	firstOrderFilter(&numPf_JCM800[3],&denPf_JCM800[2],&xin[3],&yin[3]);
	xin[5] = yin[3];
	firstOrderFilter(&numPf_JCM800[5],&denPf_JCM800[3],&xin[5],&yin[5]);
}

void ParaCalc1_JCM800(float Rb, float Rm, float Rt, float *num, float *den){
	float Rm2 = Rm*Rm;
	float RbRm = Rb*Rm;
	float RmRt = Rm*Rt;
	float RbRt = Rb*Rt;
    
	num[0] = 0.0009686363816f*Rt - 0.04908794007f*Rm - Rb + 0.4819816108f*RbRm + 0.5077970669f*RbRt + 0.02538985334f*RmRt + 0.02409908053f*Rm2 - 0.001880696306f;
	num[1] = 2.898892711f*Rb + 0.1441809418f*Rm - 0.0009548373563f*Rt - 1.36677953f*RbRm - 1.523391201f*RbRt - 0.07616956003f*RmRt - 0.06833897651f*Rm2 + 0.001718531068f;
	num[2] = 1.287614228f*RbRm - 0.1409512652f*Rm - 0.0009686363816f*Rt - 2.800784107f*Rb + 1.523391201f*RbRt + 0.07616956003f*RmRt + 0.06438071142f*Rm2 + 0.001880696306f;
	num[3] = 0.9018913962f*Rb + 0.04585826351f*Rm + 0.0009548373563f*Rt - 0.4028163089f*RbRm - 0.5077970669f*RbRt - 0.02538985334f*RmRt - 0.02014081544f*Rm2 - 0.001718531068f;

	den[0] = 0.4819816108f*RbRm - 0.05164575089f*Rm - 1.051156216f*Rb + 0.02409908053f*Rm2 - 0.001978278146f;
	den[1] = 2.950048927f*Rb + 0.1467387526f*Rm - 1.36677953f*RbRm - 0.06833897651f*Rm2 + 0.001619559093f;
	den[2] = 1.287614228f*RbRm - 0.1383934544f*Rm - 2.749627891f*Rb + 0.06438071142f*Rm2 + 0.001975497878f; 
	den[3] = 0.8507351797f*Rb + 0.04330045269f*Rm - 0.4028163089f*RbRm - 0.02014081544f*Rm2 - 0.001622339361f;

 
}

void ParamCal2_JCM800(float R, float *num, float *den)
{
    float G = -12 + 24 * R;
    float fc = 3000.0f;
    float K, V0, D1, D2;
    float K2, S2, S2V0;
    
    V0 = powf(10.0f, G*0.05f);
    K = 0.198912367379658f;
    K2 = 0.039566129896580f;
    S2 = 1.414213562373095f;
    S2V0 = sqrtf(2 * V0);
    D1 = 1.0f / (1.0f + S2 * K + K2);
    D2 = 1.0f / (1.0f + S2V0 * K + V0 * K2);
    if (G >= 0)
    {
        num[0] = (V0 + S2V0 * K + K2) * D1;
        num[1] = (2 * (K2 - V0)) * D1;
        num[2] = (V0 - S2V0 * K + K2) * D1;
        den[0] = (2 * (K2 - 1)) * D1;
        den[1] = (1 - S2 * K + K2) * D1;
    }
    else
    {
        num[0] = (V0 * (1 + S2 * K + K2)) * D2;
        num[1] = (2 * V0 * (K2 - 1)) * D2;
        num[2] = (V0 * (1 - S2 * K + K2)) * D2;
        den[0] = (2 * (V0 * K2 - 1)) * D2;
        den[1] = (1 - S2V0 * K + V0 * K2) * D2;
    }
}


float nonlinear_JCM800(float xcurr, float xprev)
{
	float y;
	float xnl[8], ynl[8];
	float diff;
	int i,N =8;
	float p1 = 0.9403f;
	float q1 = 0.1677f;
    diff = (xcurr - xprev) * 0.125f;
    for (i = 0;i<N;i++){
        xnl[i] = xprev + diff * (float)(i+1);
    }
	for (i=0;i<N;i++)
	{
		ynl[i] = (p1*xnl[i])/(fabsf(xnl[i])+q1);
        xNl_JCM800[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_JCM800, yNl_JCM800);
	}
	return yNl_JCM800[0];
}

void JCM800_init()
{
	memset(xInDe_JCM800, 0.0f, sizeof(xInDe_JCM800));
    memset(yInDe_JCM800, 0.0f, sizeof(yInDe_JCM800));
    memset(xPf_JCM800,0.0f,sizeof(xPf_JCM800));
    memset(yPf_JCM800,0.0f,sizeof(yPf_JCM800));
    memset(xEQ_JCM800,0.0f,sizeof(xEQ_JCM800));
    memset(yEQ_JCM800,0.0f,sizeof(yEQ_JCM800));
    memset(numEQ_JCM800,0.0f,sizeof(numEQ_JCM800));
    memset(denEQ_JCM800,0.0f,sizeof(denEQ_JCM800));

    memset(xNl_JCM800,0.0f,sizeof(xNl_JCM800));
    memset(yNl_JCM800,0.0f,sizeof(yNl_JCM800));

    memset(xPres_JCM800,0.0f,sizeof(xPres_JCM800));
    memset(yPres_JCM800,0.0f,sizeof(yPres_JCM800));
    memset(numPres_JCM800,0.0f,sizeof(numPres_JCM800));
    memset(denPres_JCM800,0.0f,sizeof(denPres_JCM800));
}

void JCM800_process(float *xin, float *xOut)
{
    float inputL, inputR, input;
    
    float Rv_JCM800 = A25[JCM800_T1_knob[4]]*2.0f;
	float Rg_JCM800 = A25[JCM800_T1_knob[0]] + 0.1f;
	float Rb_JCM800 = A25[JCM800_T1_knob[1]];
	float Rm_JCM800 = 1 - A25[JCM800_T1_knob[2]];
	float Rt_JCM800 = C25[JCM800_T1_knob[3]];
    float Rp_JCM800 = JCM800_T1_knob[5]*d255;

	float xoutamp_JCM800, xoutcab_JCM800;
	float youtamp_JCM800, youtcab_JCM800;
	float xnl_JCM800,ynl_JCM800;
	float gain_JCM800,level_JCM800,bass_JCM800,mid_JCM800,treble_JCM800;
    float inputL_JCM800, inputR_JCM800, input_JCM800;
    
    gain_JCM800 = Rg_JCM800;
    level_JCM800 = Rv_JCM800;
    bass_JCM800 = Rb_JCM800;
    mid_JCM800 = Rm_JCM800;
    treble_JCM800 = Rt_JCM800;

	ParaCalc1_JCM800(bass_JCM800,mid_JCM800,treble_JCM800, numEQ_JCM800, denEQ_JCM800);
    ParamCal2_JCM800(Rp_JCM800, numPres_JCM800, denPres_JCM800);
    int i = 0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xInDe_JCM800[0] = inputL;
        preproc(xInDe_JCM800, yInDe_JCM800);
        xPf_JCM800[0] = yInDe_JCM800[3];
        prefilter(xPf_JCM800,yPf_JCM800);
        xnl_JCM800 = yPf_JCM800[5] * gain_JCM800 * 31.6228f;
        ynl_JCM800 = nonlinear_JCM800(xnl_JCM800,xPrev_JCM800);
        xPrev_JCM800 = xnl_JCM800;
        xEQ_JCM800[0] = ynl_JCM800;
        thirdOrderFilterVA(numEQ_JCM800,denEQ_JCM800,xEQ_JCM800,yEQ_JCM800);
        xPres_JCM800[0] = yEQ_JCM800[0];
        secondOrderFilter(numPres_JCM800,denPres_JCM800,xPres_JCM800,yPres_JCM800);
        xoutamp_JCM800 = yPres_JCM800[0]*level_JCM800;

        xOut[0+i] = xoutamp_JCM800;
        xOut[1+i] = xoutamp_JCM800;

}
#endif