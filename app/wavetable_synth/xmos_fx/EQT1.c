#if 1
#include "global.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "xcore_math.h"
// #include "algo_buffersT0.h"
#include "EQ.h"

static float xHPF1_L[2];
static float yHPF1_L[2];
static float xHPF1_R[2];
static float yHPF1_R[2];
static float xHPF2_L[3];
static float yHPF2_L[3];
static float xHPF2_R[3];
static float yHPF2_R[3];
static float xHPF3_L[6];
static float yHPF3_L[6];
static float xHPF3_R[6];
static float yHPF3_R[6];

static float xLowShelf_L[3];
static float yLowShelf_L[3];
static float xLowShelf_R[3];
static float yLowShelf_R[3];

static float xPeakL[3];
static float yPeakL[3];
static float xPeakR[3];
static float yPeakR[3];

static float xLPF1_L[2];
static float yLPF1_L[2];
static float xLPF1_R[2];
static float yLPF1_R[2];
static float xLPF2_L[3];
static float yLPF2_L[3];
static float xLPF2_R[3];
static float yLPF2_R[3];
static float xLPF3_L[6];
static float yLPF3_L[6];
static float xLPF3_R[6];
static float yLPF3_R[6];

static float xHighShelf_L[3];
static float yHighShelf_L[3];
static float xHighShelf_R[3];
static float yHighShelf_R[3];

static float numHPF1[2];
static float denHPF1[1];
static float numLPF1[2];
static float denLPF1[1];
static float numHPF2[3];
static float denHPF2[2];
static float numLPF2[3];
static float denLPF2[2];
static float numHPF3[6];
static float denHPF3[4];
static float numLPF3[6];
static float denLPF3[4];
static float numLowShelf[3];
static float denLowShelf[3];

static float numHighShelf[3];
static float denHighShelf[3];

static float numPeak[3];
static float denPeak[2];


static void firstOrderHPFParaCalc(float fc, float *num, float *den)
{
    float k;
    k = tanf(PI*fc*2.083e-5f);
    num[0] = 1.0f/(k + 1.0f);
    num[1] = -num[0];
    den[0] = (k - 1.0f)*num[0];
}

static void firstOrderLPFParaCalc(float fc, float *num, float *den)
{
    float k;
    k = tanf(PI*fc*2.083e-5f);
    num[0] = k/(k + 1.0f);
    num[1] = num[0];
    den[0] = (k - 1.0f)/(k + 1.0f);
}

static void secondOrderHPFParaCalc(float fc, float *num, float *den)
{
    float k;
	float k2; 
	float temp;
    k = tanf(PI*fc*2.083e-5f);
    k2 = k * k;
    temp = k2 * 0.707f + k + 0.707f;

	num[0] = 0.707f/temp;
	num[1] = -1.414f/temp;
	num[2] = num[0];
	den[0] = 1.414 * (k2 -1)/temp;
	den[1] = (k2 * 0.707 - k + 0.707)/temp;
}

static void secondOrderLPFParaCalc(float fc, float *num, float *den)
{
	float k;
	float k2;
	float temp;
    k  = tanf(PI*fc*2.083e-5f);
    k2  = k * k;
    temp = k2 * 0.707f + k + 0.707f;

	num[0] = 0.707f * k2/temp;
	num[1] = 1.414f * k2/temp;
	num[2] = num[0];
	den[0] = 1.414f * (k2 -1)/temp;
	den[1] = (k2 * 0.707f - k + 0.707f)/temp;
}

static void fourthOrderHPFParaCalc(float fc, float *num, float *den)
{
    float k, Q1, Q2, norm1, norm2;
    float k2;
    k = tanf(PI*fc*2.083e-5f);
    k2 = k * k;
    Q1 = 0.5412f; 
    Q2 = 1.3065f; 
    norm1 = 1.0f + k/Q1 + k2;
    norm2 = 1.0f + k/Q2 + k2;
    num[0] = 1.0f/norm1;
    num[1] = -2.0f*num[0];
    num[2] = num[0];
    den[0] = 2.0f*(k2 - 1.0f)/norm1;
    den[1] = (1.0f - k/Q1 + k2)/norm1;

    num[3] = 1.0f/norm2;
    num[4] = -2.0f*num[3];
    num[5] = num[3];
    den[2] = 2.0f*(k2 - 1.0f)/norm2;
    den[3] = (1.0f - k/Q2 + k2)/norm2;

}

static void fourthOrderLPFParaCalc(float fc, float *num, float *den)
{
    float k, Q1, Q2, norm1, norm2;
    float k2;
    k = tanf(PI*fc*2.083e-5f);
    k2 = k * k;
    Q1 = 0.5412f; 
    Q2 = 1.3065f; 
    norm1 = 1.0f + k/Q1 + k2;
    norm2 = 1.0f + k/Q2 + k2;
    num[0] = k2/norm1;
    num[1] = 2.0f*num[0];
    num[2] = num[0];
    den[0] = 2.0f*(k2 - 1.0f)/norm1;
    den[1] = (1.0f - k/Q1 + k2)/norm1;

    num[3] = k2/norm2;
    num[4] = 2.0f*num[3];
    num[5] = num[3];
    den[2] = 2.0f*(k2 - 1.0f)/norm2;
    den[3] = (1.0f - k/Q2 + k2)/norm2;
}

static void peakParaCalc(float fc, float g, float q, float *num, float *den)
{
    float k = tanf(PI * fc*2.083e-5f);
    float v0 = powf(10.0f, g*0.05f);
    float k2 = k * k;
    float kDivQ = k / q;
    float temp1 = 1.0f + kDivQ + k2;
    float kDivQDivV0 = kDivQ / v0;
    float kDivQV0 = kDivQ * v0;
    float temp2 = 1.0f + kDivQDivV0 + k2;
    // boost
    if(g >= 0.f)
    {
        num[0] = (1.0f + kDivQV0 + k2)/temp1;
        num[1] = 2.0f * (k2 -1.0f)/temp1;
        num[2] = (1.0f - kDivQV0 + k2)/temp1;
        den[0] = 2.0f*(k2 - 1.0f)/temp1;
        den[1] = (1.0f - kDivQ + k2)/temp1;
    }
    // cut
    else
    {
        num[0] = (1.0f + kDivQ + k2)/temp2;
        num[1] = 2.0f*(k2 - 1.0f)/temp2;
        num[2] = (1.0f - kDivQ + k2)/temp2;
        den[0] = 2.0f*(k2 - 1.0f)/temp2;
        den[1] = (1.0f - kDivQDivV0 + k2)/temp2;
    }

}

static void lowShelfParaCalc(float fc, float g, float *num, float *den)
{
    float omega0 = 2.0f * PI * fc * 2.083e-5f;
    float A = powf(10.0f, g *0.025f);
    float S = 1.0f;
    float sine = sinf(omega0);
    float alpha = sine *0.5f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    float cosw0 = sqrtf(1.0f - sine * sine);
    float temp = (A + 1.0f) - (A - 1.0f) * cosw0;
    float temp1 = (A + 1.0f) + (A - 1.0f) * cosw0;
    float temp3 = sqrtf(A) * alpha;
    float temp4 = (A + 1.0f) * cosw0;
    float temp5 = 2.0f * temp3;
    num[0] = A * (temp + temp5);
    num[1] = 2.0f * A * ((A - 1.0f) - temp4);
    num[2] = A * (temp - temp5);
    den[0] = temp1 + temp5;
    den[1] = -2.0f * ((A - 1.0f) + temp4);
    den[2] = temp1 - temp5;
}

static void highShelfParaCalc(float fc, float g, float *num, float *den)
{
    float omega0 = 2.0f * PI * fc * 2.083e-5f;
    float A = powf(10.0f, g *0.025f);
    float S = 1.0f;
    float sine = sinf(omega0);
    float alpha = sine / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    float cosw0 = sqrtf(1.0f - sine * sine);
    float temp = (A + 1.0f) - (A - 1.0f) * cosw0;
    float temp1 = (A + 1.0f) + (A - 1.0f) * cosw0;
    float temp3 = sqrtf(A) * alpha;
    float temp4 = (A + 1.0f) * cosw0;
    float temp5 = 2.0f * temp3;

    num[0] = A * (temp1 + temp5);
    num[1] = -2.0f * A * ((A - 1.0f) + temp4);
    num[2] = A * (temp1 - temp5);
    den[0] = temp + temp5;
    den[1] = 2.0f * ((A - 1.0f) - temp4);
    den[2] = temp - temp5;
}

void EQT1_init()
{
   memset(xHPF1_L,0.0f,sizeof(xHPF1_L));
    memset(xHPF1_R,0.0f,sizeof(xHPF1_R));
    memset(yHPF1_L,0.0f,sizeof(yHPF1_L));
    memset(yHPF1_R,0.0f,sizeof(yHPF1_R));
    memset(xHPF2_L,0.0f,sizeof(xHPF2_L));
    memset(yHPF2_L,0.0f,sizeof(yHPF2_L));
    memset(xHPF2_R,0.0f,sizeof(xHPF2_R));
    memset(yHPF2_R,0.0f,sizeof(yHPF2_R));
    memset(xHPF3_L,0.0f,sizeof(xHPF3_L));
    memset(yHPF3_L,0.0f,sizeof(yHPF3_L));
    memset(xHPF3_R,0.0f,sizeof(xHPF3_R));
    memset(yHPF3_R,0.0f,sizeof(yHPF3_R));
    memset(xLowShelf_L,0.0f,sizeof(xLowShelf_L));
    memset(yLowShelf_L,0.0f,sizeof(yLowShelf_L));
    memset(xLowShelf_R,0.0f,sizeof(xLowShelf_R));
    memset(yLowShelf_R,0.0f,sizeof(yLowShelf_R));
    memset(xPeakL,0.0f,sizeof(xPeakL));
    memset(yPeakL,0.0f,sizeof(yPeakL));
    memset(xPeakR,0.0f,sizeof(xPeakR));
    memset(yPeakR,0.0f,sizeof(yPeakR));
    memset(xLPF1_L,0.0f,sizeof(xLPF1_L));
    memset(xLPF1_R,0.0f,sizeof(xLPF1_R));
    memset(yLPF1_L,0.0f,sizeof(yLPF1_L));
    memset(yLPF1_R,0.0f,sizeof(yLPF1_R));
    memset(xLPF2_L,0.0f,sizeof(xLPF2_L));
    memset(xLPF2_R,0.0f,sizeof(xLPF2_R));
    memset(yLPF2_L,0.0f,sizeof(yLPF2_L));
    memset(yLPF2_R,0.0f,sizeof(yLPF2_R));
    memset(xLPF3_L,0.0f,sizeof(xLPF3_L));
    memset(yLPF3_L,0.0f,sizeof(yLPF3_L));
    memset(xLPF3_R,0.0f,sizeof(xLPF3_R));
    memset(yLPF3_R,0.0f,sizeof(yLPF3_R));
    memset(xHighShelf_L,0.0f,sizeof(xHighShelf_L));
    memset(yHighShelf_L,0.0f,sizeof(yHighShelf_L));
    memset(xHighShelf_R,0.0f,sizeof(xHighShelf_R));
    memset(yHighShelf_R,0.0f,sizeof(yHighShelf_R));
    memset(numHPF1,0.0f,sizeof(numHPF1));
    memset(denHPF1,0.0f,sizeof(denHPF1));
    memset(numLPF1,0.0f,sizeof(numLPF1));
    memset(denLPF1,0.0f,sizeof(denLPF1));
    memset(numHPF2,0.0f,sizeof(numHPF2));
    memset(denHPF2,0.0f,sizeof(denHPF2));
    memset(numLPF2,0.0f,sizeof(numLPF2));
    memset(denLPF2,0.0f,sizeof(denLPF2));
    memset(numHPF3,0.0f,sizeof(numHPF3));
    memset(denHPF3,0.0f,sizeof(denHPF3));
    memset(numLPF3,0.0f,sizeof(numLPF3));
    memset(denLPF3,0.0f,sizeof(denLPF3));
    memset(numPeak,0.0f,sizeof(numPeak));
    memset(denPeak,0.0f,sizeof(denPeak));
}

void t1_EQ_fProcess(float* dataIn, float* dataOut)
{
    static uint8_t knob_backup[13] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    
    float xIn[2];
    float lpf_fc, hpf_fc;
    float low_shelf_fc, low_shelf_gain;
    float high_shelf_fc, high_shelf_gain;
    float peak_fc, peak_gain, peak_q, peak_fb;
    float peak_fl, peak_fh;
    float xLowOutL, xLowOutR, xHighOutL, xHighOutR;
    float outL, outR;


    int i;
    uint8_t param_changed = 0;
    int index = (t1_EQ)?(t1_EQ-1):0;
    for(int i=0; i<13; i++)
    {
        
        if(EQ_T1_knob[i]!=getParam(index,i))
        {
            EQ_T1_knob[i] = getParam(index,i);
        }
    }    

    for(int i=0; i<13; i++)
    {
        if(knob_backup[i] != EQ_T1_knob[i])
        {
            param_changed = 1;
            knob_backup[i] = EQ_T1_knob[i];
        }
    }
    uint8_t lowType = EQ_T1_knob[0];
    uint8_t lowCut_rolloff = EQ_T1_knob[1];
    hpf_fc = 60.0f + 160.0f * (EQ_T1_knob[2]*d255);
    float lowShelfGain = -12.0f + 24.0f * (EQ_T1_knob[3]*d255);
    float lowShelf_fc = 60.0f + 160.0f * (EQ_T1_knob[4]*d255);
    float peak_width = 0.1f + 1.9f * (EQ_T1_knob[5]*d255);
    peak_fc = 220.0f + 7780.0f * (EQ_T1_knob[7]*d255);
    peak_fl = peak_fc - (peak_fc * peak_width * 0.5f);
    if (peak_fl < 220.0f)
        peak_fl = 220.0f;
    
    peak_fh = peak_fc + (peak_fc * peak_width);
    peak_fb = peak_fh - peak_fl;
    peak_q = peak_fc/peak_fb;
    peak_gain = -12.0f + 24.0f * (EQ_T1_knob[6]*d255);
    uint8_t highType = EQ_T1_knob[8];
    uint8_t highCut_rolloff = EQ_T1_knob[9];
    lpf_fc = 6000.0f + 6000.0f * (EQ_T1_knob[10]*d255);
    float highShelfGain = -12.0f + 24.0f * (EQ_T1_knob[11]*d255);
    float highShelf_fc = 6000.0f + 6000.0f * (EQ_T1_knob[12]*d255);


    if (param_changed) {
        if (lowType < 128)
        {
            if (lowCut_rolloff < 85)
            {
                firstOrderHPFParaCalc(hpf_fc, numHPF1, denHPF1);
            }
            else if (lowCut_rolloff >= 85 && lowCut_rolloff < 170)
            {
                secondOrderHPFParaCalc(hpf_fc, numHPF2, denHPF2);
            }
            else
            {
                fourthOrderHPFParaCalc(hpf_fc, numHPF3, denHPF3);
            }   
        }
        else
        {
            lowShelfParaCalc(lowShelf_fc, lowShelfGain, numLowShelf, denLowShelf);
        }

        peakParaCalc(peak_fc, peak_gain, peak_q, numPeak, denPeak);
        if (highType < 128)
        {
            if (highCut_rolloff < 85)
            {
                firstOrderLPFParaCalc(lpf_fc, numLPF1, denLPF1);
            }
            else if (highCut_rolloff >= 85 && highCut_rolloff < 170)
            {
                secondOrderLPFParaCalc(lpf_fc, numLPF2, denLPF2);
            }
            else
            {
                fourthOrderLPFParaCalc(lpf_fc, numLPF3, denLPF3);
            }   
        }
        else
        {
            highShelfParaCalc(highShelf_fc, highShelfGain, numHighShelf, denHighShelf);
        }
    }
 
        xIn[0] = dataIn[0];
        xIn[1] = dataIn[0];
       
        if (lowType < 128)
        {
            if (lowCut_rolloff < 85)
            {
                xHPF1_L[0] = xIn[0];
                firstOrderFilter(numHPF1, denHPF1, xHPF1_L, yHPF1_L);
                xLowOutL = yHPF1_L[0];
            }
            else if (lowCut_rolloff >= 85 && lowCut_rolloff < 170)
            {
                xHPF2_L[0] = xIn[0];
                secondOrderFilter(numHPF2, denHPF2, xHPF2_L, yHPF2_L);
                xLowOutL = yHPF2_L[0];
            }
            else
            {
                xHPF3_L[0] = xIn[0];
                secondOrderFilter(numHPF3, denHPF3, xHPF3_L, yHPF3_L);
                xHPF3_L[3] = yHPF3_L[0];
                secondOrderFilter(&numHPF3[3], &denHPF3[2], &xHPF3_L[3], &yHPF3_L[3]);
                xLowOutL = yHPF3_L[3];
            }
        }
        else
        {
            xLowShelf_L[0] = xIn[0];
            secondOrderFilterVA(numLowShelf, denLowShelf, xLowShelf_L, yLowShelf_L);
            xLowOutL = yLowShelf_L[0];
        }
        
        xPeakL[0] = xLowOutL;
        secondOrderFilter(numPeak, denPeak, xPeakL, yPeakL);
        
        if (highType < 128)
        {
            if (highCut_rolloff < 85)
            {
                xLPF1_L[0] = yPeakL[0];
                firstOrderFilter(numLPF1, denLPF1, xLPF1_L, yLPF1_L);
                xHighOutL = yLPF1_L[0];
            }
            else if (highCut_rolloff >= 85 && highCut_rolloff < 170)
            {
                xLPF2_L[0] = yPeakL[0];
                secondOrderFilter(numLPF2, denLPF2, xLPF2_L, yLPF2_L);
                xHighOutL = yLPF2_L[0];
            }
            else
            {
                xLPF3_L[0] = yPeakL[0];
                secondOrderFilter(numLPF3, denLPF3, xLPF3_L, yLPF3_L);
                xLPF3_L[3] = yLPF3_L[0];
                secondOrderFilter(&numLPF3[3], &denLPF3[2], &xLPF3_L[3], &yLPF3_L[3]);
                xHighOutL = yLPF3_L[3];
            }
        }
        else
        {
            xHighShelf_L[0] = yPeakL[0];
            secondOrderFilterVA(numHighShelf, denHighShelf, xHighShelf_L, yHighShelf_L);
            xHighOutL = yHighShelf_L[0];
        }


        /* DAILE bus is mono: skip duplicate R filter bank. */
        outL = xHighOutL;
        outR = outL;

        dataOut[0] = outL;
        dataOut[1] = outR;

        


}

#endif