/*
 * @Author: llq
 * @Date: 2026-04-28 10:15:27
 * @LastEditors: llq
 * @LastEditTime: 2026-05-15 15:32:05
 * @Description: cabSim
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\global.h
 */
#ifndef GLOBAL_H_
#define GLOBAL_H_

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "xmos_fx_compat.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __XC__
#define UNSAFE unsafe
#else
#define UNSAFE
#endif

#define NUM_CHANS_PROCESS  (2)   // Number of channels to be processed
#define FRAME_LENGTH_ALGO  (8) // Frame_Len：512 samples； the length is 2 to the power of n
#define BUF_LEN_PROCESS    (FRAME_LENGTH_ALGO*NUM_CHANS_PROCESS)
#define MASK_INDEX      (FRAME_LENGTH_ALGO*2-1)

#define CMD_CLEAR_DATA  (4*BUFF_LENGTH_ALGO-1)
#define GET_RDL_FROM_INDEX_WR(index_wr) ((index_wr+BUFF_LENGTH_ALGO)& MASK_INDEX)



#define AUDIO_BUFF    (512)
#define FS            (48000)
#define PI            (3.1415926535f)
#define TWO_PI       (6.283185307f)
#define UNIT          (2147483647.0f)
#define HALF          (1073741823.5f)
#define RCPUNIT          (4.6566128e-10f)
#define RCPHALF          (9.3132257e-10f)
#define TOP_LIMI      (1.25f)
#define POW_2_12      (4096)
#define POW_2_23      (8388608)
#define unit  (3045000.0f)
#define upLim (1.0f)
#define lowLim (-1.0f)
#define UPLIM  (1.0f)
#define LOWLIM (-1.0f)
#define FRAME_LEN (512)
#define HALF_FRAME_LEN (256)
#define d63 (0.2470588f)






extern float d255;// 1/255;
extern float d4095;// 1/4095

extern float numNl1[2];
extern float denNl1[1];
extern float numNl2[3];
extern float denNl2[2];
extern float numNl3[4];
extern float denNl3[3];

//
extern float numInDe[3];
extern float denInDe[2];
extern float numOutDe[3];
extern float denOutDe[2];

extern float gNl15k8X;
extern float denNl15k8X[2];

extern float gNl12k8X;
extern float denNl12k8X[2];

extern float gNl15k16X;
extern float denNl15k16X[2];

extern float gNl12k16X;
extern float denNl12k16X[2];


extern float numHpf0[3];
extern float denHpf0[2];
extern float denHpf1[2];
extern float denHpf2[2];

//各个算法旋钮值更新数组
extern uint8_t reverbPlate_T0_knob[6];
extern uint8_t EQ_T0_knob[13];
extern uint8_t multibandCompressor_T0_knob[19];
extern uint8_t reverb_T0_knob[9];



extern uint8_t reverbPlate_T1_knob[6];
extern uint8_t Compressor_T1_knob[6];
extern uint8_t chorus_T1_knob[4];
extern uint8_t EQ_T1_knob[13];
extern uint8_t delay_T1_knob[4];
extern uint8_t voxAC30_T1_knob[6];// 增加了一个算法开关，方便系统测试
extern uint8_t JCM800_T1_knob[7];// 增加了一个算法开关，方便系统测试
extern uint8_t MesaBoogie_T1_knob[7];// 增加了一个算法开关，方便系统测试
extern uint8_t DiezelCH4_T1_knob[7];// 增加了一个算法开关，方便系统测试
extern uint8_t MarshallSuperL_T1_knob[6];
extern uint8_t Ts808_T1_knob[4];// 增加了一个算法开关，方便系统测试
extern uint8_t Vox212_T1_knob[2];
extern uint8_t Mesa412_T1_knob[2];
extern uint8_t Marshall212_T1_knob[2];
extern uint8_t Marshall412_T1_knob[2];
extern uint8_t FenderTwinReverb_T1_knob[6];// 增加了一个算法开关，方便系统测试
extern uint8_t FenderTwinReverb212_T1_knob[2];
extern uint8_t Diezel412_T1_knob[2];
extern uint8_t ISP_NG_T1_knob[2];// 增加了一个算法开关，方便系统测试
extern uint8_t MxrDisPlus_T1_knob[3];// 增加了一个算法开关，方便系统测试
extern uint8_t Vintage_ProcoRat_T1_knob[4];// 增加了一个算法开关，方便系统测试
extern uint8_t Boss_BD2_T1_knob[4];// 增加了一个算法开关，方便系统测试
extern uint8_t GuitarEQ1_T1_knob[7];// 增加了一个算法开关，方便系统测试
extern uint8_t BigMuff_T1_knob[4];// 增加了一个算法开关，方便系统测试
extern uint8_t Fuzzface_T1_knob[3];// 增加了一个算法开关，方便系统测试
extern uint8_t Tremolo_T1_knob[3];
extern uint8_t AutoPan_T1_knob[3];
extern uint8_t reverb_T1_knob[9];


extern float A25[256];
extern float C25[256];


float delayLine(float xIn, float* delayBuf, int delayLen, int* wrPtr);



float delayLineVar(float xIn, float* delayBuf, float knobSize, int minLen, int maxLen, int* wrPtr);



uint8_t getParam(uint8_t algoId, uint8_t paramNo);




static inline void firstOrderFilter(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1]  - Y[1] * den[0];
	X[1] = X[0];
	Y[1] = Y[0];
}

static inline void firstOrderFilterVA(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1]  - Y[1] * den[1];
	Y[0] = Y[0]/den[0];
	X[1] = X[0];
	Y[1] = Y[0];
}

//函数名：secondOrderFilter
//功能描述： 适用于所有二阶滤波器
static inline void secondOrderFilter(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1] + X[2] * num[2] - Y[1] * den[0] - Y[2] * den[1];
	X[2] = X[1];
	X[1] = X[0];
	Y[2] = Y[1];
	Y[1] = Y[0];
}


//函数名：secondOrderFilterVA
//功能描述： 适用于所有二阶VA滤波器
static inline void secondOrderFilterVA(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1] + X[2] * num[2] - Y[1] * den[1] - Y[2] * den[2];
	Y[0] = Y[0]/den[0];
	X[2] = X[1];
	X[1] = X[0];
	Y[2] = Y[1];
	Y[1] = Y[0];
}

//函数名：butterSecondLP
//功能描述： 适用于二阶butterWorth LPF
//分子多项式2次项和常数项系数为1，1次项系数为2
//此时只需要输入分子多项式的增益和分母多项式的系数
static inline void butterSecondLP(float G,float *den, float *X, float *Y)
{
	Y[0] = (X[0]  + X[1] +X[1] + X[2])*G - Y[1] * den[0] - Y[2] * den[1];
	X[2] = X[1];
	X[1] = X[0];
	Y[2] = Y[1];
	Y[1] = Y[0];
}

//函数名：butterSecondHP
//功能描述： 适用于二阶butterWorth HPF
//分子多项式2次项和常数项系数为1，1次项系数为-2，
//此时只需要输入分子多项式的增益和分母多项式的系数
static inline void butterSecondHP(float G,float *den, float *X, float *Y)
{
	Y[0] = (X[0]  - X[1] - X[1] + X[2])*G - Y[1] * den[0] - Y[2] * den[1];
	X[2] = X[1];
	X[1] = X[0];
	Y[2] = Y[1];
	Y[1] = Y[0];
}
//函数名：thirdOrderFilterVA
//功能描述： 适用于建模的三阶滤波器
static inline void thirdOrderFilterVA(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1] + X[2] * num[2] + X[3] * num[3]
	                                   - Y[1] * den[1] - Y[2] * den[2] - Y[3] * den[3];
	Y[0] = Y[0]/den[0];
	X[3] = X[2];
	X[2] = X[1];
	X[1] = X[0];
	Y[3] = Y[2];
	Y[2] = Y[1];
	Y[1] = Y[0];
}

//函数名：thirdOrderFilter
//功能描述： 适用于所有三阶滤波器
static inline void thirdOrderFilter(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1] + X[2] * num[2] + X[3] * num[3] - Y[1] * den[0] - Y[2] * den[1] - Y[3] * den[2];
	X[3] = X[2];
	X[2] = X[1];
	X[1] = X[0];
	Y[3] = Y[2];
	Y[2] = Y[1];
	Y[1] = Y[0];
}
//函数名：thirdOrderFilterF1
//功能描述： 适用于分子多项式3次项和常数项系数相等，2次项和1次项系数相等的情况
static inline void threeOrderFilterF1(float *num, float *den, float *X, float *Y)
{
	Y[0] = (X[0]+X[3]) * num[0] + (X[1]+X[2])  * num[1] - Y[1] * den[0] - Y[2] * den[1] - Y[3] * den[2];
	X[3] = X[2];
	X[2] = X[1];
	X[1] = X[0];
	Y[3] = Y[2];
	Y[2] = Y[1];
	Y[1] = Y[0];
}

static inline void fourthOrderFilterVA(float *num, float *den, float *X, float *Y)
{
	Y[0] = X[0] * num[0] + X[1] * num[1] + X[2] * num[2] + X[3] * num[3]+ X[4] * num[4]
	                                   - Y[1] * den[1]  - Y[2] * den[2]   - Y[3] * den[3]  - Y[4] * den[4];
	Y[0] = Y[0]/den[0];
	X[4] = X[3];
	X[3] = X[2];
	X[2] = X[1];
	X[1] = X[0];
	Y[4] = Y[3];
	Y[3] = Y[2];
	Y[2] = Y[1];
	Y[1] = Y[0];
}


//#define DATA_TEST
void algo_init(void);

/*
int  fAlgo_handler( int *  UNSAFE  buf_in,   int *  UNSAFE  buf_out, unsigned offset)
Function of algorithm processing :
Processing needs to be completed within the time corresponding to the frame length;
If the processing time is not enough, each channel can be processed separately by one thread.
or split it into multiple threads for segmented processing

Para 1: buf_in，  address of buf  input  
Para 2: buf_out， address of buf  output
Para 3: n_frame frame Number

return val: reserve
*/

//int fAlgo_handler( int32_t *  UNSAFE  buf_in,   int32_t *  UNSAFE  buf_out, unsigned n_frame);
void fAlgo_handler( float *  UNSAFE  buf_in,   float *  UNSAFE  buf_out, unsigned n_frame);




#ifdef __cplusplus
}
#endif
#endif /* GLOBAL_H_ */
