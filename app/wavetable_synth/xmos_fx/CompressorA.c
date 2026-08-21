#if 1
#include "global.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "xcore_math.h"
//#include "algo_buffers.h"
//#include "algo_buffersT0.h"
#include "CompressorA.h"

static float xPfL00[3];
static float yPfL00[3];
static float xPfL01[3];
static float yPfL01[3];
static float xPfR00[3];
static float yPfR00[3];
static float xPfR01[3];
static float yPfR01[3];
static float xPrevL, xPrevR;
static float xRMSL, xRMSR;
static float gGainL, gGainR;

static float lookaheadBufferL[LOOKAHEAD_SIZE];
static float lookaheadBufferR[LOOKAHEAD_SIZE];
static int writePointerL = 0;
static int writePointerR = 0;

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

void Compressor_init(void)
{
    memset(xPfL00, 0, sizeof(xPfL00));
    memset(yPfL00, 0, sizeof(yPfL00));
    memset(xPfL01, 0, sizeof(xPfL01));
    memset(yPfL01, 0, sizeof(yPfL01));
    memset(xPfR00, 0, sizeof(xPfR00));
    memset(yPfR00, 0, sizeof(yPfR00));
    memset(xPfR01, 0, sizeof(xPfR01));
    memset(yPfR01, 0, sizeof(yPfR01));
    xPrevL = 0.0f;
    xPrevR = 0.0f;
    xRMSL = 0.0f;
    xRMSR = 0.0f;
    gGainL = 1.0f;
    gGainR = 1.0f;
    memset(lookaheadBufferL, 0, sizeof(lookaheadBufferL));
    memset(lookaheadBufferR, 0, sizeof(lookaheadBufferR));
    writePointerL = 0;
    writePointerR = 0;
}

void t1_Compressor_fProcess(float* dataIn, float* dataOut)
{

    static uint8_t knob_backup[6] = {0,0,0,0,0,0};
    static float att_backup, rt_backup, CS_backup, thres_backup, mkGainL_backup, mkGainR_backup;


    float xIn[2];
    float att, rt, CS, thres, mkGainL, mkGainR;
    float youtL, youtR;
    float lookaheadL, lookaheadR;
    int lookaheadVar;
    float globalGaindB, globalGainLin;

    uint8_t param_changed = 0;
    int index = (t1_Compressor)?(t1_Compressor-1):0;

    for(int i=0; i<6; i++)
    {
        
        if(Compressor_T1_knob[i]!=getParam(index,i))
        {
            Compressor_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }        



    if (Compressor_T1_knob[1]<85)
    {
        lookaheadVar = 64;
    }
    else if(Compressor_T1_knob[1] >= 85 && Compressor_T1_knob[1]<170)
    {
        lookaheadVar = 128;
    }
    else
    {
        lookaheadVar = 256;
		
    }
	
	if (Compressor_T1_knob[2] != knob_backup[2]) {
        att_backup = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(Compressor_T1_knob[2] * d255))));
        knob_backup[2] = Compressor_T1_knob[2];
        // printf("att=%d\n", knob_backup[2]);
    }
    if (Compressor_T1_knob[3] != knob_backup[3]) {
        rt_backup = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(Compressor_T1_knob[3] * d255))));
        knob_backup[3] = Compressor_T1_knob[3];
        // printf("rt=%d\n", knob_backup[3]);
    }
    if (Compressor_T1_knob[4] != knob_backup[4]) {
        CS_backup = csTab[Compressor_T1_knob[4]];
        knob_backup[4] = Compressor_T1_knob[4];
        // printf("cs=%d\n", knob_backup[4]);
    }
    if (Compressor_T1_knob[5] != knob_backup[5]) {
        thres_backup = - 96.0f + 96.0f * (Compressor_T1_knob[5] * d255);
        knob_backup[5] = Compressor_T1_knob[5];
        // printf("thres=%d\n", knob_backup[5]);
    }
    if (Compressor_T1_knob[0] != knob_backup[0]) {
        mkGainL_backup = powf(10.0f,(-24.0f + 48.0f * (Compressor_T1_knob[0] * d255)) * 0.05f);
        knob_backup[0] = Compressor_T1_knob[0];
        // printf("gain=%d\n", knob_backup[0]);
    }
    att = att_backup;
    rt = rt_backup;
    CS = CS_backup;
    thres = thres_backup;
    mkGainL = mkGainL_backup;
    // att = expf(-logf(9)/(48000.0f*(0.001f + 0.999f*(Compressor_T1_knob[2] * d255))));
    // rt = expf(-logf(9)/(48000.0f*(0.01f + 1.99f*(Compressor_T1_knob[3] * d255))));
    // CS = csTab[Compressor_T1_knob[4]];
    // thres = - 96.0f + 96.0f * (Compressor_T1_knob[5] * d255);
    // mkGainL = powf(10.0f,(-24.0f + 48.0f * (Compressor_T1_knob[0] * d255)) * 0.05f);
	mkGainR = mkGainL;


     
        xIn[0] = dataIn[0];
        xIn[1] = dataIn[0];

        xPfL00[0] = xIn[0];
        secondOrderFilter(numHpf0, denHpf0, xPfL00, yPfL00);
        xPfL01[0] = yPfL00[0];
        secondOrderFilter(numHpf0, denHpf0, xPfL01, yPfL01);

        lookaheadL = delayLine(yPfL01[0], lookaheadBufferL, lookaheadVar, &writePointerL);
        youtL = compAlg(att, rt, CS, thres, mkGainL, &xPrevL, &gGainL, yPfL01[0], lookaheadL);

        /* DAILE bus is mono: skip duplicate R compressor path. */
        youtR = youtL;

        dataOut[0]  =  youtL;
        dataOut[1]  =  youtR;


}
#endif