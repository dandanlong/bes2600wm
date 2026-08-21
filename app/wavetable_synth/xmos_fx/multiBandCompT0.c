/*
 * @Author: llq
 * @Date: 2025-06-10 17:30:47
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 14:20:06
 * @Description: 3 band compressor
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\multiBandCompT0.c
 */

#if 1
#include "global.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "xcore_math.h"
// #include "algo_buffers.h"
// #include "algo_buffersT0.h"
#include "multiBandComp.h"


static float gainLP1;
static float denLP1[2];
static float xLP1_L[2][3];
static float yLP1_L[2][3];
static float gainHP1;
static float denHP1[2];
static float xHP1_L[2][3];
static float yHP1_L[2][3];
static float gainLP2;
static float denLP2[2];
static float xLP2_L[2][3];
static float yLP2_L[2][3];
static float gainHP2;
static float denHP2[2];
static float xHP2_L[2][3];
static float yHP2_L[2][3];
static float xLP1_R[2][3];
static float yLP1_R[2][3];
static float xHP1_R[2][3];
static float yHP1_R[2][3];
static float xLP2_R[2][3];
static float yLP2_R[2][3];
static float xHP2_R[2][3];
static float yHP2_R[2][3];

static float xPfL00[3];
static float yPfL00[3];
static float xPfL01[3];
static float yPfL01[3];
static float xPfR00[3];
static float yPfR00[3];
static float xPfR01[3];
static float yPfR01[3];
static float xPrevLowL, xPrevLowR;
static float xPrevMidL, xPrevMidR;
static float xPrevHighL, xPrevHighR;
static float xlowRMSL, xlowRMSR;
static float xmidRMSL, xmidRMSR;
static float xhighRMSL, xhighRMSR;
static float gGainLowL, gGainLowR;
static float gGainMidL, gGainMidR;
static float gGainHighL, gGainHighR;

static float lookaheadBufferL_low[LOOKAHEAD_SIZE];
static float lookaheadBufferL_mid[LOOKAHEAD_SIZE];
static float lookaheadBufferL_high[LOOKAHEAD_SIZE];
static float lookaheadBufferR_low[LOOKAHEAD_SIZE];
static float lookaheadBufferR_mid[LOOKAHEAD_SIZE];
static float lookaheadBufferR_high[LOOKAHEAD_SIZE];
static int writePointerL_low = 0;
static int writePointerL_mid = 0;
static int writePointerL_high = 0;
static int writePointerR_low = 0;
static int writePointerR_mid = 0;
static int writePointerR_high = 0;


static float csTab[256] = { 
-9.000000f,-8.929134f,-8.858268f,-8.787402f,-8.716535f,-8.645669f,-8.574803f,-8.503937f,
-8.433071f,-8.362205f,-8.291339f,-8.220472f,-8.149606f,-8.078740f,-8.007874f,-7.937008f,
-7.866142f,-7.795276f,-7.724409f,-7.653543f,-7.582677f,-7.511811f,-7.440945f,-7.370079f,
-7.299213f,-7.228346f,-7.157480f,-7.086614f,-7.015748f,-6.944882f,-6.874016f,-6.803150f,
-6.732283f,-6.661417f,-6.590551f,-6.519685f,-6.448819f,-6.377953f,-6.307087f,-6.236220f,
-6.165354f,-6.094488f,-6.023622f,-5.952756f,-5.881890f,-5.811024f,-5.740157f,-5.669291f,
-5.598425f,-5.527559f,-5.456693f,-5.385827f,-5.314961f,-5.244094f,-5.173228f,-5.102362f,
-5.031496f,-4.960630f,-4.889764f,-4.818898f,-4.748031f,-4.677165f,-4.606299f,-4.535433f,
-4.464567f,-4.393701f,-4.322835f,-4.251969f,-4.181102f,-4.110236f,-4.039370f,-3.968504f,
-3.897638f,-3.826772f,-3.755906f,-3.685039f,-3.614173f,-3.543307f,-3.472441f,-3.401575f,
-3.330709f,-3.259843f,-3.188976f,-3.118110f,-3.047244f,-2.976378f,-2.905512f,-2.834646f,
-2.763780f,-2.692913f,-2.622047f,-2.551181f,-2.480315f,-2.409449f,-2.338583f,-2.267717f,
-2.196850f,-2.125984f,-2.055118f,-1.984252f,-1.913386f,-1.842520f,-1.771654f,-1.700787f,
-1.629921f,-1.559055f,-1.488189f,-1.417323f,-1.346457f,-1.275591f,-1.204724f,-1.133858f,
-1.062992f,-0.992126f,-0.921260f,-0.850394f,-0.779528f,-0.708661f,-0.637795f,-0.566929f,
-0.496063f,-0.425197f,-0.354331f,-0.283465f,-0.212598f,-0.141732f,-0.070866f,0.000000f,
0.000000f,0.007087f,0.014173f,0.021260f,0.028346f,0.035433f,0.042520f,0.049606f,
0.056693f,0.063780f,0.070866f,0.077953f,0.085039f,0.092126f,0.099213f,0.106299f,
0.113386f,0.120472f,0.127559f,0.134646f,0.141732f,0.148819f,0.155906f,0.162992f,
0.170079f,0.177165f,0.184252f,0.191339f,0.198425f,0.205512f,0.212598f,0.219685f,
0.226772f,0.233858f,0.240945f,0.248031f,0.255118f,0.262205f,0.269291f,0.276378f,
0.283465f,0.290551f,0.297638f,0.304724f,0.311811f,0.318898f,0.325984f,0.333071f,
0.340157f,0.347244f,0.354331f,0.361417f,0.368504f,0.375591f,0.382677f,0.389764f,
0.396850f,0.403937f,0.411024f,0.418110f,0.425197f,0.432283f,0.439370f,0.446457f,
0.453543f,0.460630f,0.467717f,0.474803f,0.481890f,0.488976f,0.496063f,0.503150f,
0.510236f,0.517323f,0.524409f,0.531496f,0.538583f,0.545669f,0.552756f,0.559843f,
0.566929f,0.574016f,0.581102f,0.588189f,0.595276f,0.602362f,0.609449f,0.616535f,
0.623622f,0.630709f,0.637795f,0.644882f,0.651969f,0.659055f,0.666142f,0.673228f,
0.680315f,0.687402f,0.694488f,0.701575f,0.708661f,0.715748f,0.722835f,0.729921f,
0.737008f,0.744094f,0.751181f,0.758268f,0.765354f,0.772441f,0.779528f,0.786614f,
0.793701f,0.800787f,0.807874f,0.814961f,0.822047f,0.829134f,0.836220f,0.843307f,
0.850394f,0.857480f,0.864567f,0.871654f,0.878740f,0.885827f,0.892913f,0.900000f};


static void butterParaCalcLP(float Rc, float *num, float *den)
{
    float k2, temp1, temp;
    k2 = Rc * Rc;
    temp = k2*0.707f;
    temp1 = temp + Rc + 0.707f;
    num[0] = temp/temp1;
    den[0] = 1.414f*(k2 - 1.0f)/temp1;
    den[1] = (temp - Rc + 0.707f)/temp1;

}

static void butterParaCalcHP(float Rc, float *num, float *den)
{
    float k2, temp1, temp;
    k2 = Rc * Rc;
    temp = k2*0.707f;
    temp1 = temp + Rc + 0.707f;
    num[0] = 0.707f/temp1;
    den[0] = 1.414f*(k2 - 1.0f)/temp1;
    den[1] = (temp - Rc + 0.707f)/temp1;

}

static float compAlg(float attack, float release, float CS, float thres, float makeUpGain, float *prevRMS, float *currGain, float input, float inputDelay)
{
	float xin, xin2, xRMS, tav;
	float envdB, F, f, compOut;
	xin = input;
	xin2 = input * input;
    if (*prevRMS <= xin2)
		tav = attack;
	else
		tav = release;

	xRMS = (1.0f - tav) * (*prevRMS) + tav * xin2;
	*prevRMS = xRMS;
	envdB = 10*log10f(xRMS);

    if (CS >= 0.0f)
    { 
        if (envdB > thres){
		    F = CS * (thres - envdB);
		    f = powf(10.0f, F*0.05f);
		    if (f > 1.0f){
			    f = 1.0f;
		    }
	    }
	    else{
		    f = 1.0f;
	    }
    }
    else{ 
        if (envdB < thres){
            F = CS * (thres - envdB);
            f = powf(10.0f, F*0.05f);
            if (f > 1.0f){
                f = 1.0f;
            }
        }
        else{
            f = 1.0f;
        }
    }
	

	if (f < (*currGain)){
		*currGain = (1.0f - attack) * f + attack * (*currGain);
	}
	else{
		*currGain = (1.0f - release) * f + release * (*currGain);
	}

    compOut = inputDelay * (*currGain) * makeUpGain;
	return compOut;
}

void multiBandCompT0_init()
{
    gainLP1 = 0.0f;
    gainHP1 = 0.0f;
    gainLP2 = 0.0f;
    gainHP2 = 0.0f;
    xlowRMSL = 0.0f;
    xlowRMSR = 0.0f;
    xmidRMSL = 0.0f;
    xmidRMSR = 0.0f;
    xhighRMSL = 0.0f;
    xhighRMSR = 0.0f;
    xPrevLowL = 0.0f;
    xPrevLowR = 0.0f;
    xPrevMidL = 0.0f;
    xPrevMidR = 0.0f;
    xPrevHighL = 0.0f;
    xPrevHighR = 0.0f;
    gGainLowL = 1.0f;
    gGainLowR = 1.0f;
    gGainMidL = 1.0f;
    gGainMidR = 1.0f;
    gGainHighL = 1.0f;
    gGainHighR = 1.0f;
    memset(xPfL00,0.0f,sizeof(xPfL00));
    memset(xPfR00,0.0f,sizeof(xPfR00));
    memset(yPfL00,0.0f,sizeof(yPfL00));
    memset(yPfR00,0.0f,sizeof(yPfR00));
    memset(xPfL01,0.0f,sizeof(xPfL01));
    memset(xPfR01,0.0f,sizeof(xPfR01));
    memset(yPfL01,0.0f,sizeof(yPfL01));
    memset(yPfR01,0.0f,sizeof(yPfR01));
    memset(denLP1,0.0f,sizeof(denLP1));
    memset(denHP1,0.0f,sizeof(denHP1));
    memset(denLP2,0.0f,sizeof(denLP2));
    memset(denHP2,0.0f,sizeof(denHP2));
    memset(xLP1_L,0.0f,sizeof(xLP1_L));
    memset(yLP1_L,0.0f,sizeof(yLP1_L));
    memset(xHP1_L,0.0f,sizeof(xHP1_L));
    memset(yHP1_L,0.0f,sizeof(yHP1_L));
    memset(xLP2_L,0.0f,sizeof(xLP2_L));
    memset(yLP2_L,0.0f,sizeof(yLP2_L));
    memset(xHP2_L,0.0f,sizeof(xHP2_L));
    memset(yHP2_L,0.0f,sizeof(yHP2_L));
    memset(xLP1_R,0.0f,sizeof(xLP1_R));
    memset(yLP1_R,0.0f,sizeof(yLP1_R));
    memset(xHP1_R,0.0f,sizeof(xHP1_R));
    memset(yHP1_R,0.0f,sizeof(yHP1_R));
    memset(xLP2_R,0.0f,sizeof(xLP2_R));
    memset(yLP2_R,0.0f,sizeof(yLP2_R));
    memset(xHP2_R,0.0f,sizeof(xHP2_R));
    memset(yHP2_R,0.0f,sizeof(yHP2_R));

    memset(lookaheadBufferL_low,0.0f,sizeof(lookaheadBufferL_low));
    memset(lookaheadBufferR_low,0.0f,sizeof(lookaheadBufferR_low));
    memset(lookaheadBufferL_mid,0.0f,sizeof(lookaheadBufferL_mid));
    memset(lookaheadBufferR_mid,0.0f,sizeof(lookaheadBufferR_mid));
    memset(lookaheadBufferL_high,0.0f,sizeof(lookaheadBufferL_high));
    memset(lookaheadBufferR_high,0.0f,sizeof(lookaheadBufferR_high));

}




void t0_CompL_fProcess(float* dataIn, float* dataOut)
{
    
    static float fc1;
    static float fc2;
    static float K1, K2;
    static float xIn[2];
    static int Rthr1;
    static int Rthr2;
    static int Rthr3;
    static int Rrat1;
    static int Rrat2;
    static int Rrat3;
    static int Ratt1;
    static int Ratt2;
    static int Ratt3;
    static int Rrel1;
    static int Rrel2;
    static int Rrel3;

    static float att1, att2, att3;
    static float rt1, rt2, rt3;
    static float CS1, CS2, CS3;
    static float thres1, thres2, thres3;
    static float mkGainLowL, mkGainMidL, mkGainHighL;
    static float mkGainLowR, mkGainMidR, mkGainHighR;
    static float xlowBandL, xlowBandL2;
    static float xlowBandR, xlowBandR2;
    static float xmidBandL, xmidBandL2;
    static float xmidBandR, xmidBandR2;
    static float xhighBandL, xhighBandL2;
    static float xhighBandR, xhighBandR2;
    static float envdBlowL, envdBlowR;
    static float envdBmidL, envdBmidR;
    static float envdBhighL, envdBhighR;
    static float FL1, fL1, FR1, fR1;
    static float FL2, fL2, FR2, fR2;
    static float FL3, fL3, FR3, fR3;
    static float youtL1, youtR1;
    static float youtL2, youtR2;
    static float youtL3, youtR3;
    static float youtL, youtR;
    static float lookaheadL_low, lookaheadL_mid, lookaheadL_high;
    static float lookaheadR_low, lookaheadR_mid, lookaheadR_high;
    static int i;
    static int lookaheadVar;
    static float globalGaindB, globalGainLin;
    static uint8_t knob_backup[19] = {0};

    uint8_t param_changed = 0;
    int index= 0;
    for(int i=0; i<13; i++)
    {
        if(sT0_algo_paras[index].algoParas[i]!= multibandCompressor_T0_knob[i])
        {
            multibandCompressor_T0_knob[i] = sT0_algo_paras[index].algoParas[i];
        }
    }  


    if(knob_backup[2] != multibandCompressor_T0_knob[2])
    {
        globalGaindB = -12.0f + 24.0f * (multibandCompressor_T0_knob[2] * d255); // -25dB ~ +25dB
        globalGainLin = powf(10.0f,globalGaindB*0.05f);
        knob_backup[2] = multibandCompressor_T0_knob[2];
    }

    if (multibandCompressor_T0_knob[3]<85)
    {
        lookaheadVar = 64;
    }
    else if(multibandCompressor_T0_knob[3] >= 85 && multibandCompressor_T0_knob[3]<170)
    {
        lookaheadVar = 128;
    }
    else
    {
        lookaheadVar = 256;
    }

   
    if (knob_backup[7] != multibandCompressor_T0_knob[7])
    {    
        att1 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[7] * d255)))); 
        knob_backup[7] = multibandCompressor_T0_knob[7];
    }
    if (knob_backup[12] != multibandCompressor_T0_knob[12])
    {    
        att2 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[12] * d255))));
        knob_backup[12] = multibandCompressor_T0_knob[12];
    }
    if (knob_backup[17] != multibandCompressor_T0_knob[17])
    {    
        att3 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[17] * d255))));
        knob_backup[17] = multibandCompressor_T0_knob[17];
    }
    if (knob_backup[8] != multibandCompressor_T0_knob[8])
    {    
        rt1 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[8] * d255))));
        knob_backup[8] = multibandCompressor_T0_knob[8];
    }
    if (knob_backup[13] != multibandCompressor_T0_knob[13])
    {    
        rt2 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[13] * d255))));
        knob_backup[13] = multibandCompressor_T0_knob[13];
    }
    if (knob_backup[18] != multibandCompressor_T0_knob[18])
    {    
        rt3 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[18] * d255))));
        knob_backup[18] = multibandCompressor_T0_knob[18];
    }
    
    CS1 = multibandCompressor_T0_knob[5];   
    CS2 = multibandCompressor_T0_knob[10];
    CS3 = multibandCompressor_T0_knob[15];
   
    if (knob_backup[4] != multibandCompressor_T0_knob[4])
    {    
        thres1 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[4] * d255); 
        knob_backup[4] = multibandCompressor_T0_knob[4];
    }
    if (knob_backup[9] != multibandCompressor_T0_knob[9])
    {    
        thres2 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[9] * d255);
        knob_backup[9] = multibandCompressor_T0_knob[9];
    }
    if (knob_backup[14] != multibandCompressor_T0_knob[14])
    {    
        thres3 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[14] * d255);
        knob_backup[14] = multibandCompressor_T0_knob[14];
    }
    
    if (knob_backup[6] != multibandCompressor_T0_knob[6])
    {    
        mkGainLowL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[6] * d255)) * 0.05f);
        mkGainLowR = mkGainLowL;
        knob_backup[6] = multibandCompressor_T0_knob[6];
    }
    if (knob_backup[11] != multibandCompressor_T0_knob[11])
    {    
        mkGainMidL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[11] * d255)) * 0.05f);
        mkGainMidR = mkGainMidL;
        knob_backup[11] = multibandCompressor_T0_knob[11];
    }
    if (knob_backup[16] != multibandCompressor_T0_knob[16])
    {    
        mkGainHighL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[16] * d255)) * 0.05f);
        mkGainHighR = mkGainHighL;	
        knob_backup[16] = multibandCompressor_T0_knob[16];
    }
   

       
        dataOut[0] = dataIn[0];
        dataOut[1] = dataIn[1];
        
}

void t0_CompR_fProcess(float* dataIn, float* dataOut)
{

    static float fc1;
    static float fc2;
    static float K1, K2;
    static float xIn[2];
    static int Rthr1;
    static int Rthr2;
    static int Rthr3;
    static int Rrat1;
    static int Rrat2;
    static int Rrat3;
    static int Ratt1;
    static int Ratt2;
    static int Ratt3;
    static int Rrel1;
    static int Rrel2;
    static int Rrel3;

    static float att1, att2, att3;
    static float rt1, rt2, rt3;
    static float CS1, CS2, CS3;
    static float thres1, thres2, thres3;
    static float mkGainLowL, mkGainMidL, mkGainHighL;
    static float mkGainLowR, mkGainMidR, mkGainHighR;
    static float xlowBandL, xlowBandL2;
    static float xlowBandR, xlowBandR2;
    static float xmidBandL, xmidBandL2;
    static float xmidBandR, xmidBandR2;
    static float xhighBandL, xhighBandL2;
    static float xhighBandR, xhighBandR2;
    static float envdBlowL, envdBlowR;
    static float envdBmidL, envdBmidR;
    static float envdBhighL, envdBhighR;
    static float FL1, fL1, FR1, fR1;
    static float FL2, fL2, FR2, fR2;
    static float FL3, fL3, FR3, fR3;
    static float youtL1, youtR1;
    static float youtL2, youtR2;
    static float youtL3, youtR3;
    static float youtL, youtR;
    static float lookaheadL_low, lookaheadL_mid, lookaheadL_high;
    static float lookaheadR_low, lookaheadR_mid, lookaheadR_high;
    int i;
    int lookaheadVar;
    static float globalGaindB, globalGainLin;

    
    static float knob_backup[19] = {0};

    uint8_t param_changed = 0;
    int index= 0;
    for(int i=0; i<13; i++)
    {
        if(sT0_algo_paras[index].algoParas[i]!= multibandCompressor_T0_knob[i])
        {
            multibandCompressor_T0_knob[i] = sT0_algo_paras[index].algoParas[i];
        }
    }  

    if (knob_backup[0] != multibandCompressor_T0_knob[0]) {
        fc1 = 80.0f + 800.0f * (multibandCompressor_T0_knob[0] * d255);
        K1 = tanf(6.545e-5f * fc1); // K = tan(pi*fc/fs), 褰撳墠fs涓?8000Hz
        knob_backup[0] = multibandCompressor_T0_knob[0];
    }
    if (knob_backup[1] != multibandCompressor_T0_knob[1]) {
        fc2 = 1100.0f + 18900.0f * (multibandCompressor_T0_knob[1] * d255);
        K2 = tanf(6.545e-5f * fc2);
        knob_backup[1] = multibandCompressor_T0_knob[1];
    }
    if (knob_backup[2] != multibandCompressor_T0_knob[2]) {
        globalGaindB = -12.0f + 24.0f * (multibandCompressor_T0_knob[2] * d255);
        globalGainLin = powf(10.0f,globalGaindB*0.05f);
        knob_backup[2] = multibandCompressor_T0_knob[2];
    }

    if (multibandCompressor_T0_knob[3]<85)
    {
        lookaheadVar = 64;
    }
    else if(multibandCompressor_T0_knob[3] >= 85 && multibandCompressor_T0_knob[3]<170)
    {
        lookaheadVar = 128;
    }
    else
    {
        lookaheadVar = 256;
    }

    if (knob_backup[7] != multibandCompressor_T0_knob[7]) {
        att1 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[7] * d255)))); 
        knob_backup[7] = multibandCompressor_T0_knob[7];
    }
    if (knob_backup[12] != multibandCompressor_T0_knob[12]) {
        att2 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[12] * d255))));
        knob_backup[12] = multibandCompressor_T0_knob[12];
    }
    if (knob_backup[17] != multibandCompressor_T0_knob[17]) {
        att3 = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(multibandCompressor_T0_knob[17] * d255))));
        knob_backup[17] = multibandCompressor_T0_knob[17];
    }
    if (knob_backup[8] != multibandCompressor_T0_knob[8]) {
        rt1 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[8] * d255)))); 
        knob_backup[8] = multibandCompressor_T0_knob[8];
    }
    if (knob_backup[13] != multibandCompressor_T0_knob[13]) {
        rt2 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[13] * d255))));
        knob_backup[13] = multibandCompressor_T0_knob[13];
    }
    if (knob_backup[18] != multibandCompressor_T0_knob[18]) {
        rt3 = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(multibandCompressor_T0_knob[18] * d255))));
        knob_backup[18] = multibandCompressor_T0_knob[18];
    }


    CS1 = multibandCompressor_T0_knob[5];    
    CS2 = multibandCompressor_T0_knob[10];
    CS3 = multibandCompressor_T0_knob[15];

    if (knob_backup[4] != multibandCompressor_T0_knob[4]) {
        thres1 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[4] * d255); 
        knob_backup[4] = multibandCompressor_T0_knob[4];
    }
    if (knob_backup[9] != multibandCompressor_T0_knob[9]) {
        thres2 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[9] * d255);
        knob_backup[9] = multibandCompressor_T0_knob[9];
    }
    if (knob_backup[14] != multibandCompressor_T0_knob[14]) {
        thres3 = - 96.0f + 96.0f * (multibandCompressor_T0_knob[14] * d255);
        knob_backup[14] = multibandCompressor_T0_knob[14];
    }


    if (knob_backup[6] != multibandCompressor_T0_knob[6]) {
        mkGainLowL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[6] * d255)) * 0.05f);
        mkGainLowR = mkGainLowL;
        knob_backup[6] = multibandCompressor_T0_knob[6];
    }
    if (knob_backup[11] != multibandCompressor_T0_knob[11]) {
        mkGainMidL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[11] * d255)) * 0.05f);
        mkGainMidR = mkGainMidL;
        knob_backup[11] = multibandCompressor_T0_knob[11];
    }
    if (knob_backup[16] != multibandCompressor_T0_knob[16]) {
        mkGainHighL = powf(10.0f,(-24.0f + 48.0f * (multibandCompressor_T0_knob[16] * d255)) * 0.05f);
        mkGainHighR = mkGainHighL;	
        knob_backup[16] = multibandCompressor_T0_knob[16];
    }
       
        dataOut[0] = dataIn[0];
        dataOut[1] = dataIn[1];
       
}


#endif