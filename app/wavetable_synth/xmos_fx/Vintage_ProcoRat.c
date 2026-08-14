 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Vintage_ProcoRat.h"

static float xInDe_rat[6];
static float yInDe_rat[6];
static float numPre1_rat[4];
static float denPre1_rat[4];
static float numPre2_rat[2];
static float denPre2_rat[2];
static float xPre1_rat[4];
static float yPre1_rat[4];
static float xPre2_rat[2];
static float yPre2_rat[2];
static float numTo_rat[3];
static float denTo_rat[3];
static float xTo_rat[3];
static float yTo_rat[3];
static float xLim_rat[3];
static float yLim_rat[3];
static float xNl_rat[3];
static float yNl_rat[3];
static float max_v = 6.16666f;
static float data_pre = 0.0f;
static float xPrev_rat = 0.0f;

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

float nonlinear_rat(float xcurr, float xprev)
{
    float xnl[8],ynl[8],diff;
    float absx,signx;
    int i;
    int N = 8;
    float p1 = 0.944f;
    float p2 = -0.6421f;
    float p3 = 0.3065f;
    float q1 = -0.2256f;
    float q2 = -0.2028f;
    float q3 = 0.2493f;
    diff = (xcurr - xprev) * 0.125f;
    for (i = 0;i<N;i++){
        xnl[i] = xprev + diff * (float)(i+1);
    }
    for (i=0;i<N;i++){
        absx = xnl[i] < 0? -xnl[i] : xnl[i];
		signx = xnl[i] < 0? -1 : 1;
        ynl[i] = signx * ((p1 * absx * absx * absx + p2 * absx * absx + p3 * absx) /
                         (absx * absx * absx + q1 * absx * absx + q2 * absx + q3));
        xNl_rat[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_rat, yNl_rat);
    }
    return yNl_rat[0];
}

void gainParaCalcPre1_rat(float Rv, float *num, float *den){
	num[0] = 1.0f+2111.7605619f*Rv;
	num[1] = -0.809073f-2065.7f*Rv;
    num[2] = -0.998557f-2108.2857f*Rv;
    num[3] = 0.810516f+2062.225461f*Rv;
	
	den[0] = 1.0f+0.968f*Rv;
	den[1] = -0.8091f-2.69671f*Rv;
    den[2] = -0.9985f+2.5148f*Rv;
    den[3] = 0.8105f-0.7781f*Rv;
}

void gainParaCalcPre2_rat(float Rv, float *num, float *den){
	num[0] = 1.0f;
	num[1] = 0.0f;
	
	den[0] = 1.0f+57.6f*Rv;
	den[1] = -57.6f*Rv;
}

void toneParaCalc_rat(float Rt, float *num, float *den){
    num[0] = 0.030013f;
    num[1] = 0.0f;
    num[2] = -0.030013f;
    
    den[0] = 1.0f-0.954711f*Rt;
    den[1] = -1.9320178f+1.901620f*Rt;
    den[2] = 0.932074f-0.94691f*Rt;
}

void Vintage_ProcoRat_init()
{
    memset(xInDe_rat, 0.0f, sizeof(xInDe_rat));
    memset(yInDe_rat, 0.0f, sizeof(yInDe_rat));
    memset(numPre1_rat,0.0f,sizeof(numPre1_rat));
    memset(denPre1_rat,0.0f,sizeof(denPre1_rat));
    memset(numPre2_rat,0.0f,sizeof(numPre2_rat));
    memset(denPre2_rat,0.0f,sizeof(denPre2_rat));
    memset(xPre1_rat,0.0f,sizeof(xPre1_rat));
    memset(yPre1_rat,0.0f,sizeof(yPre1_rat));
    memset(xPre2_rat,0.0f,sizeof(xPre2_rat));
    memset(yPre2_rat,0.0f,sizeof(yPre2_rat));
    memset(numTo_rat,0.0f,sizeof(numTo_rat));
    memset(denTo_rat,0.0f,sizeof(denTo_rat));
    memset(xTo_rat,0.0f,sizeof(xTo_rat));
    memset(yTo_rat,0.0f,sizeof(yTo_rat));
    memset(xLim_rat,0.0f,sizeof(xLim_rat));
    memset(yLim_rat,0.0f,sizeof(yLim_rat));
    memset(xNl_rat,0.0f,sizeof(xNl_rat));
    memset(yNl_rat,0.0f,sizeof(yNl_rat));
}

void Vintage_ProcoRat_process(float *xin, float *xOut)
{
    float inputL, inputR, input;
    float xnl,ynl;
    float yout;
    float knobDist = powf(Vintage_ProcoRat_T1_knob[0]*d255, 2.0f);
    float knobTone = Vintage_ProcoRat_T1_knob[1]*d255;
    float knobLevel = Vintage_ProcoRat_T1_knob[2]*d255;

    gainParaCalcPre1_rat(knobDist, numPre1_rat, denPre1_rat);
    gainParaCalcPre2_rat(knobDist, numPre2_rat, denPre2_rat);
    toneParaCalc_rat(knobTone, numTo_rat, denTo_rat);

    int i = 0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xInDe_rat[0] = inputL;
        preproc(xInDe_rat, yInDe_rat);
        xPre1_rat[0] = yInDe_rat[3]*25.0f;
        thirdOrderFilterVA(numPre1_rat, denPre1_rat, xPre1_rat, yPre1_rat);
        xPre2_rat[0] = yPre1_rat[0];
        firstOrderFilterVA(numPre2_rat, denPre2_rat, xPre2_rat, yPre2_rat);

        if ((yPre2_rat[0] - data_pre) > max_v)
        {
            yPre2_rat[0] = data_pre + max_v;
        }
        else if((yPre2_rat[0] - data_pre) < -max_v)
        {
            yPre2_rat[0] = data_pre - max_v;
        }
        if (yPre2_rat[0] > 4.0f)
        {
            yPre2_rat[0] = 4.0f;
        }
        else if (yPre2_rat[0] < -4.0f)
        {
            yPre2_rat[0] = -4.0f;
        }
        data_pre = yPre2_rat[0];

        xnl = yPre2_rat[0];
		ynl = nonlinear_rat(xnl,xPrev_rat);
		xPrev_rat = xnl;

        xTo_rat[0] = ynl;
        secondOrderFilterVA(numTo_rat, denTo_rat, xTo_rat, yTo_rat);

        yout = yTo_rat[0]*knobLevel;
        xOut[0+i] = yout;
        xOut[1+i] = yout;
}

#endif




