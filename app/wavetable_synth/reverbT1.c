#if 1
#include "global.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "xcore_math.h"
#include "reverb.h"
#include "xmos_lut.h"



static float modAP1L[1101];
static float modAP1R[1091];
static float modAP2L[1061];
static float modAP2R[1035];
static float modAP3L[1054];
static float modAP3R[1044];
static float modAP4L[1255];
static float modAP4R[1383];
static float modAP5L[1579];
static float modAP5R[1677];
static float modAP6L[1881];
static float modAP6R[1885];

static int writPoint_AP1_L = 0;
static int writPoint_AP1_R = 0;
static int writPoint_AP2_L = 0;
static int writPoint_AP2_R = 0;
static int writPoint_AP3_L = 0;
static int writPoint_AP3_R = 0;
static int writPoint_AP4_L = 0;
static int writPoint_AP4_R = 0;
static int writPoint_AP5_L = 0;
static int writPoint_AP5_R = 0;
static int writPoint_AP6_L = 0;
static int writPoint_AP6_R = 0;

static float phase = 0.f;
static float SR = 48000.0f;
static float rate = 0.5f;
static float old_left = 0.f;
static float old_right = 0.f;

static float ldecOld[6];
static float rdecOld[6];
static float fDecOld = 1000.0f + 2400.0f * 0.5f;
static float tl[6] = {397.0f, 457.0f, 549.0f, 649.0f, 773.0f, 877.0f};
static float tr[6] = {383.0f, 429.0f, 631.0f, 756.0f, 803.0f, 901.0f};

static float yModAP1_L[2];
static float yModAP1_R[2];
static float yModAP2_L[2];
static float yModAP2_R[2];
static float yModAP3_L[2];
static float yModAP3_R[2];
static float yModAP4_L[2];
static float yModAP4_R[2];
static float yModAP5_L[2];
static float yModAP5_R[2];
static float yModAP6_L[2];
static float yModAP6_R[2];

static float yLP_L[2];
static float yLP_R[2];
static float dampOld;

static float gainLowShelfOld = -3.0f;
static float gainHighShelfOld = 0.0f;
static float num_lowShelf[3];
static float den_lowShelf[2];
static float num_highShelf[3];
static float den_highShelf[2];
static float x_lowShelf_L[3];
static float y_lowShelf_L[3];
static float x_highShelf_L[3];
static float y_highShelf_L[3];
static float x_lowShelf_R[3];
static float y_lowShelf_R[3];
static float x_highShelf_R[3];
static float y_highShelf_R[3];

static float delayLtoR_buffer[L_TO_R_DELAY];
static float delayRtoL_buffer[R_TO_L_DELAY];
static int writPoint_LtoR = 0;
static int writPoint_RtoL = 0;
static float num_allpassXL[3];
static float den_allpassXL[2];
static float num_allpassXR[3];
static float den_allpassXR[2];
static float x_allpassXL[3];
static float y_allpassXL[3];
static float x_allpassXR[3];
static float y_allpassXR[3];
static float num_allpassL2[3];
static float den_allpassL2[2];
static float num_allpassR2[3];
static float den_allpassR2[2];
static float x_allpassL2[3];
static float y_allpassL2[3];
static float x_allpassR2[3];
static float y_allpassR2[3];

static float num_brightHall[3] = {0.262874029585929f, 0.525748059171858f, 0.262874029585929f};
static float den_brightHall[2] = {-0.122741225012519f, 0.174237343356235f};
static float x_brightHall_L[3];
static float y_brightHall_L[3];
static float x_brightHall_R[3];
static float y_brightHall_R[3];
static float num_clearHall[3] = {0.142922941262408f, 0.285845882524816f, 0.142922941262408f};
static float den_clearHall[2] = {-0.683777651544954f, 0.255469416594585f};
static float x_clearHall_L[3];
static float y_clearHall_L[3];
static float x_clearHall_R[3];
static float y_clearHall_R[3];
static float num_darkHall[3] = {0.092358312213070f, 0.184716624426141f, 0.092358312213070f};
static float den_darkHall[2] = {-0.975791704164800f, 0.345224953017082f};
static float x_darkHall_L[3];
static float y_darkHall_L[3];
static float x_darkHall_R[3];
static float y_darkHall_R[3];


static EarlyReflections erR;

static void initER(EarlyReflections* er, float Fs)
{
    int delayEarly_L[ER_TAPS] = {955, 1699, 1867, 1987, 3355, 3821};
    int delayEarly_R[ER_TAPS] = {1003, 1747, 1915, 2035, 3403, 3869};
    float gainEarly_L[ER_TAPS] = {1.02f, 0.818f, 0.635f, 0.719f, 0.267f, 0.242f};
    float gainEarly_R[ER_TAPS] = {1.021f, 0.82f, 0.633f, 0.722f, 0.187f, 0.243f};

    for (int i = 0; i < ER_TAPS; i++)
    {
        er->delayL[i] = delayEarly_L[i];
        er->delayR[i] = delayEarly_R[i];
        er->gainL[i] = gainEarly_L[i];
        er->gainR[i] = gainEarly_R[i];
    }

    memset(er->bufferL, 0, sizeof(er->bufferL));
    memset(er->bufferR, 0, sizeof(er->bufferR));
    er->indexL = 0;
    er->indexR = 0;
}

static float calcAlpha(float freq, float bw, float fs, unsigned mode)
{
    float omega = 2.0f * M_PI * freq / fs;
    float sn = sinf(omega);
    switch(mode)
    {
        case BIQUAD_RBJ_BW:
            return sn * sinhf(M_LN2/2.0f * bw * omega/sn);
        case BIQUAD_RBJ_Q:
            return sn * (2.0f * bw);
        case BIQUAD_RBJ_S:
        default:
            break;
    }
    return 0;
} 

static void setAPF_RBJ(float freq, float bw, float fs, unsigned mode, float* num, float* den)
{
    float omega = 2.0f * M_PI * freq / fs;
    float cs = cosf(omega);
    float alpha = calcAlpha(freq, bw, fs, mode);
    float a0r = 1.0 / (1.0 + alpha);
    num[0] = a0r * (1.0 - alpha);
    num[1] = a0r * (-2.0 * cs);
    num[2] = a0r * (1.0 + alpha);
    den[0] = a0r * (-2.0 * cs);
    den[1] = a0r * (1.0 - alpha);
}

static void setLowShelf_RBJ(float freq, float gainDB, float fs, float* num, float* den)
{
    float A = powf(10.0f, gainDB / 40.0f);
    float omega = 2.0f * M_PI * freq / fs;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/0.5f - 1.0f) + 2.0f);
    float temp = 2.0f * sqrtf(A) * alpha;
    float a0r = 1.0f / ((A + 1.0f) + (A - 1.0f) * cs + temp);
    num[0] = a0r * (A * ((A + 1.0f) - (A - 1.0f) * cs + temp));
    num[1] = a0r * (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs));
    num[2] = a0r * (A * ((A + 1.0f) - (A - 1.0f) * cs - temp));
    den[0] = a0r * (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs));
    den[1] = a0r * (((A + 1.0f) + (A - 1.0f) * cs - temp));
}

static void setHighShelf_RBJ(float freq, float gainDB, float fs, float* num, float* den)
{
    float A = powf(10.0f, gainDB / 40.0f);
    float omega = 2.0f * M_PI * freq / fs;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/0.5f - 1.0f) + 2.0f);
    float temp = 2.0f * sqrtf(A) * alpha;
    float a0r = 1.0f / ((A + 1.0f) - (A - 1.0f) * cs + temp);
    num[0] = a0r * (A * ((A + 1.0f) + (A - 1.0f) * cs + temp));
    num[1] = a0r * (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs));
    num[2] = a0r * (A * ((A + 1.0f) + (A - 1.0f) * cs - temp));
    den[0] = a0r * (2.0f * ((A - 1.0f) - (A + 1.0f) * cs));
    den[1] = a0r * (((A + 1.0f) - (A - 1.0f) * cs - temp));
}

static void processER(EarlyReflections* er, float inputL, float inputR, float* outputL, float* outputR)
{
    (void)inputR;
    er->bufferL[er->indexL] = inputL;

    float outL = 0.0f;

    for (int i = 0; i < ER_TAPS; i++)
    {
        int idxL = er->indexL - er->delayL[i];
        if (idxL < 0) idxL += MAX_EARLY_DELAY;

        outL += er->bufferL[idxL] * er->gainL[i];
    }

    if (++er->indexL >= MAX_EARLY_DELAY)
        er->indexL = 0;

    *outputL = outL;
    *outputR = outL;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//


static ReverbType reverbTypeOld = REVERB_TYPE_0;

static float LFO_generate(float* phase, float rate, float sampleRate)
{
    float value = xmos_sin_rad(*phase);
    *phase += 2.0f * PI * rate / sampleRate;
    if (*phase > 2 * PI)
        *phase -= 2 * PI;

    return value;
}

static void singlePoleLPF(float coeff, float X, float *Y)
{
    Y[0] = (1 - coeff) * X + coeff * Y[1];
    Y[1] = Y[0];
}


static float allpassFilterVar(float xIn, float *apBuf, float apLen, int maxLen, int *wrPtr, float gain, float *fn)
{
    int d = 0;
    int rdPointer = 0;
    float frac = 0.f;
    float f1 = 0.f, f2 = 0.f;
    float apOut = 0.f;
    float xh;

    d = (int)apLen;
    frac = apLen - d;

    rdPointer = *wrPtr - d;
    if (rdPointer < 0)
    {
        rdPointer += maxLen;
    }
    f1 = apBuf[rdPointer];

    rdPointer -= 1;
    if (rdPointer < 0)
    {
        rdPointer += maxLen;
    }
    f2 = apBuf[rdPointer];

    fn[0] = f2 + (1.f - frac) / (1.f + frac) * (f1 - fn[1]);
    fn[1] = fn[0];

    xh = xIn - gain * fn[0];
    apOut = gain * xh + fn[0];

    apBuf[*wrPtr] = xh;

    (*wrPtr)++;
    if (*wrPtr >= maxLen)
    {
        *wrPtr = 0;
    }

    return apOut;
}

void reverb_T1_init()
{
    memset(modAP1L, 0.f, sizeof(modAP1L));
    memset(modAP1R, 0.f, sizeof(modAP1R));
    memset(modAP2L, 0.f, sizeof(modAP2L));
    memset(modAP2R, 0.f, sizeof(modAP2R));
    memset(modAP3L, 0.f, sizeof(modAP3L));
    memset(modAP3R, 0.f, sizeof(modAP3R));
    memset(modAP4L, 0.f, sizeof(modAP4L));
    memset(modAP4R, 0.f, sizeof(modAP4R));
    memset(modAP5L, 0.f, sizeof(modAP5L));
    memset(modAP5R, 0.f, sizeof(modAP5R));
    memset(modAP6L, 0.f, sizeof(modAP6L));
    memset(modAP6R, 0.f, sizeof(modAP6R));
    memset(yModAP1_L, 0.f, sizeof(yModAP1_L));
    memset(yModAP1_R, 0.f, sizeof(yModAP1_R));
    memset(yModAP2_L, 0.f, sizeof(yModAP2_L));
    memset(yModAP2_R, 0.f, sizeof(yModAP2_R));
    memset(yModAP3_L, 0.f, sizeof(yModAP3_L));
    memset(yModAP3_R, 0.f, sizeof(yModAP3_R));
    memset(yModAP4_L, 0.f, sizeof(yModAP4_L));
    memset(yModAP4_R, 0.f, sizeof(yModAP4_R));
    memset(yModAP5_L, 0.f, sizeof(yModAP5_L));
    memset(yModAP5_R, 0.f, sizeof(yModAP5_R));
    memset(yModAP6_L, 0.f, sizeof(yModAP6_L));
    memset(yModAP6_R, 0.f, sizeof(yModAP6_R));
    memset(yLP_L, 0.f, sizeof(yLP_L));
    memset(yLP_R, 0.f, sizeof(yLP_R));
    memset(delayLtoR_buffer, 0.f, sizeof(delayLtoR_buffer));
    memset(delayRtoL_buffer, 0.f, sizeof(delayRtoL_buffer));
    memset(num_allpassXL, 0.f, sizeof(num_allpassXL));
    memset(den_allpassXL, 0.f, sizeof(den_allpassXL));
    memset(num_allpassXR, 0.f, sizeof(num_allpassXR));
    memset(den_allpassXR, 0.f, sizeof(den_allpassXR));
    memset(num_allpassL2, 0.f, sizeof(num_allpassL2));
    memset(den_allpassL2, 0.f, sizeof(den_allpassL2));
    memset(num_allpassR2, 0.f, sizeof(num_allpassR2));
    memset(den_allpassR2, 0.f, sizeof(den_allpassR2));
    memset(x_allpassXL, 0.f, sizeof(x_allpassXL));
    memset(y_allpassXL, 0.f, sizeof(y_allpassXL));
    memset(x_allpassXR, 0.f, sizeof(x_allpassXR));
    memset(y_allpassXR, 0.f, sizeof(y_allpassXR));
    memset(x_allpassL2, 0.f, sizeof(x_allpassL2));
    memset(y_allpassL2, 0.f, sizeof(y_allpassL2));
    memset(x_allpassR2, 0.f, sizeof(x_allpassR2));
    memset(y_allpassR2, 0.f, sizeof(y_allpassR2));
    memset(x_brightHall_L, 0.f, sizeof(x_brightHall_L));
    memset(y_brightHall_L, 0.f, sizeof(y_brightHall_L));
    memset(x_brightHall_R, 0.f, sizeof(x_brightHall_R));
    memset(y_brightHall_R, 0.f, sizeof(y_brightHall_R));
    memset(x_clearHall_L, 0.f, sizeof(x_clearHall_L));
    memset(y_clearHall_L, 0.f, sizeof(y_clearHall_L));
    memset(x_clearHall_R, 0.f, sizeof(x_clearHall_R));
    memset(y_clearHall_R, 0.f, sizeof(y_clearHall_R));
    memset(x_darkHall_L, 0.f, sizeof(x_darkHall_L));
    memset(y_darkHall_L, 0.f, sizeof(y_darkHall_L));
    memset(x_darkHall_R, 0.f, sizeof(x_darkHall_R));
    memset(y_darkHall_R, 0.f, sizeof(y_darkHall_R));
    memset(x_lowShelf_L, 0.f, sizeof(x_lowShelf_L));
    memset(y_lowShelf_L, 0.f, sizeof(y_lowShelf_L));
    memset(x_lowShelf_R, 0.f, sizeof(x_lowShelf_R));
    memset(y_lowShelf_R, 0.f, sizeof(y_lowShelf_R));
    memset(x_highShelf_L, 0.f, sizeof(x_highShelf_L));
    memset(y_highShelf_L, 0.f, sizeof(y_highShelf_L));
    memset(x_highShelf_R, 0.f, sizeof(x_highShelf_R));
    memset(y_highShelf_R, 0.f, sizeof(y_highShelf_R));


    initER(&erR, SR);

    for (int i = 0; i < 6; i++)
    {
        ldecOld[i] = expf(-tl[i] / fDecOld);
        rdecOld[i] = expf(-tr[i] / fDecOld);
    }

    dampOld = expf(-2.0f * PI * 5000.0f * 2.083e-5f);

    setAPF_RBJ(750.0f, 4.0f, SR, BIQUAD_RBJ_BW, num_allpassXL, den_allpassXL); 
    setAPF_RBJ(150.0f, 4.0f, SR, BIQUAD_RBJ_BW, num_allpassL2, den_allpassL2); 

    for (int i = 0; i < 3; i++)
    {
        num_allpassXR[i] = num_allpassXL[i];
        num_allpassR2[i] = num_allpassL2[i];
    }
    for (int i = 0; i < 2; i++)
    {
        den_allpassXR[i] = den_allpassXL[i];
        den_allpassR2[i] = den_allpassL2[i];
    }
}

void t1_reverb_fProcess(float* dataIn, float* dataOut)
{

    uint8_t param_changed = 0;

    int index= (t1_Reverb)?(t1_Reverb-1):0;
    for(int i=0; i<9; i++)
    {
        if(reverb_T1_knob[i]!=getParam(index,i))
        {
            reverb_T1_knob[i] = getParam(index,i);
            param_changed = 1;
        }
    }  

    float damp = xmos_reverb_damp_lut[reverb_T1_knob[0]];
    float decay = 0.4f + A25[reverb_T1_knob[1]] * 9.9f;
    float diffusion = reverb_T1_knob[2] * d255;
    float preTime = reverb_T1_knob[3] * d255;
    float width = reverb_T1_knob[4] * d255;
    float mix = reverb_T1_knob[5] * d255;
    float inputL, inputR;
    float outL, outR;
    float xMono;
    float preDelayOut;
    float left, right;
    float fDec = 1000 + 2400.f * diffusion;
    float fb = 1.0f - 0.3f/decay;
    float outputL, outputR;
    float wet1 = 0.5f + 0.5f * width;
    float wet2 = 0.5f - 0.5f * width;
    float left1, right1, left2, right2;
    float left3, right3, left4, right4, left5, right5, left6, right6;
    ReverbType reverbType;

    float gainLowShelf = -12.0f + 24.0f * reverb_T1_knob[7] * d255;
    float gainHighShelf = -12.0f + 24.0f * reverb_T1_knob[8] * d255;
    
    if (gainLowShelf != gainLowShelfOld)
    {
        gainLowShelfOld = gainLowShelf;
        setLowShelf_RBJ(220.0f, gainLowShelf, SR, num_lowShelf, den_lowShelf);
    }

    if (gainHighShelf != gainHighShelfOld)
    {
        gainHighShelfOld = gainHighShelf;
        setHighShelf_RBJ(4100.0f, gainHighShelf, SR, num_highShelf, den_highShelf);
    }

    if (reverb_T1_knob[6] < 85)
    {
        reverbType = REVERB_TYPE_0;
    }
    else if (reverb_T1_knob[6] >= 85 && reverb_T1_knob[6] < 170)
    {
        reverbType = REVERB_TYPE_1;
    }
    else
    {
        reverbType = REVERB_TYPE_2;
    }



    if (fDec != fDecOld)
    {
        fDecOld = fDec;
        for (int i = 0; i < 6; i++)
        {
            ldecOld[i] = expf(-tl[i] / fDec);
            rdecOld[i] = expf(-tr[i] / fDec);
        }
    }

    if (reverbType != reverbTypeOld)
    {
        reverbTypeOld = reverbType;
        switch (reverbType)
        {
            case REVERB_TYPE_0:
                tl[0] = 397.0f;
                tl[1] = 457.0f;
                tl[2] = 549.0f;
                tl[3] = 649.0f;
                tl[4] = 773.0f;
                tl[5] = 877.0f;
                tr[0] = 383.0f;
                tr[1] = 429.0f;
                tr[2] = 631.0f;
                tr[3] = 756.0f;
                tr[4] = 803.0f;
                tr[5] = 901.0f;

                for (int i = 0; i < 6; i++)
                {
                    ldecOld[i] = expf(-tl[i] / fDec);
                    rdecOld[i] = expf(-tr[i] / fDec);
                }
                break;
            case REVERB_TYPE_1:
                tl[0] = 697.0f;
                tl[1] = 957.0f;
                tl[2] = 649.0f;
                tl[3] = 1049.0f;
                tl[4] = 473.0f;
                tl[5] = 587.0f;
                tr[0] = 783.0f;
                tr[1] = 929.0f;
                tr[2] = 531.0f;
                tr[3] = 1177.0f;
                tr[4] = 501.0f;
                tr[5] = 681.0f;

                for (int i = 0; i < 6; i++)
                {
                    ldecOld[i] = expf(-tl[i] / fDec);
                    rdecOld[i] = expf(-tr[i] / fDec);
                }
                break;
            case REVERB_TYPE_2:
                tl[0] = 697.0f;
                tl[1] = 957.0f;
                tl[2] = 649.0f;
                tl[3] = 1249.0f;
                tl[4] = 1573.0f;
                tl[5] = 1877.0f;
                tr[0] = 783.0f;
                tr[1] = 929.0f;
                tr[2] = 531.0f;
                tr[3] = 1377.0f;
                tr[4] = 1671.0f;
                tr[5] = 1781.0f;

                for (int i = 0; i < 6; i++)
                {
                    ldecOld[i] = expf(-tl[i] / fDec);
                    rdecOld[i] = expf(-tr[i] / fDec);
                }
                break;
        }
    }

    inputL = dataIn[0];
    inputR = inputL;

    processER(&erR, inputL, inputL, &outputL, &outputR);
    /* Mono bus: skip L<->R cross delays and R early allpasses. */
    x_allpassXL[0] = outputL + inputL;
    secondOrderFilter(num_allpassXL, den_allpassXL, x_allpassXL, y_allpassXL);
    float allpassL2in = y_allpassXL[0];
    x_allpassL2[0] = allpassL2in;
    secondOrderFilter(num_allpassL2, den_allpassL2, x_allpassL2, y_allpassL2);
    x_brightHall_L[0] = y_allpassL2[0];
    secondOrderFilter(num_brightHall, den_brightHall, x_brightHall_L, y_brightHall_L);
    //-------------------------------------------------------------------------//
    if (reverb_T1_knob[3] > 128)
    {
        inputL = y_brightHall_L[0];
    }

    left = inputL;

    float lfo = LFO_generate(&phase, rate, SR);

    /* Mono feedback: reuse left tail instead of right network. */
    left += old_left;
    if (left > 1.0f)
        left = 1.0f;
    else if (left < -1.0f)
        left = -1.0f;

    left1 = allpassFilterVar(left, modAP1L, tl[0] - lfo * 1.72f, MAX_DELAY1_L, &writPoint_AP1_L, -ldecOld[0], yModAP1_L);
    left2 = allpassFilterVar(left1, modAP2L, tl[1] + lfo * 1.8f, MAX_DELAY2_L, &writPoint_AP2_L, -ldecOld[1], yModAP2_L);
    float out_left = left2;
    left3 = allpassFilterVar(left2, modAP3L, tl[2] + lfo * 2.06f, MAX_DELAY3_L, &writPoint_AP3_L, -ldecOld[2], yModAP3_L);
    left4 = allpassFilterVar(left3, modAP4L, tl[3] - lfo * 2.64f, MAX_DELAY4_L, &writPoint_AP4_L, -ldecOld[3], yModAP4_L);
    left5 = allpassFilterVar(left4, modAP5L, tl[4] + lfo * 2.64f, MAX_DELAY5_L, &writPoint_AP5_L, -ldecOld[4], yModAP5_L);
    left6 = allpassFilterVar(left5, modAP6L, tl[5] - lfo * 1.76f, MAX_DELAY6_L, &writPoint_AP6_L, -ldecOld[5], yModAP6_L);

    singlePoleLPF(damp, left6*fb, yLP_L);
    old_left = yLP_L[0];
    if (fabsf(old_left) < 2e-24f)
        old_left = 0.0f;
    old_right = old_left;

    x_lowShelf_L[0] = out_left;
    secondOrderFilter(num_lowShelf, den_lowShelf, x_lowShelf_L, y_lowShelf_L);
    x_highShelf_L[0] = y_lowShelf_L[0];
    secondOrderFilter(num_highShelf, den_highShelf, x_highShelf_L, y_highShelf_L);
    out_left = y_highShelf_L[0];

    outL = inputL * (1 - mix) + out_left * mix;
    outR = outL;

    if (outL > 1.0f)
        outL = 1.0f;
    else if (outL < -1.0f)
        outL = -1.0f;
    outR = outL;

    dataOut[0] = outL;
    dataOut[1] = outR;

}

#endif