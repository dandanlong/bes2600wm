 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Ts808.h"
#include "xmos_lut.h"


static float xInDe_ts808[6];
static float yInDe_ts808[6];
static float numInBuffer_ts808[3] = {0.9846089351f, -1.771020254f, 0.7867065961f};
static float denInBuffer_ts808[2] = {-1.797028793f, 0.7977913661f};
static float xInBuffer_ts808[3];
static float yInBuffer_ts808[3];
static float numHP_ts808[2] = {0.9989594f, -0.9989594f};
static float denHP_ts808[1] = {-0.9979188f};
static float xHP_ts808[2];
static float yHP_ts808[2];
static float numGain_ts808[3];
static float denGain_ts808[3];
static float xGain_ts808[3];
static float yGain_ts808[3];
static float numNl_ts808[3] = {0.01276592482f, 0.02563337465f, 0.01276592482f};
static float denNl_ts808[2] = {-1.655252287f, 0.7065030048f};
static float xNl_ts808[3];
static float yNl_ts808[3];
static float numTone_ts808[3];
static float denTone_ts808[3];
static float xTone_ts808[3];
static float yTone_ts808[3];
static float numLevel_ts808[2];
static float denLevel_ts808[1];
static float xLevel_ts808[2];
static float yLevel_ts808[2];
static float numOutBuffer_ts808[3] = {0.9706266539f, -1.754662055f, 0.7843138343f};
static float denOutBuffer_ts808[2] = {-1.807409322f, 0.8077560498f};
static float xOutBuffer_ts808[3];
static float yOutBuffer_ts808[3];
static float xPrev_ts808 = 0.0f;

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

float expCurve_ts808(float upLimit, float downLimit, float upCurvature,
                      float downCurvature, float x) {
  float y;

  /* Hot path uses identical up/down limits+curvature (kn/kn, gn/gn).
   * Keep general form for A/B against LUT when g_xmos_ts808_curve_lut==0. */
  if (x > 0) {
    y = upLimit * (1 - expf(-upCurvature * x));
  } else {
    y = -downLimit * (1 - expf(downCurvature * x));
  }

  return y;
}

float nonlinear_ts808(float xcurr, float xprev, float Rg)
{
    float diff;
    float xnl;
    float y;
    int i;
    const int N = 8;
    /* Soft-clip curve params (gain-dependent). */
    float kn = 0.39f - 0.12f * Rg;
    float gn = 3.0f;
    const int use_lut = (g_xmos_ts808_curve_lut != 0);

    /* 8x linear upsample → nonlinearity → AA LPF (same pattern as Rat/amps). */
    diff = (xcurr - xprev) * 0.125f;
    for (i = 0; i < N; i++)
    {
        xnl = xprev + diff * (float)(i + 1);
        if (use_lut)
          {
            /* gn fixed at 3.0 — matches xmos_ts808_curve_lut generation. */
            (void)gn;
            y = xmos_ts808_curve_lut_eval(kn, xnl);
          }
        else
          {
            y = expCurve_ts808(kn, kn, gn, gn, xnl);
          }
        xNl_ts808[0] = y;
        secondOrderFilter(numNl_ts808, denNl_ts808, xNl_ts808, yNl_ts808);
    }

    return yNl_ts808[0];
}

void gainParaCalc_ts808(float Rg, float *num, float *den){
    num[0] = 42.50014f*Rg + 4.743511f;
    num[1] = - 1.909936f*Rg - 0.1580226f;
    num[2] = - 40.5902f*Rg - 4.511906f;
    den[0] = Rg + 0.5104967f;
    den[1] = - 1.909936f*Rg - 0.1580226f;
    den[2] = 0.9099359f*Rg - 0.2788924f;
}

void toneParaCalc_ts808(float Rt, float *num, float *den){
    float Rt2 = Rt*Rt;
    num[0] = 0.0450045f*Rt2 - 0.04725473f*Rt - 0.0006015943f;
    num[1] = -0.0002130895f;
    num[2] = - 0.0450045f*Rt2 + 0.04725473f*Rt + 0.0003885048f;
    den[0] = Rt2 - 0.9977498f*Rt - 0.01561765f;
    den[1] = - 1.90099f*Rt2 + 1.90099f*Rt + 0.02067649f;
    den[2] = 0.9009901f*Rt2 - 0.9032403f*Rt - 0.00552764f;
}

void levelParaCalc_ts808(float Rl, float *num, float *den){
    num[0] = 0.9899969f*Rl;
    num[1] = -0.9899969f*Rl;
    den[0] = -0.9997938f;
}

void Ts808_init()
{
    memset(xInDe_ts808, 0.0f, sizeof(xInDe_ts808));
    memset(yInDe_ts808, 0.0f, sizeof(yInDe_ts808));
    memset(xInBuffer_ts808,0.0f,sizeof(xInBuffer_ts808));
    memset(yInBuffer_ts808,0.0f,sizeof(yInBuffer_ts808));
    memset(xHP_ts808,0.0f,sizeof(xHP_ts808));
    memset(yHP_ts808,0.0f,sizeof(yHP_ts808));
    memset(numGain_ts808,0.0f,sizeof(numGain_ts808));
    memset(denGain_ts808,0.0f,sizeof(denGain_ts808));
    memset(xGain_ts808,0.0f,sizeof(xGain_ts808));
    memset(yGain_ts808,0.0f,sizeof(yGain_ts808));
    memset(xNl_ts808,0.0f,sizeof(xNl_ts808));
    memset(yNl_ts808,0.0f,sizeof(yNl_ts808));
    memset(numTone_ts808,0.0f,sizeof(numTone_ts808));
    memset(denTone_ts808,0.0f,sizeof(denTone_ts808));
    memset(xTone_ts808,0.0f,sizeof(xTone_ts808));
    memset(yTone_ts808,0.0f,sizeof(yTone_ts808));
    memset(numLevel_ts808,0.0f,sizeof(numLevel_ts808));
    memset(denLevel_ts808,0.0f,sizeof(denLevel_ts808));
    memset(xLevel_ts808,0.0f,sizeof(xLevel_ts808));
    memset(yLevel_ts808,0.0f,sizeof(yLevel_ts808));
    memset(xOutBuffer_ts808,0.0f,sizeof(xOutBuffer_ts808));
    memset(yOutBuffer_ts808,0.0f,sizeof(yOutBuffer_ts808));
    xPrev_ts808 = 0.0f;
}

void Ts808_process(float *xin, float *xOut)
{
    float inputL, inputR, input;
    float xCurr, output;
    float youtL, youtR;
    static uint8_t knob_backup[3] = {255, 255, 255};
    static float knobGain = 0.0f;
    static float knobTone = 0.0f;
    static float knobLevel = 0.0f;

    if (knob_backup[0] != Ts808_T1_knob[0] ||
        knob_backup[1] != Ts808_T1_knob[1] ||
        knob_backup[2] != Ts808_T1_knob[2])
    {
        knob_backup[0] = Ts808_T1_knob[0];
        knob_backup[1] = Ts808_T1_knob[1];
        knob_backup[2] = Ts808_T1_knob[2];
        knobGain = powf(Ts808_T1_knob[0]*d255, 2.0f);
        knobTone = Ts808_T1_knob[1]*d255;
        knobLevel = Ts808_T1_knob[2]*d255;
        gainParaCalc_ts808(knobGain, numGain_ts808, denGain_ts808);
        toneParaCalc_ts808(knobTone, numTone_ts808, denTone_ts808);
        levelParaCalc_ts808(knobLevel, numLevel_ts808, denLevel_ts808);
    }

    int i = 0;
        inputL = xin[0+i];
        inputR = xin[0+i];

        xInDe_ts808[0] = inputL;
        preproc(xInDe_ts808, yInDe_ts808);
        xInBuffer_ts808[0] = yInDe_ts808[3]*15.0f;
        secondOrderFilter(numInBuffer_ts808, denInBuffer_ts808, xInBuffer_ts808, yInBuffer_ts808);

        xHP_ts808[0] = yInBuffer_ts808[0];
        firstOrderFilter(numHP_ts808, denHP_ts808, xHP_ts808, yHP_ts808);
        xGain_ts808[0] = yHP_ts808[0];
        secondOrderFilterVA(numGain_ts808, denGain_ts808, xGain_ts808, yGain_ts808);

        xCurr = yGain_ts808[0];
        output = nonlinear_ts808(xCurr, xPrev_ts808, knobGain);
        xPrev_ts808 = xCurr;

        output = 0.6f * output + 0.8f * yHP_ts808[0];
        xTone_ts808[0] = output;
        secondOrderFilterVA(numTone_ts808, denTone_ts808, xTone_ts808, yTone_ts808);

        xLevel_ts808[0] = yTone_ts808[0];
        firstOrderFilter(numLevel_ts808, denLevel_ts808, xLevel_ts808, yLevel_ts808);

        xOutBuffer_ts808[0] = yLevel_ts808[0];
        secondOrderFilter(numOutBuffer_ts808, denOutBuffer_ts808, xOutBuffer_ts808, yOutBuffer_ts808);

        youtL = yOutBuffer_ts808[0] * (1.0f + 0.3f * knobGain);

        xOut[0+i] = youtL;
        xOut[1+i] = youtL;
}

#endif