/*
 * @Author: llq
 * @Date: 2025-12-25 14:54:02
 * @LastEditors: llq
 * @LastEditTime: 2026-06-05 14:09:17
 * @Description: Mesa Boogie Dual Rectifier
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\MesaBoogie.c
 */

 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "MesaBoogie.h"


static float xInDe_Mesa[6];
static float yInDe_Mesa[6];

static float num2_Mesa[2] = {0.9937406f, -0.9937406f};
static float den2_Mesa[1] = {-0.9874812f};
static float xPre2_Mesa[2];
static float yPre2_Mesa[2];

static float num3_Mesa[4] = {-0.8788016878f, -0.8813301261f, 0.8737448114f, 0.8762732496f};
static float den3_Mesa[3] = {-2.092589772f, 1.378310758f, -0.2756072329f};
static float xPre3_Mesa[4];
static float yPre3_Mesa[4];

static float num5_Mesa[4];
static float den5_Mesa[4];
static float xPre5_Mesa[4];
static float yPre5_Mesa[4];

static float numPrese_Mesa[3];
static float denPrese_Mesa[3];
static float xPrePrese_Mesa[3];
static float yPrePrese_Mesa[3];

static float xNl_Mesa[3];
static float yNl_Mesa[3];
static float xPrev_Mesa = 0.0f;

static float numPost1_Mesa[3] = {0.028448105078568f, -0.040908588522505f, 0.013211162534814f};
static float denPost1_Mesa[2] = {-1.205171405976000f, 0.329917821407695f};
static float xPost1_Mesa[3];
static float yPost1_Mesa[3];
static float numPost2_Mesa[3] = {1.513248360736935, -2.911418496634181f, 1.400034104288518f};
static float denPost2_Mesa[2] = {-1.971536398197736f, 0.972066148626661f};
static float xPost2_Mesa[3];
static float yPost2_Mesa[3];
static float numPost3_Mesa[3] = {2.869477210702402f, -1.361840733319431f, 0.353995720489968f};
static float denPost3_Mesa[2] = {-0.777474545075390f, 0.311506623267116f};
static float xPost3_Mesa[3];
static float yPost3_Mesa[3];
static float numPost4_Mesa[3] = {2.836244401577009f, -1.722083357351311f, 0.168465981830344f};
static float denPost4_Mesa[2] = {-1.172635860203306f, 0.298244297015023f};
static float xPost4_Mesa[3];
static float yPost4_Mesa[3];

static float a25v3[256] = {
0.000000f,0.000000f,0.000000f,0.000398f,0.000797f,0.001195f,0.001594f,0.001992f,
0.002390f,0.002789f,0.003187f,0.003586f,0.003984f,0.004382f,0.004781f,0.005179f,
0.005578f,0.005976f,0.006375f,0.006773f,0.007171f,0.007570f,0.007968f,0.008367f,
0.008765f,0.009163f,0.009562f,0.009960f,0.010359f,0.010757f,0.011156f,0.011554f,
0.011953f,0.012351f,0.012749f,0.013148f,0.013547f,0.013945f,0.014344f,0.014742f,
0.015141f,0.015540f,0.015939f,0.016337f,0.016736f,0.017135f,0.017534f,0.017934f,
0.018333f,0.018732f,0.019132f,0.019532f,0.019932f,0.020332f,0.020732f,0.021132f,
0.021533f,0.021934f,0.022336f,0.022737f,0.023139f,0.023542f,0.023945f,0.024348f,
0.024752f,0.025156f,0.025561f,0.025967f,0.026373f,0.026780f,0.027188f,0.027597f,
0.028007f,0.028417f,0.028829f,0.029242f,0.029656f,0.030072f,0.030489f,0.030907f,
0.031328f,0.031749f,0.032173f,0.032599f,0.033027f,0.033457f,0.033889f,0.034324f,
0.034762f,0.035202f,0.035646f,0.036092f,0.036542f,0.036996f,0.037453f,0.037915f,
0.038380f,0.038850f,0.039325f,0.039804f,0.040289f,0.040779f,0.041275f,0.041776f,
0.042285f,0.042799f,0.043321f,0.043850f,0.044387f,0.044932f,0.045485f,0.046048f,
0.046619f,0.047200f,0.047791f,0.048393f,0.049006f,0.049631f,0.050268f,0.050917f,
0.051580f,0.052256f,0.052947f,0.053652f,0.054374f,0.055111f,0.055866f,0.056638f,
0.057429f,0.058239f,0.059068f,0.059919f,0.060790f,0.061685f,0.062602f,0.063544f,
0.064510f,0.065503f,0.066523f,0.067571f,0.068647f,0.069754f,0.070892f,0.072063f,
0.073267f,0.074506f,0.075781f,0.077093f,0.078444f,0.079835f,0.081267f,0.082742f,
0.084261f,0.085826f,0.087438f,0.089099f,0.090810f,0.092574f,0.094392f,0.096265f,
0.098196f,0.100185f,0.102237f,0.104351f,0.106530f,0.108777f,0.111093f,0.113481f,
0.115942f,0.118479f,0.121094f,0.123790f,0.126569f,0.129433f,0.132385f,0.135429f,
0.138565f,0.141797f,0.145129f,0.148562f,0.152100f,0.155745f,0.159501f,0.163371f,
0.167358f,0.171465f,0.175695f,0.180053f,0.184541f,0.189164f,0.193924f,0.198825f,
0.203872f,0.209068f,0.214417f,0.219923f,0.225590f,0.231423f,0.237426f,0.243602f,
0.249958f,0.256497f,0.263224f,0.270143f,0.277260f,0.284580f,0.292107f,0.299848f,
0.307806f,0.315987f,0.324398f,0.333043f,0.341928f,0.351059f,0.360442f,0.370083f,
0.379988f,0.390163f,0.400615f,0.411350f,0.422375f,0.433697f,0.445322f,0.457257f,
0.469510f,0.482087f,0.494997f,0.508246f,0.521843f,0.535794f,0.550109f,0.564795f,
0.579860f,0.595313f,0.611161f,0.627415f,0.644082f,0.661172f,0.678693f,0.696656f,
0.715068f,0.733941f,0.753283f,0.773105f,0.793416f,0.814228f,0.835549f,0.857392f,
0.879766f,0.902682f,0.926153f,0.950188f,0.974800f,1.000000f,1.000000f,1.000000f};

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

float nonlinear_Mesa(float xcurr, float xprev)
{
	float absx, signx, x, y, x1, x2;
	float xnl[8], ynl[8];
	int i = 0;
	int N = 8;
	float p1, q1,q2;
    float diff;
    diff = (xcurr - xprev) * 0.125f;


	p1 =   5.353e+04f;
	q1 =   1.343e+05f;
	q2 =        8731.0f;
	for (i = 0;i<N;i++){
        xnl[i] = xprev + diff * (float)(i+1);
    }
	for (i = 0;i < N;i++)
	{
		x = xnl[i];
		absx = x < 0.0f ? -x:x;
		signx = x < 0? -1 : 1;
		x1 = absx;
		x2 = absx*absx;
		ynl[i] = signx*((p1*x1)/(x2 + q1*x1 + q2));
        xNl_Mesa[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_Mesa, yNl_Mesa);
	}
	return yNl_Mesa[0];
}


void ParamCal1_Mesa(float Rb, float Rm, float Rt,float *num,float *den)
{
    float RBRM = Rb*Rm;
    float RBRT = Rb*Rt;

    num[0] = 0.01749768f*Rb + 0.0006746259f*Rm + 0.0009527181f*Rt + 0.713012f*RBRM + RBRT;
	num[1] = - 0.01539241f*Rb - 0.0005831441f*Rm - 0.0009412213f*Rt - 2.041217f*RBRM - 3.0f*RBRT;
	num[2] = 1.943398f*RBRM - 0.0006746259f*Rm - 0.0009527181f*Rt - 0.01749768f*Rb + 3.0f*RBRT;
	num[3] = 0.01539241f*Rb + 0.0005831441f*Rm + 0.0009412213f*Rt - 0.6151931f*RBRM - RBRT;

	den[0] = 1.102049f*Rb + 0.0006746259f*Rm + 0.713012f*RBRM + 0.001033271f;
	den[1] = - 3.099943f*Rb - 0.0005831441f*Rm - 2.041217f*RBRM - 0.0008596961f;
	den[2] = 2.897951f*Rb - 0.0006746259f*Rm + 1.943398f*RBRM - 0.001031327f;
	den[3] = 0.0005831441f*Rm - 0.9000567f*Rb - 0.6151931f*RBRM + 0.0008616402f;
}

void ParamCal2_Mesa(float R, float *num, float *den)
{
	num[0] = 1.035392f - 0.9994216f*R;
	num[1] = 1.998843f*R - 2.063322f;
	num[2] = 1.02793f - 0.9994216f*R;

	den[0] = R - 1.014499f;
	den[1] = 2.020332f - 1.998843f*R;
	den[2] = 0.9988433f*R - 1.005842f;
}

void MesaBoogie_init()
{
	memset(xInDe_Mesa, 0.0f, sizeof(xInDe_Mesa));
    memset(yInDe_Mesa, 0.0f, sizeof(yInDe_Mesa));
    memset(xPre2_Mesa,0.0,sizeof(xPre2_Mesa));
    memset(yPre2_Mesa,0.0,sizeof(yPre2_Mesa));
    memset(xPre3_Mesa,0.0f,sizeof(xPre3_Mesa));
    memset(yPre3_Mesa,0.0f,sizeof(yPre3_Mesa));
    memset(num5_Mesa,0.0f,sizeof(num5_Mesa));
    memset(den5_Mesa,0.0f,sizeof(den5_Mesa));
    memset(xPre5_Mesa,0.0f,sizeof(xPre5_Mesa));
    memset(yPre5_Mesa,0.0f,sizeof(yPre5_Mesa));
    memset(numPrese_Mesa,0.0f,sizeof(numPrese_Mesa));
    memset(denPrese_Mesa,0.0f,sizeof(denPrese_Mesa));
    memset(xPrePrese_Mesa,0.0f,sizeof(xPrePrese_Mesa));
    memset(yPrePrese_Mesa,0.0f,sizeof(yPrePrese_Mesa));

    
	memset(xPost1_Mesa,0.0f,sizeof(xPost1_Mesa));
	memset(yPost1_Mesa,0.0f,sizeof(yPost1_Mesa));
	memset(xPost2_Mesa,0.0f,sizeof(xPost2_Mesa));
	memset(yPost2_Mesa,0.0f,sizeof(yPost2_Mesa));
	memset(xPost3_Mesa,0.0f,sizeof(xPost3_Mesa));
	memset(yPost3_Mesa,0.0f,sizeof(yPost3_Mesa));
	memset(xPost4_Mesa,0.0f,sizeof(xPost4_Mesa));
	memset(yPost4_Mesa,0.0f,sizeof(yPost4_Mesa));
    memset(yPost4_Mesa,0.0f,sizeof(yPost4_Mesa));
    memset(xNl_Mesa,0.0f,sizeof(xNl_Mesa));
    memset(yNl_Mesa,0.0f,sizeof(yNl_Mesa));
}

void MesaBoogie_process(float *xin, float *xOut)
{
    float inputL, inputR, input;
    float xoutamp_Mesa;
	float youtamp_Mesa;
    float xnl_Mesa,ynl_Mesa;

    float knobGain_Mesa = MesaBoogie_T1_knob[0]*d255;
    float knobBass_Mesa = a25v3[MesaBoogie_T1_knob[1]];
    float knobMid_Mesa = A25[MesaBoogie_T1_knob[2]];
    float knobTreble_Mesa = A25[MesaBoogie_T1_knob[3]];
    float knobPresence_Mesa = 0.8f + 0.2f*(MesaBoogie_T1_knob[4]*d255);
    float knobVol_Mesa = MesaBoogie_T1_knob[5]*d255;

    ParamCal1_Mesa(knobBass_Mesa,knobMid_Mesa,knobTreble_Mesa,num5_Mesa,den5_Mesa);
    ParamCal2_Mesa(knobPresence_Mesa, numPrese_Mesa, denPrese_Mesa);

	int i = 0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xInDe_Mesa[0] = inputL;
        preproc(xInDe_Mesa, yInDe_Mesa);
        xPre2_Mesa[0] = yInDe_Mesa[3] * 2.0f * knobGain_Mesa * 31.6228f;
        firstOrderFilter(num2_Mesa,den2_Mesa,xPre2_Mesa,yPre2_Mesa);
        xPre3_Mesa[0] = yPre2_Mesa[0];
        thirdOrderFilter(num3_Mesa,den3_Mesa,xPre3_Mesa,yPre3_Mesa);

        xnl_Mesa = yPre3_Mesa[0];
        ynl_Mesa = nonlinear_Mesa(xnl_Mesa, xPrev_Mesa);
        xPrev_Mesa = xnl_Mesa;

        xPre5_Mesa[0] =ynl_Mesa;
        thirdOrderFilterVA(num5_Mesa, den5_Mesa, xPre5_Mesa, yPre5_Mesa);

        xPost1_Mesa[0] = yPre5_Mesa[0];
        secondOrderFilter(numPost1_Mesa, denPost1_Mesa, xPost1_Mesa, yPost1_Mesa);
        xPost2_Mesa[0] = yPost1_Mesa[0];
        secondOrderFilter(numPost2_Mesa, denPost2_Mesa, xPost2_Mesa, yPost2_Mesa);
        xPost3_Mesa[0] = yPost2_Mesa[0];
        secondOrderFilter(numPost3_Mesa, denPost3_Mesa, xPost3_Mesa, yPost3_Mesa);
        xPost4_Mesa[0] = yPost3_Mesa[0];
        secondOrderFilter(numPost4_Mesa, denPost4_Mesa, xPost4_Mesa, yPost4_Mesa);
        xPrePrese_Mesa[0] = yPost4_Mesa[0];
        secondOrderFilterVA(numPrese_Mesa, denPrese_Mesa, xPrePrese_Mesa, yPrePrese_Mesa);

        xoutamp_Mesa = yPrePrese_Mesa[0]*knobVol_Mesa*20.0f;
    
        xOut[0+i] = xoutamp_Mesa;
        xOut[1+i] = xoutamp_Mesa;
}

#endif