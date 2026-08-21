#if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "pureDelay.h"
uint8_t getParam(uint8_t algoId, uint8_t paramNo);
float delayBuffer[2][MAX_DELAY_LEN];                
int rdPointer[2];                                    
int wrPointer[2];                                     
float feedback[2]; 
float readData;
float readData2[2];

typedef enum {
    MONO_DELAY,
    PING_PONG_DELAY
} DelayMode;

DelayMode Mode = MONO_DELAY;

void pureDelay_init()
{
    memset(delayBuffer,0.0f,sizeof(delayBuffer));
    rdPointer[0] = rdPointer[1] = 0;
    wrPointer[0] = wrPointer[1] = 0;
    feedback[0] = feedback[1] = 0.0f;
    readData = 0.0f;
    readData2[0] = readData2[1] = 0.0f;
}

float delay(int index, float data, float fb, float delayLength)
{
    int d = 0;
	float frac = 0.f;
	float input = data;
	float out = 0;

	d = (int)delayLength;
	frac = delayLength - d;

	out = fb * feedback[index];

    input = input + out;

	if(input > upLim){
		input = upLim;
	}else if(input < lowLim){
		input = lowLim;
	}

	delayBuffer[index][wrPointer[index]++] = input;

	rdPointer[index] = wrPointer[index] - d;
	if(rdPointer[index] < 0){
		rdPointer[index] += MAX_DELAY_LEN;
	}

	readData = delayBuffer[index][rdPointer[index]];

	if(wrPointer[index] > MAX_DELAY_LEN - 1){
		wrPointer[index] = 0;
	}

	out = readData2[index] * frac + readData * (1.f - frac);
	readData2[index] = readData;

	return out;
    
}

void  t1_PureDelay_fProcess(float *xin, float *xOut)
{ 

    int i;
    float delayLen; 
    float xIn[2], input_delay[2];
    int m1_delay, m2_delay;
    float yn_delay;
    float frac1_delay, frac2_delay, dm1_delay, dm2_delay;
    float out_delay[2];
    int j_delay;
    DelayMode modeChoose;

    uint8_t param_changed = 0;
    int index = (t1_PureDelay)?(t1_PureDelay-1):0;

    for(int i=0; i<4; i++)
    {
        
        if(delay_T1_knob[i]!=getParam(index,i))
        {
            delay_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }        

    float knobTime = delay_T1_knob[0]*d255;
    float knobFdbk = delay_T1_knob[1]*d255;
    float knobMix = delay_T1_knob[2]*d255;

    delayLen = MIN_DELAY_LEN + knobTime * (MAX_DELAY_LEN - MIN_DELAY_LEN);

    if (delay_T1_knob[3] < 128)
    {
        modeChoose = MONO_DELAY;
    }
    else
    {
        modeChoose = PING_PONG_DELAY;
    }

    if (modeChoose != Mode)
    {
        Mode = modeChoose;
    }


    input_delay[0] = xin[0];
    input_delay[1] = xin[0];

    /* DAILE bus is mono: one delay line is enough (incl. former ping-pong). */
    out_delay[0] = delay(0, input_delay[0], knobFdbk, delayLen);
    feedback[0] = out_delay[0];
    feedback[1] = out_delay[0];

    if (knobMix < 0.5f)
    {
        out_delay[0] = input_delay[0] + (out_delay[0] * knobMix) * 2.0f;
    }
    else {
        out_delay[0] = ((input_delay[0] * (1.0f - knobMix)) * 2.0f) + out_delay[0];
    }

    if (out_delay[0] > 1.0f)
    {
        out_delay[0] = 1.0f;
    }
    else if (out_delay[0] < -1.0f)
    {
        out_delay[0] = -1.0f;
    }
    out_delay[1] = out_delay[0];

    xOut[0] = out_delay[0];
    xOut[1] = out_delay[1];

}
#endif
