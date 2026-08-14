#if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "Tremolo.h"
#include "xmos_lut.h"
uint8_t getParam(uint8_t algoId, uint8_t paramNo);
static float SR = 48000.0f;

typedef enum {
    TREMOLO_SINE,
    TREMOLO_TRIANGLE,
    TREMOLO_SAW,
    TREMOLO_SQUARE
} TremoloWaveform;

typedef struct {
    float sampleRate;

    float rate;        
    float depth_dB;    

    float phase;       
    float phaseInc;

    float minGain;

    TremoloWaveform waveform;
} Tremolo;

Tremolo tremolo = {
    .sampleRate = 48000.0f,
    .rate = 1.0f,
    .depth_dB = -96.0f,
    .phase = 0.0f,
    .waveform = TREMOLO_SINE
};

static float alpha = 0.002614569920637f; 
static float gainPre;
static float smoothGain[2] = {0.0f, 0.0f};

static void onePoleLPF(float *x, float *y, float alpha)
{
    y[0] = alpha * x[0] + (1.0f - alpha) * y[1];
    y[1] = y[0];
}

void tremolo_init(Tremolo* t, float fs)
{
    t->sampleRate = fs;
    t->phase = 0.0f;
    t->rate = 1.0f;
    t->depth_dB = -96.0f;
    t->waveform = TREMOLO_SINE;
}


void Tremolo_rate_update(Tremolo* t)
{
    t->phaseInc = t->rate / t->sampleRate;
}

void Tremolo_depth_update(Tremolo* t)
{
    t->minGain = powf(10.0f, t->depth_dB / 20.0f);
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
    return phase;  // 0~1
}

static float lfo_square(float phase)
{
    return (phase < 0.5f) ? 1.0f : 0.0f;
}

float Tremolo_lfo(Tremolo* t)
{
    switch (t->waveform)
    {
        case TREMOLO_SINE:     return lfo_sine(t->phase);
        case TREMOLO_TRIANGLE: return lfo_triangle(t->phase);
        case TREMOLO_SAW:      return lfo_saw(t->phase);
        case TREMOLO_SQUARE:   return lfo_square(t->phase);
        default: return 0.0f;
    }
}

void Tremolo_init()
{
    tremolo_init(&tremolo, SR);
    Tremolo_rate_update(&tremolo);
    Tremolo_depth_update(&tremolo);
}

void Tremolo_stereo_process(Tremolo* t, float xL, float xR, float* yL, float* yR)
{
    float lfo = Tremolo_lfo(t);

    float gain = t->minGain + (1.0f - t->minGain) * lfo;

    gainPre = gain;
    onePoleLPF(&gain, smoothGain, alpha);
    gain = smoothGain[0];

    *yL = xL * gain;
    *yR = xR * gain;

    t->phase += t->phaseInc;
    if (t->phase >= 1.0f)
        t->phase -= 1.0f;
}
void t1_Tremolo_fprocess(float* dataIn, float* dataOut)
{

    uint8_t param_changed = 0;
    int index = (t1_Tremolo)?(t1_Tremolo-1):0;

    for(int i=0; i<3; i++)
    {
        
        if(Tremolo_T1_knob[i]!=getParam(index,i))
        {
            Tremolo_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }        

    float Depth = -96.0f + A25[Tremolo_T1_knob[0]] * 84.0f;
    float Rate = 0.1f + A25[Tremolo_T1_knob[1]] * 4.9f;
    TremoloWaveform waveShape;
    if (Tremolo_T1_knob[2] < 64)
    {
        waveShape = TREMOLO_SINE;
    }
    else if (Tremolo_T1_knob[2] < 128 && Tremolo_T1_knob[2] >= 64)
    {
        waveShape = TREMOLO_TRIANGLE;
    }
    else if (Tremolo_T1_knob[2] < 192 && Tremolo_T1_knob[2] >= 128)
    {
        waveShape = TREMOLO_SAW;
    }
    else
    {
        waveShape = TREMOLO_SQUARE;
    }

    if (waveShape != tremolo.waveform)
    {
        tremolo.waveform = waveShape;
    }

    if (Rate !=  tremolo.rate)
    {
        tremolo.rate = Rate;
        Tremolo_rate_update(&tremolo);
    }

    if (Depth != tremolo.depth_dB)
    {
        tremolo.depth_dB = Depth;
        Tremolo_depth_update(&tremolo);
    }

    Tremolo_stereo_process(&tremolo, dataIn[0], dataIn[0], &dataOut[0], &dataOut[1]);
}

#endif