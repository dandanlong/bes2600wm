/*
 * @Author: llq
 * @Date: 2025-12-26 11:57:21
 * @LastEditors: llq
 * @LastEditTime: 2026-05-15 14:50:30
 * @Description: Fender Twin Reverb
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\FenderTwinReverb.c
 */

 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "FenderTwinReverb.h"

static float xInDe_TwinRev[6];
static float yInDe_TwinRev[6];
static float xPf1_TwinRev[3];
static float yPf1_TwinRev[3];
static float xPf2_TwinRev[3];
static float yPf2_TwinRev[3];
static float xEQ_TwinRev[4];
static float yEQ_TwinRev[4];
static float xPost1_TwinRev[2];
static float yPost1_TwinRev[2];
static float xPost2_TwinRev[3];
static float yPost2_TwinRev[3];
static float numPf1_TwinRev[3] = {-0.7786052612f, -0.3042377545f, 0.4743675067f};
static float denPf1_TwinRev[2] = {0.03960990586f, -0.431134397f};
static float numPf2_TwinRev[3] = {0.6060758865f, 1.212151773f, 0.6060758865f};
static float denPf2_TwinRev[2] = {1.157412938f, 0.2668906077f};
static float numEQ_TwinRev[4];
static float denEQ_TwinRev[4];
static float numPost1_TwinRev[2] = {0.5656108597f, 0.5656108597f};
static float denPost1_TwinRev[1] = {0.1312217195f};
static float numPost2_TwinRev[3] = {0.5028046596f, 0.0f, -0.5028046596f};
static float denPost2_TwinRev[2] = {-0.9316619903f, -0.05881519415f};
static float xNl_TwinRev[3];
static float yNl_TwinRev[3];
static float xPrev_TwinRev = 0.0f;

static float A[256] = {
		0.000000,0.000000,0.000000,0.000199,0.000398,0.000598,0.000797,0.000996,
		0.001195,0.001394,0.001594,0.001793,0.001992,0.002191,0.002390,0.002590,
		0.002789,0.002988,0.003187,0.003386,0.003586,0.003785,0.003984,0.004183,
		0.004383,0.004582,0.004781,0.004980,0.005179,0.005379,0.005578,0.005777,
		0.005976,0.006176,0.006375,0.006574,0.006774,0.006973,0.007173,0.007372,
		0.007571,0.007771,0.007971,0.008170,0.008370,0.008570,0.008770,0.008970,
		0.009170,0.009370,0.009571,0.009771,0.009972,0.010173,0.010374,0.010576,
		0.010777,0.010979,0.011182,0.011384,0.011587,0.011791,0.011995,0.012199,
		0.012404,0.012609,0.012816,0.013022,0.013230,0.013438,0.013648,0.013858,
		0.014069,0.014281,0.014494,0.014709,0.014925,0.015142,0.015361,0.015582,
		0.015804,0.016028,0.016254,0.016482,0.016712,0.016945,0.017180,0.017417,
		0.017658,0.017902,0.018148,0.018399,0.018652,0.018910,0.019171,0.019437,
		0.019707,0.019981,0.020261,0.020546,0.020836,0.021132,0.021434,0.021742,
		0.022057,0.022379,0.022709,0.023046,0.023391,0.023745,0.024108,0.024480,
		0.024862,0.025254,0.025657,0.026071,0.026497,0.026935,0.027385,0.027849,
		0.028327,0.028820,0.029328,0.029851,0.030392,0.030949,0.031524,0.032118,
		0.032731,0.033364,0.034019,0.034695,0.035394,0.036117,0.036864,0.037636,
		0.038435,0.039262,0.040117,0.041001,0.041917,0.042864,0.043844,0.044858,
		0.045908,0.046994,0.048118,0.049282,0.050487,0.051734,0.053024,0.054359,
		0.055742,0.057172,0.058652,0.060184,0.061770,0.063410,0.065107,0.066863,
		0.068680,0.070559,0.072503,0.074513,0.076592,0.078742,0.080966,0.083264,
		0.085641,0.088098,0.090637,0.093261,0.095973,0.098775,0.101670,0.104661,
		0.107750,0.110941,0.114236,0.117639,0.121152,0.124778,0.128521,0.132385,
		0.136372,0.140486,0.144730,0.149109,0.153625,0.158283,0.163086,0.168038,
		0.173144,0.178407,0.183832,0.189422,0.195183,0.201119,0.207234,0.213532,
		0.220020,0.226701,0.233580,0.240662,0.247954,0.255459,0.263183,0.271132,
		0.279311,0.287725,0.296382,0.305286,0.314443,0.323860,0.333543,0.343498,
		0.353732,0.364252,0.375063,0.386173,0.397589,0.409318,0.421368,0.433745,
		0.446457,0.459512,0.472917,0.486681,0.500812,0.515317,0.530206,0.545486,
		0.561167,0.577257,0.593765,0.610700,0.628072,0.645890,0.664163,0.682902,
		0.702116,0.721816,0.742011,0.762713,0.783931,0.805678,0.827962,0.850797,
		0.874193,0.898161,0.922714,0.947864,0.973621,1.000000,1.000000,1.000000};

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

void ParamCal1_TwinRev(float R1, float R2, float R3, float *num,float *den)
{
	float R12, R13, R23, R22;
	R12 = R1*R2;
	R13 = R1*R3;
	R23 = R2*R3;
	R22 = R2*R2;
	num[0] = 0.001123657622f*R3 - 0.03982405314f*R2 - R1 + 0.1350237668f*R12 + 0.8616246749f*R13 + 0.034464987f*R23 + 0.005400950671f*R22 - 0.001299604487f;
	num[1] = 2.963866124f*R1 + 0.1184161905f*R2 - 0.001122066265f*R3 - 0.3762786757f*R12 - 2.584874025f*R13 - 0.103394961f*R23 - 0.01505114703f*R22 + 0.001260520744f;
	num[2] = 0.3474860512f*R12 - 0.1173362876f*R2 - 0.001123657622f*R3 - 2.929008518f*R1 + 2.584874025f*R13 + 0.103394961f*R23 + 0.01389944205f*R22 + 0.001299604487f;
	num[3] = 0.9651423932f*R1 + 0.03874415021f*R2 + 0.001122066265f*R3 - 0.1062311422f*R12 - 0.8616246749f*R13 - 0.034464987f*R23 - 0.004249245689f*R22 - 0.001260520744f;
	den[0] = 0.1350237668f*R12 - 0.04556821763f*R2 - 1.143604112f*R1 + 0.005400950671f*R22 - 0.001486880758f;
	den[1] = 3.107470237f*R1 + 0.124160355f*R2 - 0.3762786757f*R12 - 0.01505114703f*R22 + 0.001072979247f;
	den[2] = 0.3474860512f*R12 - 0.1115921231f*R2 - 2.785404405f*R1 + 0.01389944205f*R22 + 0.001486350305f;
	den[3] = 0.8215382808f*R1 + 0.03299998571f*R2 - 0.1062311422f*R12 - 0.004249245689f*R22 - 0.0010735097f;
}

float nonlinear_TwinRev(float xcurr, float xprev)
{
	float y;
	float xnl[8], ynl[8];
	int i,N =8;
	float p1 =  0.2375f;
	float q1 =  0.4189f;
	float diff;
	diff = (xcurr - xprev) * 0.125f;
	for (i = 0;i<N;i++){
		xnl[i] = xprev + diff * (float)(i+1);
	}
	for (i=0;i<N;i++)
	{
		ynl[i] = (p1*xnl[i])/(fabsf(xnl[i])+q1);
		ynl[i] = 7.0f*ynl[i];
		xNl_TwinRev[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_TwinRev, yNl_TwinRev);
	}
	return yNl_TwinRev[0];
}

void FenderTwinReverb_init()
{
    memset(xInDe_TwinRev, 0.0f, sizeof(xInDe_TwinRev));
    memset(yInDe_TwinRev, 0.0f, sizeof(yInDe_TwinRev));
    memset(xPf1_TwinRev,0.0f,sizeof(xPf1_TwinRev));
    memset(yPf1_TwinRev,0.0f,sizeof(yPf1_TwinRev));
    memset(xPf2_TwinRev,0.0f,sizeof(xPf2_TwinRev));
    memset(yPf2_TwinRev,0.0f,sizeof(yPf2_TwinRev));
    memset(xEQ_TwinRev,0.0f,sizeof(xEQ_TwinRev));
    memset(yEQ_TwinRev,0.0f,sizeof(yEQ_TwinRev));
    memset(numEQ_TwinRev,0.0f,sizeof(numEQ_TwinRev));
    memset(denEQ_TwinRev,0.0f,sizeof(denEQ_TwinRev));
    memset(xPost1_TwinRev,0.0f,sizeof(xPost1_TwinRev));
    memset(yPost1_TwinRev,0.0f,sizeof(yPost1_TwinRev));
    memset(xPost2_TwinRev,0.0f,sizeof(xPost2_TwinRev));
    memset(yPost2_TwinRev,0.0f,sizeof(yPost2_TwinRev));
	memset(xNl_TwinRev,0.0f,sizeof(xNl_TwinRev));
	memset(yNl_TwinRev,0.0f,sizeof(yNl_TwinRev));
}

void FenderTwinReverb_process(float *xin, float *xOut)
{
    float xnl,ynl;
    float xoutcab, xoutamp, youtcab, youtamp;
    float inputL, inputR, input;

    float R2 = 1-FenderTwinReverb_T1_knob[2]*d255;
	float R3 = 1-A25[FenderTwinReverb_T1_knob[3]];
	float R1 = A[FenderTwinReverb_T1_knob[1]];
	float gain = FenderTwinReverb_T1_knob[0]*0.00109803922f;
	float masterVol;
	float G;
	float level = FenderTwinReverb_T1_knob[4]*d255;
    G = 70.7946f * gain * gain;
    masterVol = 40.0f * level * level;

    ParamCal1_TwinRev(R1, R2, R3, numEQ_TwinRev, denEQ_TwinRev);

        inputL = xin[0];
        inputR = xin[0];
        xInDe_TwinRev[0] = inputL;
        preproc(xInDe_TwinRev, yInDe_TwinRev);
        xPf1_TwinRev[0] = yInDe_TwinRev[3]*G;
        secondOrderFilter(numPf1_TwinRev,denPf1_TwinRev, xPf1_TwinRev, yPf1_TwinRev);
        xPf2_TwinRev[0] = yPf1_TwinRev[0];
		secondOrderFilter(numPf2_TwinRev,denPf2_TwinRev, xPf2_TwinRev, yPf2_TwinRev);
		xnl = yPf2_TwinRev[0];
		ynl = nonlinear_TwinRev(xnl, xPrev_TwinRev);
		xPrev_TwinRev = xnl;
		xEQ_TwinRev[0] = ynl;
		thirdOrderFilterVA(numEQ_TwinRev,denEQ_TwinRev,xEQ_TwinRev,yEQ_TwinRev);
		xPost1_TwinRev[0] = yEQ_TwinRev[0] * masterVol;
		firstOrderFilter(numPost1_TwinRev, denPost1_TwinRev, xPost1_TwinRev, yPost1_TwinRev);
        xPost2_TwinRev[0] = yPost1_TwinRev[0];
		secondOrderFilter(numPost2_TwinRev, denPost2_TwinRev, xPost2_TwinRev, yPost2_TwinRev);
        xoutamp = yPost2_TwinRev[0];

        xOut[0] = xoutamp;
        xOut[1] = xoutamp;
}
#endif