/*
 * @Author: llq
 * @Date: 2025-12-25 14:38:06
 * @LastEditors: llq
 * @LastEditTime: 2026-06-05 11:35:39
 * @Description: Vox AC30
 * @FilePath: \AerobandSDK_A_20260428_InAerobandBoard\src\algorithm\VoxAC30.c
 */

 #if 1

#include "xcore_math.h"
#include "global.h"
#include <stdlib.h>
#include <string.h>
// #include "algo_buffersT0.h"
#include "VoxAC30.h"

#include "xcore_math.h"



static float xInDe[6];
static float yInDe[6];

static float xPf2[3];
static float yPf2[3];
static float xPf3[2];
static float yPf3[2];

static float xEQ[4];
static float yEQ[4];
static float xCut[2];
static float yCut[2];
static float xCab[14][3];
static float yCab[13][3];

static const float numPf1[3] = {6.595620078f, -9.672770042f, 3.080243356f};
static const float denPf1[2] = {-1.46059163f, 0.461279931f};

static float xPf1[3];
static float yPf1[3];


static float numGain[3];
static float denGain[3];
static float xGain[3];
static float yGain[3];

static const float numPf2[3] = {0.884712696747817f, -0.757295225050815f, -0.127417471831573f};
static const float denPf2[2] = {-0.732315340436658f, -0.217724890630749f};

static const float numPf3[2] = {0.9995288679f, -0.9995288679f};
static const float denPf3[1] = {-0.9990577358f};
static float numEQ[4];
static float denEQ[4];

static float numCut[2];
static float denCut[2];

static float xNl[3];
static float yNl[3];
static float xPrev = 0.0f;

static float Ab25[256] =  {
		0.050000f,0.050000f,0.050000f,0.052390f,0.054781f,0.057171f,0.059562f,0.061952f,
		0.064343f,0.066733f,0.069124f,0.071514f,0.073904f,0.076295f,0.078685f,0.081076f,
		0.083466f,0.085857f,0.088247f,0.090637f,0.093028f,0.095418f,0.097809f,0.100199f,
		0.102590f,0.104980f,0.107371f,0.109761f,0.112151f,0.114542f,0.116932f,0.119323f,
		0.121713f,0.124104f,0.126494f,0.128885f,0.131275f,0.133666f,0.136056f,0.138447f,
		0.140837f,0.143228f,0.145618f,0.148009f,0.150400f,0.152790f,0.155181f,0.157572f,
		0.159962f,0.162353f,0.164744f,0.167135f,0.169526f,0.171917f,0.174308f,0.176699f,
		0.179090f,0.181481f,0.183873f,0.186264f,0.188656f,0.191048f,0.193440f,0.195832f,
		0.198224f,0.200616f,0.203009f,0.205402f,0.207795f,0.210188f,0.212582f,0.214976f,
		0.217370f,0.219765f,0.222159f,0.224555f,0.226951f,0.229347f,0.231743f,0.234140f,
		0.236538f,0.238936f,0.241335f,0.243735f,0.246135f,0.248536f,0.250938f,0.253340f,
		0.255744f,0.258148f,0.260554f,0.262960f,0.265368f,0.267777f,0.270187f,0.272598f,
		0.275011f,0.277425f,0.279841f,0.282259f,0.284678f,0.287099f,0.289522f,0.291947f,
		0.294374f,0.296803f,0.299235f,0.301669f,0.304105f,0.306544f,0.308986f,0.311432f,
		0.313880f,0.316331f,0.318786f,0.321244f,0.323706f,0.326172f,0.328642f,0.331116f,
		0.333594f,0.336077f,0.338565f,0.341058f,0.343556f,0.346060f,0.348569f,0.351084f,
		0.353605f,0.356133f,0.358667f,0.361208f,0.363756f,0.366312f,0.368875f,0.371447f,
		0.374027f,0.376615f,0.379213f,0.381820f,0.384436f,0.387063f,0.389700f,0.392348f,
		0.395007f,0.397677f,0.400360f,0.403055f,0.405763f,0.408484f,0.411219f,0.413968f,
		0.416732f,0.419512f,0.422307f,0.425118f,0.427946f,0.430792f,0.433655f,0.436537f,
		0.439438f,0.442359f,0.445301f,0.448263f,0.451247f,0.454254f,0.457283f,0.460337f,
		0.463415f,0.466518f,0.469647f,0.472804f,0.475988f,0.479200f,0.482442f,0.485714f,
		0.489017f,0.492352f,0.495720f,0.499122f,0.502559f,0.506032f,0.509541f,0.513089f,
		0.516676f,0.520302f,0.523970f,0.527680f,0.531434f,0.535232f,0.539077f,0.542968f,
		0.546908f,0.550898f,0.554938f,0.559031f,0.563178f,0.567380f,0.571639f,0.575955f,
		0.580331f,0.584769f,0.589268f,0.593833f,0.598463f,0.603160f,0.607927f,0.612765f,
		0.617675f,0.622660f,0.627721f,0.632860f,0.638080f,0.643381f,0.648766f,0.654237f,
		0.659797f,0.665446f,0.671188f,0.677024f,0.682956f,0.688988f,0.695120f,0.701357f,
		0.707698f,0.714149f,0.720709f,0.727384f,0.734173f,0.741082f,0.748111f,0.755264f,
		0.762543f,0.769951f,0.777492f,0.785168f,0.792981f,0.800935f,0.809033f,0.817278f,
		0.825674f,0.834222f,0.842927f,0.851792f,0.860820f,0.870015f,0.879380f,0.888918f,
		0.898634f,0.908530f,0.918611f,0.928881f,0.9393f,0.950000f,0.950000f,0.950000f};

static float At25[256] = {
		0.000000f,0.000000f,0.000000f,0.002789f,0.005578f,0.008367f,0.011155f,0.013944f,
		0.016733f,0.019522f,0.022311f,0.025100f,0.027888f,0.030677f,0.033466f,0.036255f,
		0.039044f,0.041833f,0.044622f,0.047410f,0.050199f,0.052988f,0.055777f,0.058566f,
		0.061355f,0.064143f,0.066932f,0.069721f,0.072510f,0.075299f,0.078088f,0.080877f,
		0.083665f,0.086454f,0.089243f,0.092032f,0.094821f,0.097610f,0.100399f,0.103188f,
		0.105977f,0.108765f,0.111554f,0.114343f,0.117132f,0.119921f,0.122710f,0.125499f,
		0.128289f,0.131078f,0.133867f,0.136656f,0.139445f,0.142235f,0.145024f,0.147813f,
		0.150603f,0.153393f,0.156182f,0.158972f,0.161762f,0.164552f,0.167342f,0.170132f,
		0.172922f,0.175713f,0.178504f,0.181294f,0.184085f,0.186877f,0.189668f,0.192460f,
		0.195252f,0.198044f,0.200837f,0.203630f,0.206423f,0.209217f,0.212011f,0.214805f,
		0.217600f,0.220395f,0.223191f,0.225988f,0.228785f,0.231582f,0.234380f,0.237179f,
		0.239979f,0.242780f,0.245581f,0.248383f,0.251187f,0.253991f,0.256796f,0.259602f,
		0.262410f,0.265218f,0.268028f,0.270840f,0.273653f,0.276467f,0.279283f,0.282100f,
		0.284920f,0.287741f,0.290564f,0.293389f,0.296216f,0.299046f,0.301878f,0.304712f,
		0.307549f,0.310389f,0.313231f,0.316077f,0.318925f,0.321777f,0.324632f,0.327490f,
		0.330352f,0.333219f,0.336089f,0.338963f,0.341841f,0.344724f,0.347612f,0.350505f,
		0.353403f,0.356306f,0.359214f,0.362129f,0.365049f,0.367976f,0.370909f,0.373848f,
		0.376795f,0.379749f,0.382711f,0.385680f,0.388657f,0.391643f,0.394637f,0.397640f,
		0.400653f,0.403675f,0.406708f,0.409750f,0.412804f,0.415868f,0.418944f,0.422032f,
		0.425132f,0.428245f,0.431371f,0.434511f,0.437664f,0.440832f,0.444015f,0.447214f,
		0.450428f,0.453659f,0.456907f,0.460173f,0.463456f,0.466759f,0.470080f,0.473421f,
		0.476783f,0.480166f,0.483571f,0.486998f,0.490448f,0.493922f,0.497420f,0.500944f,
		0.504493f,0.508069f,0.511673f,0.515304f,0.518965f,0.522656f,0.526378f,0.530131f,
		0.533916f,0.537735f,0.541589f,0.545477f,0.549402f,0.553364f,0.557365f,0.561404f,
		0.565485f,0.569606f,0.573770f,0.577978f,0.582230f,0.586528f,0.590874f,0.595268f,
		0.599712f,0.604206f,0.608753f,0.613353f,0.618008f,0.622720f,0.627489f,0.632317f,
		0.637206f,0.642157f,0.647171f,0.652251f,0.657397f,0.662612f,0.667896f,0.673252f,
		0.678682f,0.684187f,0.689768f,0.695428f,0.701169f,0.706992f,0.712899f,0.718893f,
		0.724974f,0.731146f,0.737411f,0.743769f,0.750224f,0.756778f,0.763432f,0.770190f,
		0.777053f,0.784023f,0.791104f,0.798297f,0.805605f,0.813030f,0.820576f,0.828243f,
		0.836036f,0.843957f,0.852008f,0.860192f,0.868512f,0.876971f,0.885572f,0.894318f,
		0.903211f,0.912255f,0.921452f,0.930807f,0.940322f,0.950000f,0.968785f,0.984229f};

static void preproc(float *xin, float *yin)
{
	secondOrderFilter(numHpf0,denHpf0,&xin[0],&yin[0]);
	xin[3] = yin[0];
	secondOrderFilter(numHpf0,denHpf0,&xin[3],&yin[3]);
}

void gainParaCalc_voxAC30(float Rg, float *num, float *den)
{
	float Rg2 = Rg * Rg;
	num[0] = 1.0f * Rg2 - 1.377929f * Rg;
	num[1] = -2.0f * Rg2 + 2.0f * Rg;
	num[2] = 1.0f * Rg2 - 0.6220711f * Rg;
	den[0] = 1.0f * Rg2 - 0.9517538f * Rg - 0.4444089f;
	den[1] = -2.0f * Rg2 + 2.0f * Rg - 0.03646731f;
	den[2] = 1.0f * Rg2 - 1.048246f * Rg + 0.4079415f;
}

void ParaCalc1_voxAC30(float Rb, float Rt, float *num,float *den){
	float Rt2 = Rt*Rt;
	float Rbt = Rb*Rt;
	float Rbt2 = Rb*Rt2;
	num[0] =  0.9910303936f*Rb*Rt2 - 0.1362606892f*Rt - Rbt - 0.000950104497f*Rb + 0.1363173888f*Rt2 - 0.0001228061285f;
	num[1] =  0.0009456165211f*Rb + 0.352347326f*Rt + 3.001885839f*Rbt - 2.973091181f*Rbt2 - 0.352384474f*Rt2 + 0.00008365853681f;
	num[2] =  0.000950104497f*Rb - 0.299792684f*Rt - 3.00376279f*Rbt + 2.973091181f*Rbt2 + 0.2997359844f*Rt2 + 0.0001228061285f;
	num[3] =  0.08370604714f*Rt - 0.0009456165211f*Rb + 1.001876952f*Rbt - 0.9910303936f*Rbt2 - 0.08366889914f*Rt2 - 0.00008365853681f;

	den[0] = 1.333812448f*Rt2 - 1.344594023f*Rt - 0.001270849062f;
	den[1] = - 3.53194032f*Rt2 + 3.562959381f*Rt + 0.000830401626f;
	den[2] = 3.0663625f*Rt2 - 3.095220289f*Rt + 0.001268979072f;
	den[3] = - 0.8682346274f*Rt2 + 0.8768586337f*Rt - 0.000832271616f;
}


float nonlinear_voxAC30(float xcurr, float xprev)
{
	float y;
	float xnl[8], ynl[8];
	float diff;
	int i, N = 8;
	float p1 = 0.8745;
	float q1 = 0.2931;
	diff = (xcurr - xprev) * 0.125f;
    for (i = 0;i<N;i++){
        xnl[i] = xprev + diff * (float)(i+1);
    }
	for (i = 0; i < N; i++)
	{
		ynl[i] = (p1 * xnl[i]) / (fabsf(xnl[i]) + q1);
		xNl[0] = ynl[i];
		butterSecondLP(gNl15k8X,denNl15k8X, xNl, yNl);
	}
	return yNl[0];
}


void VoxAC30_init()
{

    memset(xInDe, 0.0f, sizeof(xInDe));
    memset(yInDe, 0.0f, sizeof(yInDe));
    memset(numCut,0.0f,sizeof(numCut));
    memset(denCut,0.0f,sizeof(denCut));
    memset(xPf2,0.0f,sizeof(xPf2));
    memset(yPf2,0.0f,sizeof(yPf2));
    memset(xPf3,0.0f,sizeof(xPf3));
    memset(yPf3,0.0f,sizeof(yPf3));
    memset(xCut,0.0f,sizeof(xCut));
    memset(yCut,0.0f,sizeof(yCut));
    memset(xEQ,0.0f,sizeof(xEQ));
    memset(yEQ,0.0f,sizeof(yEQ));

    memset(xNl,0.0f,sizeof(xNl));
    memset(yNl,0.0f,sizeof(yNl));
    memset(xCab,0.0f,sizeof(xCab));
    memset(yCab,0.0f,sizeof(yCab));

	memset(xPf1,0.0f,sizeof(xPf1));
	memset(yPf1,0.0f,sizeof(yPf1));

	memset(numGain,0.0f,sizeof(numGain));
	memset(denGain,0.0f,sizeof(denGain));
	memset(xGain,0.0f,sizeof(xGain));
	memset(yGain,0.0f,sizeof(yGain));
	
}

void VoxAC30_process(float *xin, float *xOut)
{
    float xnl,ynl;
    float xoutcab, xoutamp, youtcab, youtamp;
    float inputL, inputR, input;

    float gain = voxAC30_T1_knob[0]*d255;
    float Rt = Ab25[voxAC30_T1_knob[1]];
    float Rb = At25[voxAC30_T1_knob[2]];
    float Rc = A25[voxAC30_T1_knob[3]];
    float level = voxAC30_T1_knob[4]*d255;
	
    float G = 70.7946f * gain * gain;
    float masterVol = 10.0f * level * level;

    ParaCalc1_voxAC30(Rb, Rt, numEQ, denEQ);
    
	int i= 0;
	inputL = xin[0+i];
	inputR = xin[0+i];

	xInDe[0] = inputL;
	preproc(xInDe, yInDe);
	xPf2[0] = yInDe[3] * G * 31.6228f;
	secondOrderFilter(numPf2,denPf2, xPf2, yPf2);

	xPf3[0] = yPf2[0];
	firstOrderFilter(numPf3,denPf3, xPf3, yPf3);

	xnl = yPf3[0];
	ynl = nonlinear_voxAC30(xnl, xPrev);
	xPrev = xnl;

	xEQ[0] = ynl;
	thirdOrderFilterVA(numEQ,denEQ,xEQ,yEQ);

	numCut[0] = -0.04817178444f;
	numCut[1] = -0.04817178444f;
	denCut[0] = Rc + 0.08590763349f;
	denCut[1] = 0.01043593538 - Rc;

	xCut[0] = yEQ[0];
	firstOrderFilterVA(numCut, denCut, xCut, yCut);

	xoutamp = -yCut[0] * masterVol * 1.25f;

	xOut[0+i] = xoutamp;
	xOut[1+i] = xoutamp;
}

#endif