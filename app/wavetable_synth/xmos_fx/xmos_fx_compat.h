#ifndef __BES_XMOS_FX_COMPAT_H
#define __BES_XMOS_FX_COMPAT_H

#include <stdint.h>

typedef enum
{
  t0_MultibandCompressor = 1,
  t0_EQ = 2,
  t0_Reverb = 3,
} t0_algo_id;

typedef enum
{
  t1_EQ = 1,
  t1_MultibandCompressor,
  t1_ChorusSine,
  t1_PureDelay,
  t1_VoxAC30,
  t1_JCM800,
  t1_FenderTwinReverb,
  t1_Mesa,
  t1_DiezelCH4,
  t1_Ts808,
  t1_Vox212,
  t1_Mesa412,
  t1_Marshall212,
  t1_Marshall412,
  t1_FenderTwinReverb212,
  t1_Diezel412,
  t1_MxrDisPlus,
  t1_Vintage_ProcoRat,
  t1_Compressor,
  t1_Reverb,
  t1_Tremolo,
  t1_AutoPan,
  t1_algo_max,
} t1_algo_id;

#define ParaLength 32
typedef struct
{
  uint8_t para_Len;
  uint8_t tile_id;
  uint8_t slot_id;
  uint8_t algo_id;
  uint8_t algoParas[ParaLength];
} sAlgosParams_t;

extern sAlgosParams_t sT0_algo_paras[3];
uint8_t getParam(uint8_t algoId, uint8_t paramNo);

#endif
