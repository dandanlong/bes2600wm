 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "MxrDisPlus.h"

static float xInDe_mxrDis[6];
static float yInDe_mxrDis[6];
static float xPf00_mxrDis[3];
static float yPf00_mxrDis[3];
static float xPf01_mxrDis[3];
static float yPf01_mxrDis[3];
static float xPf02_mxrDis[3];
static float yPf02_mxrDis[3];
static float xPf03_mxrDis[3];
static float yPf03_mxrDis[3];
static float numPf1_mxrDis[2];
static float denPf1_mxrDis[2];
static float xPf1_mxrDis[2];
static float yPf1_mxrDis[2];
static float numPf2_mxrDis[3] = {0.4601463419f, 0.0f, -0.4601463419f};
static float denPf2_mxrDis[2] = {-0.8832769782f, -0.1163150254f};
static float xPf2_mxrDis[3];
static float yPf2_mxrDis[3];
static float xPrev_mxrDis[2];
static float xTo_mxrDis[4][3];
static float yTo_mxrDis[4][3];

static float xNl_mxrDis[3];
static float yNl_mxrDis[3];
static float xLim_mxrDis[3];
static float yLim_mxrDis[3];
static float numTo_mxrDis[4][3] = {0.8147212046f, -1.586459316f, 0.7723138715f,
		0.8059143818f, -1.587425063f, 0.789590811f,
		0.9171599853f, -1.610159724f, 0.7534400684f,
		1.582352537f, -0.09828314287f, 0.03171010717f};
static float denTo_mxrDis[4][2] = {-1.966376311f, 0.9670574919f,
		  -1.962837381f, 0.9730229573f,
          -1.709387162f, 0.7763329589f,
		  -0.08893362544f, 0.002427615608f};

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

float limiterMXR(float xcurr, float xprev)
{
	float xnl[8],ynl[8],diff;
	int i;
	int N = 8;
	diff = (xcurr - xprev) * 0.125f;
	for (i = 0;i<N;i++){
		xnl[i] = xprev + diff * (float)(i+1);
	}
	for (i=0;i<N;i++){
		if (xnl[i] > 2)
			ynl[i] = 2;
		else if (xnl[i] < -2)
			ynl[i] = -2;
		else
			ynl[i] = xnl[i];
		xLim_mxrDis[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xLim_mxrDis, yLim_mxrDis);
	}
	return yLim_mxrDis[0];
}

float nonlinear_mxrDis(float xcurr, float xprev)
{
	float absx,signx,xnl[8],ynl[8],diff;
	int i;
	int N = 8;
	diff = (xcurr - xprev) * 0.125f;
	for (i = 0;i<N;i++){
		xnl[i] = xprev + diff * (float)(i+1);
	}
	for (i=0;i<N;i++){
		absx = xnl[i] < 0? -xnl[i] : xnl[i];
		signx = xnl[i] < 0? -1 : 1;
		if (absx <= 0.5786f)
			ynl[i] = signx * 0.8163f*absx;
		else if ((absx > 0.5786f) &&  (absx <=1.515f))
			ynl[i] = signx * (-0.2944f + 2.0524f*absx - 1.4693f * absx*absx + 0.3671f *absx*absx*absx);
		else if ((absx > 1.515f) &&  (absx <=3.0f))
			ynl[i] = signx * (0.57965f + 0.1169f*absx - 0.0164f * absx*absx);
		else
			ynl[i] =  signx * 0.78275f;
		xNl_mxrDis[0] = ynl[i]*0.5f;
		butterSecondLP(gNl15k8X,denNl15k8X, xNl_mxrDis, yNl_mxrDis);
	}
	return yNl_mxrDis[0];
}

void gainParaCalc_mxrDis(float Rv, float *num, float *den){
	num[0] = Rv + 2.13813113f;
	num[1] = -Rv - 2.137188019f;
	
	den[0] = Rv + 0.01047155576f;
	den[1] = -Rv - 0.009528444243f;
}

void MxrDisPlus_init()
{
    memset(xInDe_mxrDis, 0.0f, sizeof(xInDe_mxrDis));
    memset(yInDe_mxrDis, 0.0f, sizeof(yInDe_mxrDis));
    memset(xPf00_mxrDis,0.0f,sizeof(xPf00_mxrDis));
    memset(yPf00_mxrDis,0.0f,sizeof(yPf00_mxrDis));
    memset(xPf01_mxrDis,0.0f,sizeof(xPf01_mxrDis));
    memset(yPf01_mxrDis,0.0f,sizeof(yPf01_mxrDis));
    memset(xPf02_mxrDis,0.0f,sizeof(xPf02_mxrDis));
    memset(yPf02_mxrDis,0.0f,sizeof(yPf02_mxrDis));
    memset(xPf03_mxrDis,0.0f,sizeof(xPf03_mxrDis));
    memset(yPf03_mxrDis,0.0f,sizeof(yPf03_mxrDis));
    memset(numPf1_mxrDis,0.0f,sizeof(numPf1_mxrDis));
	memset(denPf1_mxrDis,0.0f,sizeof(denPf1_mxrDis));
    memset(xPf1_mxrDis,0.0f,sizeof(xPf1_mxrDis));
    memset(yPf1_mxrDis,0.0f,sizeof(yPf1_mxrDis));
    memset(xPf2_mxrDis,0.0f,sizeof(xPf2_mxrDis));
    memset(yPf2_mxrDis,0.0f,sizeof(yPf2_mxrDis));
    memset(xPrev_mxrDis,0.0f,sizeof(xPrev_mxrDis));
    memset(xTo_mxrDis,0.0f,sizeof(xTo_mxrDis));
    memset(yTo_mxrDis,0.0f,sizeof(yTo_mxrDis));
    
    memset(xNl_mxrDis,0.0f,sizeof(xNl_mxrDis));
    memset(yNl_mxrDis,0.0f,sizeof(yNl_mxrDis));
    
    memset(xLim_mxrDis,0.0f,sizeof(xLim_mxrDis));
    memset(yLim_mxrDis,0.0f,sizeof(yLim_mxrDis));
}

void MxrDisPlus_process(float *xin, float *xOut)
{
    float knobLevel = powf((MxrDisPlus_T1_knob[1] * d255),2.0f);
	float knobDist = C25[MxrDisPlus_T1_knob[0]];
	float xnl1, ynl1, xnl2,ynl2, yout;
	float level;
	float inputL, inputR, input;

	gainParaCalc_mxrDis(knobDist, numPf1_mxrDis, denPf1_mxrDis);

	int i = 0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xInDe_mxrDis[0] = inputL*30.0f;
        preproc(xInDe_mxrDis, yInDe_mxrDis);
        xPf1_mxrDis[0] = yInDe_mxrDis[3];
		firstOrderFilterVA(numPf1_mxrDis,denPf1_mxrDis,xPf1_mxrDis,yPf1_mxrDis);
		xPf02_mxrDis[0] = yPf1_mxrDis[0];
		secondOrderFilter(numHpf0,denHpf2,xPf02_mxrDis,yPf02_mxrDis);
		xnl1 = yPf02_mxrDis[0];
		ynl1 = limiterMXR(xnl1,xPrev_mxrDis[0]);
		xPrev_mxrDis[0] = xnl1;
		xPf2_mxrDis[0] = ynl1;
		secondOrderFilter(numPf2_mxrDis,denPf2_mxrDis,xPf2_mxrDis,yPf2_mxrDis);
		xPf03_mxrDis[0] = yPf2_mxrDis[0];
		secondOrderFilter(numHpf0,denHpf2,xPf03_mxrDis,yPf03_mxrDis);
		xnl2 = yPf03_mxrDis[0];
		ynl2 = nonlinear_mxrDis(xnl2,xPrev_mxrDis[1]);
		xPrev_mxrDis[1] = xnl2;
		xTo_mxrDis[0][0] = ynl2;
		secondOrderFilter(&numTo_mxrDis[0][0],&denTo_mxrDis[0][0],&xTo_mxrDis[0][0],&yTo_mxrDis[0][0]);
		xTo_mxrDis[1][0] = yTo_mxrDis[0][0];
		secondOrderFilter(&numTo_mxrDis[1][0],&denTo_mxrDis[1][0],&xTo_mxrDis[1][0],&yTo_mxrDis[1][0]);
		xTo_mxrDis[2][0] = yTo_mxrDis[1][0];
		secondOrderFilter(&numTo_mxrDis[2][0],&denTo_mxrDis[2][0],&xTo_mxrDis[2][0],&yTo_mxrDis[2][0]);
		xTo_mxrDis[3][0] = yTo_mxrDis[2][0];
		secondOrderFilter(&numTo_mxrDis[3][0],&denTo_mxrDis[3][0],&xTo_mxrDis[3][0],&yTo_mxrDis[3][0]);
        
        yout = knobLevel * yTo_mxrDis[3][0];
        xOut[0+i] = yout;
        xOut[1+i] = yout;

}
#endif