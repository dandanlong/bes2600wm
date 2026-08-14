/*
 * @Author: llq
 * @Date: 2026-04-02 14:45:37
 * @LastEditors: llq
 * @LastEditTime: 2026-05-13 14:41:43
 * @Description: autoPan
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\autoPan.c
 */

  #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "autoPan.h"
#include "xmos_lut.h"

static float SR = 48000.0f;

typedef enum {
    AUTOPAN_SINE,
    AUTOPAN_TRIANGLE,
    AUTOPAN_SAW,
    AUTOPAN_SQUARE
} AutoPanWaveform;

typedef struct {
    float sampleRate;

    float rate;      
    float width;     

    float phase;     
    float phaseInc;

    AutoPanWaveform waveform;
} AutoPan;

AutoPan autoPan = {
    .sampleRate = 48000.0f,
    .rate = 1.0f,
    .width = 0.5f,
    .phase = 0.0f,
    .waveform = AUTOPAN_SINE
};

static float alpha = 0.002614569920637f; 
static float gainPreL, gainPreR;
static float smoothGainL[2] = {0.0f, 0.0f};
static float smoothGainR[2] = {0.0f, 0.0f};

static void onePoleFilt(float *x, float *y, float alpha)
{
    y[0] = alpha * x[0] + (1.0f - alpha) * y[1];
    y[1] = y[0];
}

void autoPan_init(AutoPan* p, float fs)
{
    p->sampleRate = fs;
    p->phase = 0.0f;
    p->rate = 1.0f;
    p->width = 0.5f;
    p->waveform = AUTOPAN_SINE;

}

void AutoPan_update(AutoPan* p)
{
    p->phaseInc = p->rate / p->sampleRate;
}

static float lfo_sine(float phase)
{
    return xmos_lfo_sine01(phase);
}

static float lfo_triangle(float phase)
{
    if (phase < 0.5f)
        return phase * 2.0f;
    else
        return 2.0f * (1.0f - phase);
}

static float lfo_saw(float phase)
{
    return phase; 
}

static float lfo_square(float phase)
{
    return (phase < 0.5f) ? 1.0f : 0.0f;
}

float AutoPan_lfo(AutoPan* p)
{
    switch (p->waveform)
    {
        case AUTOPAN_SINE:     return lfo_sine(p->phase);
        case AUTOPAN_TRIANGLE: return lfo_triangle(p->phase);
        case AUTOPAN_SAW:      return lfo_saw(p->phase);
        case AUTOPAN_SQUARE:   return lfo_square(p->phase);
        default: return 0.5f;
    }
}

void AutoPan_init()
{
    autoPan_init(&autoPan, SR);
    AutoPan_update(&autoPan);
}

void AutoPan_process(AutoPan* p, float inL, float inR,
                     float* outL, float* outR)
{
    float lfo = AutoPan_lfo(p);
    float pan = (lfo * 2.0f - 1.0f) * p->width;
    float theta = (pan + 1.0f) * (M_PI * 0.25f);

    /* Mono bus: constant-power L/R → average gain (matches chain 0.5*(L+R)). */
    float gain = 0.5f * (xmos_sin_rad(theta + (float)M_PI * 0.5f)
                       + xmos_sin_rad(theta));
    gainPreL = gain;
    gainPreR = gain;
    onePoleFilt(&gain, smoothGainL, alpha);
    gain = smoothGainL[0];
    smoothGainR[0] = gain;
    smoothGainR[1] = smoothGainL[1];

    *outL = inL * gain;
    *outR = *outL;

    p->phase += p->phaseInc;
    if (p->phase >= 1.0f)
        p->phase -= 1.0f;
}

void t1_AutoPan_fprocess(float* dataIn, float* dataOut)
{
    float inputL, inputR;
    
    AutoPanWaveform waveShape;

    uint8_t param_changed = 0;
    int index = (t1_AutoPan)?(t1_AutoPan-1):0;

    for(int i=0; i<3; i++)
    {
        
        if(AutoPan_T1_knob[i]!=getParam(index,i))
        {
            AutoPan_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }    
    
    float Width = AutoPan_T1_knob[0] * d255; 
    float Rate = A25[AutoPan_T1_knob[1]] * 4.9f + 0.1f; 

    if (AutoPan_T1_knob[2] < 64)
    {
        waveShape = AUTOPAN_SINE;
    }
    else if (AutoPan_T1_knob[2] < 128 && AutoPan_T1_knob[2] >= 64)
    {
        waveShape = AUTOPAN_TRIANGLE;
    }
    else if (AutoPan_T1_knob[2] < 192 && AutoPan_T1_knob[2] >= 128)
    {
        waveShape = AUTOPAN_SAW;
    }
    else
    {
        waveShape = AUTOPAN_SQUARE;
    }

    if (waveShape != autoPan.waveform)
    {
        autoPan.waveform = waveShape;
    }
    
    if (Rate != autoPan.rate)
    {
        autoPan.rate = Rate;
        AutoPan_update(&autoPan);
    }

    if (Width != autoPan.width)
    {
        autoPan.width = Width;
    }

    inputL = dataIn[0];
    inputR = dataIn[0];

    float outL, outR;
    AutoPan_process(&autoPan, inputL, inputR, &outL, &outR);

    if (outL > 1.0f)
        outL = 1.0f;
    else if (outL < -1.0f)
        outL = -1.0f;

    if (outR > 1.0f)
        outR = 1.0f;
    else if (outR < -1.0f)
        outR = -1.0f;

    dataOut[0] = outL;
    dataOut[1] = outR;
}

#endif