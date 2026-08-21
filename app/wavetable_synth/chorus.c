#if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "chorus.h"
#include "pureDelay.h"
#include "xmos_lut.h"
uint8_t getParam(uint8_t algoId, uint8_t paramNo);
int preBuffersize = 288, bufferSize = 144, preReadpoint, preWritepoint, writePoint, readPoint1, readPoint2;
static float depth;
float preBuffer[288];
float buffer1[144];
float buffer2[144];
float phase = 0.0f;
float RateOld = 1;
float SR = 48000.0f;
float phaseInc;

float Chorus_update(float rate, float sampleRate)
{
	return rate / sampleRate;
}

float lfo(float phase)
{
	return xmos_lfo_sine01(phase);
}

void chorus_init()
{
    memset(preBuffer, 0.0f, sizeof(preBuffer));
    memset(buffer1, 0.0f, sizeof(buffer1));
    memset(buffer2, 0.0f, sizeof(buffer2));
    preReadpoint = 0;
    preWritepoint = preBuffersize - 1;
    writePoint = 0;
    depth = 144.0f;
	phaseInc = Chorus_update(RateOld, SR);
}


void  t1_ChorusSine_fProcess(float* dataIn, float* dataOut)
{ 

    int m1;
    float choDepth, choRate, vol;

    uint8_t param_changed = 0;
    int index = (t1_ChorusSine)?(t1_ChorusSine-1):0;

    for(int i=0; i<4; i++)
    {
        
        if(chorus_T1_knob[i]!=getParam(index,i))
        {
            chorus_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }        

    vol = chorus_T1_knob[0]*d255;
    choRate = 0.1f + A25[chorus_T1_knob[1]]*4.9f;
    choDepth = chorus_T1_knob[2]*d255;
    (void)chorus_T1_knob[3]; /* spread unused on mono bus */
    (void)param_changed;

    float inputL, yp;
    float tap1, frac1;
    float dm11, dm21;
    float yn1, y1;
    float sineAmp;
   
	if (choRate != RateOld)
	{
		phaseInc = Chorus_update(choRate, SR);
		RateOld = choRate;
	}

	inputL = dataIn[0];

	preBuffer[preWritepoint] = inputL;
	preWritepoint += 1;
	if (preWritepoint > preBuffersize - 1)
	{
		preWritepoint = 0;
	}
	yp = preBuffer[preReadpoint];
	preReadpoint += 1;
	if (preReadpoint > preBuffersize - 1)
	{
		preReadpoint = 0;
	}



	/* Mono bus: one modulated tap (spread unused). */
	float lfoL = lfo(phase);

	sineAmp = choDepth * (depth - 15.0f);
	tap1 = sineAmp * lfoL + 15.0f;
	m1 = (int)tap1;
	frac1 = tap1 - m1;

	readPoint1 = writePoint - m1;
	if (readPoint1 < 0)
	{
		readPoint1 += bufferSize;
	}
	dm11 = buffer1[readPoint1];

	readPoint1 -= 1;
	if (readPoint1 < 0)
	{
		readPoint1 += bufferSize;
	}
	dm21 = buffer1[readPoint1];

	yn1 = dm21 * frac1 + dm11 * (1 - frac1);

	buffer1[writePoint] = yp;
	writePoint += 1;
	if (writePoint >= bufferSize)
	{
		writePoint = 0;
	}

	phase += phaseInc;
	if (phase >= 1.0f)
	phase -= 1.0f;

	y1 = inputL + vol * yn1;

	if (y1 > 1.0f)
		y1 = 1.0f;
	else if (y1 < -1.0f)
		y1 = -1.0f;

	dataOut[0] = y1;
	dataOut[1] = y1;
    

}
#endif