//---------------------------------------------------------------------------
#include <windows.h>
#include <math.h>
#include "common.h"
#include "tp_stub.h"
#include "dim.h"


/**
 * ���O�o�͗p
 */
#if 0
#include <stdio.h>
static void log(const tjs_char *format, ...)
{
	va_list args;
	va_start(args, format);
	tjs_char msg[1024];
	_vsnwprintf_s(msg, 1024, format, args);
	TVPAddLog(msg);
	va_end(args);
}
#endif


//---------------------------------------------------------------------------
/*
	Dim �g�����W�V����
	2011/12/09	0.2.1.0	���������[�X
*/
//---------------------------------------------------------------------------

#define USE_SSE2 // SSE2���g���Ȃ��`

// �z���g��tTVPUniversalTransHandler���p�������������񂾂��ǁAHeader�Œ�`
// ����Ă��Ȃ�����f�O�B
class tTVPDimTransHandler : public iTVPDivisibleTransHandler
{
	//	boxDim �g�����W�V�����n���h���N���X�̎���

	tjs_int RefCount; // �Q�ƃJ�E���^
	/*
	 * iTVPDivisibleTransHandler �� �Q�ƃJ�E���^�ɂ��Ǘ����s��
	 */

protected:
	bool          First;		// ��ԍŏ��̌Ăяo�����ǂ���
	tjs_int64     StartTick;	// �g�����W�V�������J�n���� tick count
	tjs_int64     Time;			// �g�����W�V�����ɗv���鎞��
	tTVPLayerType LayerType;	// ���C���^�C�v
	tjs_int       Width;		// ��������摜�̕�
	tjs_int       Height;		// ��������摜�̍���

	tjs_int64     CurTime;		// ���݂� tick count(0�`Time)
	tjs_int       Phase;		// ���݂̃t�F�[�Y(0�`Vague+255)

	iTVPScanLineProvider *Ruleimg;	// �O���[�X�P�[���Ńu���[���ꂽ���[���摜
	double        CurRatio;		// ���݂̕ω���
	double        Accel;		// dim��������x�Bdef=1.0�A<-1�ōŏ��x���Ō㑁���A>1�ōŏ������Ō�x��
	tjs_uint32    BlendTable[256];	// Universal Transition�̃u�����h�e�[�u��
	tjs_int       Vague;

	// ���݂�Ratio(0�`1.0)�����߂�
	inline double setCurrentRatio()
	{
		double ratio = (double)CurTime/Time;
		// ratio = 0�`1.0
		ratio = (ratio < 0.0) ? 0.0 : (ratio > 1.0) ? 1.0 : ratio;
	
		if(Accel > 1.0)				// ����(�ŏ��x�����X�ɑ���)
			ratio = pow(ratio, Accel);
		else if(Accel < -1.0) {	// �㌷(�ŏ��������X�ɒx��)
			ratio = 1.0 - ratio;
			ratio = pow(ratio, -Accel);
			ratio = 1.0 - ratio;
		}
		CurRatio = ratio;
		return ratio;
	}

public:
	// �R���X�g���N�^
	tTVPDimTransHandler(iTVPScanLineProvider *ruleimg, tjs_uint64 time, tjs_int vague, tTVPLayerType layertype,
		tjs_int width, tjs_int height, double accel)
	{
		RefCount    = 1;
		First       = true;

		LayerType   = layertype;
		Width       = width;
		Height      = height;
		Time        = time;

		Vague       = vague;
		Accel       = accel;
		Ruleimg     = ruleimg;
		Ruleimg->AddRef();
	}

	~tTVPDimTransHandler()
	{
		Ruleimg->Release();
	}

	tjs_error TJS_INTF_METHOD AddRef()
	{
		// iTVPBaseTransHandler �� AddRef
		// �Q�ƃJ�E���^���C���N�������g
		RefCount ++;
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD Release()
	{
		// iTVPBaseTransHandler �� Release
		// �Q�ƃJ�E���^���f�N�������g���A0 �ɂȂ�Ȃ�� delete this
		if(RefCount == 1) {
			delete this;
		} else {
			RefCount--;
		}
		return TJS_S_OK;
	}


	tjs_error TJS_INTF_METHOD SetOption(
			/*in*/iTVPSimpleOptionProvider *options // option provider
		)
	{
		// iTVPBaseTransHandler �� SetOption
		// �Ƃ��ɂ�邱�ƂȂ�
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick);

	tjs_error TJS_INTF_METHOD EndProcess();

	tjs_error TJS_INTF_METHOD Process(
			tTVPDivisibleData *data);

	void Blend(tTVPDivisibleData *data);

	tjs_error TJS_INTF_METHOD MakeFinalImage(
			iTVPScanLineProvider ** dest,
			iTVPScanLineProvider * src1,
			iTVPScanLineProvider * src2)
	{
		*dest = src2; // ��ɍŏI�摜�� src2
		return TJS_S_OK;
	}
};


//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTVPDimTransHandler::StartProcess(tjs_uint64 tick)
{
	// �g�����W�V�����̉�ʍX�V��񂲂ƂɌĂ΂��

	// �g�����W�V�����̉�ʍX�V���ɂ��A�܂��ŏ��� StartProcess ���Ă΂��
	// ���̂��� Process ��������Ă΂�� ( �̈�𕪊��������Ă���ꍇ )
	// �Ō�� EndProcess ���Ă΂��

	if (First) {	// �ŏ��̎��s
		StartTick = tick;
		First = false;
	}

	// �摜���Z�ɕK�v�Ȋe�p�����[�^���v�Z
	CurTime = ((tjs_int64)tick - StartTick);
	if (CurTime > Time) CurTime = Time;

	// CurRatio�v�Z
	setCurrentRatio();
	Phase = (tjs_int)(CurRatio*(255+Vague));

	if (TVPIsTypeUsingAlpha(LayerType))
		TVPInitUnivTransBlendTable_d(BlendTable, Phase, Vague);
	else if(TVPIsTypeUsingAddAlpha(LayerType))
		TVPInitUnivTransBlendTable_a(BlendTable, Phase, Vague);
	else
		TVPInitUnivTransBlendTable(BlendTable, Phase, Vague);

	return TJS_S_TRUE;
}

//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTVPDimTransHandler::EndProcess()
{
	// �g�����W�V�����̉�ʍX�V��񕪂��I��邲�ƂɌĂ΂��

	if(CurRatio >= 1.0) return TJS_S_FALSE; // �g�����W�V�����I��

	return TJS_S_TRUE;
}


//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTVPDimTransHandler::Process(
			tTVPDivisibleData *data)
{
	// �g�����W�V�����̊e�̈悲�ƂɌĂ΂��
	// �g���g���͉�ʂ��X�V����Ƃ��ɂ������̗̈�ɕ������Ȃ��珈�����s���̂�
	// ���̃��\�b�h�͒ʏ�A��ʍX�V���ɂ�������Ă΂��

	// data �ɂ͗̈��摜�Ɋւ����񂪓����Ă���

	if (CurRatio == 0.0) {
		data->Dest     = data->Src1;
		data->DestLeft = data->Src1Left;
		data->DestTop  = data->Src1Top;
	} else if (CurRatio >= 1.0) {
		data->Dest     = data->Src2;
		data->DestLeft = data->Src2Left;
		data->DestTop  = data->Src2Top;
	} else {
		Blend(data);
	}

	return TJS_S_OK;
}
//---------------------------------------------------------------------------

// Univeral Transtion�Ɠ���Blend()�֐������̂܂܎g��
void tTVPDimTransHandler::Blend(tTVPDivisibleData *data)
{
	// blend the image according with the rule graphic
	tjs_uint8 *dest;
	const tjs_uint8 *src1;
	const tjs_uint8 *src2;
	const tjs_uint8 *rule;

	data->Dest->GetScanLineForWrite(data->DestTop, (void**)&dest);
	data->Src1->GetScanLine(data->Src1Top, (const void**)&src1);
	data->Src2->GetScanLine(data->Src2Top, (const void**)&src2);
	Ruleimg->GetScanLine(data->Top, (const void**)&rule);

	/* ���[���摜�\���p
	for (int y = 0; y < data->Height; y++) {
		data->Dest->GetScanLineForWrite(data->DestTop+y, (void**)&dest);
		Ruleimg->GetScanLine(data->Top+y, (const void**)&rule);
		for (int x = 0; x < data->Width; x++) {
			*dest++ = 0xff;
			*dest++ = *rule;
			*dest++ = *rule;
			*dest++ = *rule++;
		}
	}
	return;
	*/

	tjs_int destpitch;
	tjs_int src1pitch;
	tjs_int src2pitch;
	tjs_int rulepitch;

	data->Dest->GetPitchBytes(&destpitch);
	data->Src1->GetPitchBytes(&src1pitch);
	data->Src2->GetPitchBytes(&src2pitch);
	Ruleimg->GetPitchBytes(&rulepitch);

	dest += data->DestLeft * sizeof(tjs_uint32);
	src1 += data->Src1Left * sizeof(tjs_uint32);
	src2 += data->Src2Left * sizeof(tjs_uint32);
	rule += data->Left * sizeof(tjs_uint8);

	tjs_int h = data->Height;
	if(Vague >= 512) {
		if(TVPIsTypeUsingAlpha(LayerType)) {
			while(h--) {
				TVPUnivTransBlend_d((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		} else if(TVPIsTypeUsingAddAlpha(LayerType)) {
			while(h--) {
				TVPUnivTransBlend_a((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		} else {
			while(h--) {
				TVPUnivTransBlend((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		}
	} else {
		tjs_int src1lv = Phase;
		tjs_int src2lv = Phase - Vague;

		if(TVPIsTypeUsingAlpha(LayerType)) {
			while(h--) {
				TVPUnivTransBlend_switch_d((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width,
						src1lv, src2lv);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		} else if(TVPIsTypeUsingAddAlpha(LayerType)) {
			while(h--) {
				TVPUnivTransBlend_switch_a((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width,
						src1lv, src2lv);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		} else {
			while(h--) {
				TVPUnivTransBlend_switch((tjs_uint32*)dest, (const tjs_uint32*)src1,
					(const tjs_uint32*)src2, rule, BlendTable, data->Width,
						src1lv, src2lv);
				dest += destpitch, src1 += src1pitch, src2 += src2pitch;
				rule += rulepitch;
			}
		}
	}
}
 

//---------------------------------------------------------------------------
class tTVPDimTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount; // �Q�ƃJ�E���^

	inline void addALineToIntegralImage8(DWORD *dst, DWORD *dstprev, BYTE *src, tjs_int srcwidth, tjs_int xblur);
	inline void drawALineFromIntegralImageToImage8(BYTE *dst, tjs_int dstwidth, DWORD *src1, DWORD *src2, tjs_int xblur, tjs_int sq);
	void DoBoxBlur(iTVPScanLineProvider *img, tjs_int xblur, tjs_int yblur);
	void NegateGrayImage(iTVPScanLineProvider *img);
public:
	tTVPDimTransHandlerProvider() { RefCount = 1; }
	~tTVPDimTransHandlerProvider() {; }

	tjs_error TJS_INTF_METHOD AddRef()
	{
		// iTVPBaseTransHandler �� AddRef
		// �Q�ƃJ�E���^���C���N�������g
		RefCount ++;
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD Release()
	{
		// iTVPBaseTransHandler �� Release
		// �Q�ƃJ�E���^���f�N�������g���A0 �ɂȂ�Ȃ�� delete this
		if(RefCount == 1)
			delete this;
		else
			RefCount--;
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD GetName(
			/*out*/const tjs_char ** name)
	{
		// ���̃g�����W�V�����̖��O��Ԃ�
		if(name) *name = TJS_W("dim");
		return TJS_S_OK;
	}


	tjs_error TJS_INTF_METHOD StartTransition(
			/*in*/iTVPSimpleOptionProvider *options, // option provider
			/*in*/iTVPSimpleImageProvider *imagepro, // image provider
			/*in*/tTVPLayerType layertype, // destination layer type
			/*in*/tjs_uint src1w, tjs_uint src1h, // source 1 size
			/*in*/tjs_uint src2w, tjs_uint src2h, // source 2 size
			/*out*/tTVPTransType *type, // transition type
			/*out*/tTVPTransUpdateType * updatetype, // update typwe
			/*out*/iTVPBaseTransHandler ** handler // transition handler
			)
	{
		if(type) *type = ttExchange; // transition type : exchange
		if(updatetype) *updatetype = tutDivisible;
			// update type : divisible
		if(!handler) return TJS_E_FAIL;
		if(!options) return TJS_E_FAIL;

		if(src1w != src2w || src1h != src2h)
			return TJS_E_FAIL; // src1 �� src2 �̃T�C�Y����v���Ă��Ȃ��Ƒʖ�

		// �I�v�V�����𓾂�
		tTJSVariant tmp;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
			return TJS_E_FAIL; // time �������w�肳��Ă��Ȃ�
		if(tmp.Type() == tvtVoid)
			return TJS_E_FAIL;
		tjs_int64 time = (tjs_int64)tmp;
		if(time < 2) time = 2; // ���܂菬���Ȑ��l���w�肷��Ɩ�肪�N����̂�

		tjs_int vague = 64;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("vague"), &tmp)))
			if (tmp.Type() != tvtVoid)
				vague = (tjs_int)tmp;
		tjs_int32 xblur=16, yblur=16;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur"), &tmp)))
			if (tmp.Type() != tvtVoid)
				xblur = yblur = (tjs_int32)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("xblur"), &tmp)))
			if (tmp.Type() != tvtVoid)
				xblur = (tjs_int32)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("yblur"), &tmp)))
			if (tmp.Type() != tvtVoid)
				yblur = (tjs_int32)tmp;
		double accel = 1.0;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("accel"), &tmp)))
			if(tmp.Type() != tvtVoid)
				accel = (double)tmp;

		// retrieve "rule" option and load it as an image
		const tjs_char *rulename;

		tjs_error er = options->GetAsString(TJS_W("rule"), &rulename);
		if(TJS_FAILED(er))
			TVPThrowExceptionMessage(TJS_W("�I�v�V���� %1 ���w�肵�Ă�������"), TJS_W("rule"));
		iTVPScanLineProvider *ruleimg;
		er = imagepro->LoadImage(rulename, 8, 0x02ffffff, src1w, src1h, &ruleimg);
		if(TJS_FAILED(er))
			TVPThrowExceptionMessage(TJS_W("���[���摜 %1 ��ǂݍ��ނ��Ƃ��ł��܂���"), rulename);

		DoBoxBlur(ruleimg, xblur, yblur);
		// ���]���K�v�Ȃ甽�]����
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("neg"), &tmp)))
			if(tmp.Type() != tvtVoid)
				NegateGrayImage(ruleimg);

		// �I�u�W�F�N�g���쐬
		try {
			*handler = new tTVPDimTransHandler(ruleimg, time, vague, layertype, src1w, src1h, accel);
		} catch(...) {
			ruleimg->Release();
			throw;
		}
		ruleimg->Release();

		return TJS_S_OK;
	}

} static * DimTransHandlerProvider;


// Integral Image dst �ɁAsrc ����ꃉ�C���ǉ�����B
inline void tTVPDimTransHandlerProvider::addALineToIntegralImage8(DWORD *dst, DWORD *dstprev, BYTE *src, tjs_int srcwidth, tjs_int xblur)
{
	DWORD sum = 0;
#ifndef USE_SSE2
	BYTE col = *src;
	for (int x = -xblur-1; x < 0; x++) {
		sum += col;
		*dst++ = sum + *dstprev++;
	}
	for (int x = 0; x < srcwidth-1; x++) {
		sum += *src++;
		*dst++ = sum + *dstprev++;
	}
	col = *src++;
	for (int x = srcwidth-1; x < srcwidth+xblur; x++) {
		sum += col;
		*dst++ = sum + *dstprev++;
	}
#else
	__asm {
		mov		esi,   src
		mov		edi,   dst
		mov		ebx,   dstprev
		mov		ecx,   xblur
		inc		ecx
	ADDALINETOINTEGRALIMAGE8_LOOP1:
		mov		edx,   [esi]
		and		edx,   0xff
		add		eax,   edx	// sum += *src
		mov		edx,   [ebx]
		add		edx,   eax
		mov		[edi], edx	// *dst = sum + *dstprev
		add		ebx,   4
		add		edi,   4
		loop		ADDALINETOINTEGRALIMAGE8_LOOP1

		mov		ecx,   srcwidth
		dec		ecx
	ADDALINETOINTEGRALIMAGE8_LOOP2:
		mov		edx,   [esi]
		and		edx,   0xff
		add		eax,   edx	// sum += *src
		mov		edx,   [ebx]
		add		edx,   eax
		mov		[edi], edx	// *dst = sum + *dstprev
		inc		esi
		add		ebx,   4
		add		edi,   4
		loop		ADDALINETOINTEGRALIMAGE8_LOOP2

		mov		ecx,   xblur
		inc		ecx
	ADDALINETOINTEGRALIMAGE8_LOOP3:
		mov		edx,   [esi]
		and		edx,   0xff
		add		eax,   edx	// sum += *src
		mov		edx,   [ebx]
		add		edx,   eax
		mov		[edi], edx	// *dst = sum + *dstprev
		add		ebx,   4
		add		edi,   4
		loop		ADDALINETOINTEGRALIMAGE8_LOOP3
	}
#endif 
}


// �C���e�O�����C���[�W����A�u���[�����摜���ꃉ�C���쐬����
inline void tTVPDimTransHandlerProvider::drawALineFromIntegralImageToImage8(BYTE *dst, tjs_int dstwidth, DWORD *src1, DWORD *src2, tjs_int xblur, tjs_int sq)
{
	int xblurwidth = xblur*2+1;
#ifndef USE_SSE2
	for (int x = 0; x < dstwidth; x++) {
		*dst++ = ((*src1 - *(src1+xblurwidth) - *src2 + *(src2+xblurwidth))/sq)&0xff;
		src1++, src2++;
	}
#else
	float rsqf = (float)65536/sq;
	__asm {
		mov			esi,  src1
		mov			edx,  src2
		mov			edi,  dst
		mov			ebx,  xblurwidth
		sal			ebx,  2
		mov			eax,  rsqf
		movd		xmm1, eax
		pshufd		xmm1, xmm1, 0x0
		mov			ecx,  dstwidth
		sar			ecx,  2
		jz			DRAWALINEFROMINTEGRALIMAGETOIMAGE8_NEXT
	DRAWALINEFROMINTEGRALIMAGETOIMAGE8_LOOPx4:
		movdqu		xmm0, [esi]
		movdqu		xmm2, [edx+ebx]
		paddd		xmm0, xmm2
		movdqu		xmm2, [esi+ebx]
		psubd		xmm0, xmm2
		movdqu		xmm2, [edx]
		psubd		xmm0, xmm2	// xmm0 = |sum1|sum2|sum3|sum4|
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16	// xmm0 = |g1|g2|g3|g4|(dword)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0	// xmm0 = |g1|g2|g3|g4|(byte)
		movd		[edi],xmm0
		add			esi,  16
		add			edx,  16
		add			edi,  4
		loop		DRAWALINEFROMINTEGRALIMAGETOIMAGE8_LOOPx4

	DRAWALINEFROMINTEGRALIMAGETOIMAGE8_NEXT:
		mov			ecx,   dstwidth
		and			ecx,   0x3
		jz			DRAWALINEFROMINTEGRALIMAGETOIMAGE8_END
	DRAWALINEFROMINTEGRALIMAGETOIMAGE8_LOOPx1:
		mov			eax,  [esi]
		add			eax,  [edx+ebx]
		sub			eax,  [esi+ebx]
		sub			eax,  [edx]	// xmm0 = |sum1|sum2|sum3|sum4|
		movd		xmm0, eax
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1	// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16	// xmm0 = |g1|g2|g3|g4|(dword)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0	// xmm0 = |g1|g2|g3|g4|(byte)
		movd		eax,  xmm0
		mov			[edi],al
		add			esi,  4
		add			edx,  4
		inc			edi
		loop		DRAWALINEFROMINTEGRALIMAGETOIMAGE8_LOOPx1
	DRAWALINEFROMINTEGRALIMAGETOIMAGE8_END:
	}
#endif
}


void tTVPDimTransHandlerProvider::DoBoxBlur(iTVPScanLineProvider *img, tjs_int xblur, tjs_int yblur)
{
	tjs_int width, height, pitch;
	img->GetWidth(&width), img->GetHeight(&height), img->GetPitchBytes(&pitch);
	// iimg = �C���e�O�����C���[�W
	int  iimgwidth = width+xblur*2+1;
	int  iimgpitch = iimgwidth*sizeof(DWORD);
	int  iimgsiz   = (yblur*2+1+1)*iimgpitch;
	BYTE *iimg     = (BYTE*)TJSAlignedAlloc(iimgsiz, 4);
	// �ŏ��̃C���e�O�����C���[�W((xblur*2+1)x(yblur*2+1))�쐬

	DWORD *iimgp = (DWORD*)iimg;
	// �ŏ��̗��0�Ŗ��߂�
	for	(int x = -xblur-1; x < width+xblur; x++)
		*iimgp++ = 0;
	// ������̗�́u�����܂ł̘a�v�{�u���̒l�v
	BYTE *src;
	for (int y = -yblur; y < yblur+1 ; y++) {
		img->GetScanLine(y, (const void**)&src);
		if (y < 0)
			img->GetScanLine(0, (const void**)&src);
		else if (y >= height)
			img->GetScanLine(height-1, (const void**)&src);
		iimgp = (DWORD*)(iimg + (yblur+1+y)*iimgpitch);
		addALineToIntegralImage8(iimgp, (DWORD*)((BYTE*)iimgp-iimgpitch), src, width, xblur);
	}
	// ����ōŏ��̃C���e�O�����C���[�W(iimgwidth x yblur*2+1+1) �� iimg �ȉ��ɂł���

	int sq = (xblur*2+1)*(yblur*2+1);
	int xblurwidth = (xblur*2+1)*sizeof(DWORD);
	BYTE *dst;
	// ��E���|�C���^
	DWORD *iimgp1 = (DWORD*)iimg, *iimgp2 = (DWORD*)(iimg+(yblur*2+1)*iimgpitch);
	for (int y = 0; y < height; y++) {
		// ����IntegralImage�ŕ`��
		img->GetScanLineForWrite(y, (void**)&dst);
		drawALineFromIntegralImageToImage8(dst, width, iimgp1, iimgp2, xblur, sq);
		// ��4000/6800 ���炢�̏d��
		// �V�������C����ǉ�����
		BYTE *imgyblurp;
		img->GetScanLine(y+yblur+1, (const void**)&imgyblurp);
		if (y+yblur+1 >= height)
			img->GetScanLine(height-1, (const void**)&imgyblurp);
		addALineToIntegralImage8(iimgp1, iimgp2, imgyblurp, width, xblur);
		iimgp2 = iimgp1;
		iimgp1 = (DWORD*)((BYTE*)iimgp1 + iimgpitch);
		if ((BYTE*)iimgp1 >= iimg+iimgsiz)
			iimgp1 = (DWORD*)iimg;
		// ��1000/6800 ���炢�̏d��
	}
	TJSAlignedDealloc(iimg);
}


void tTVPDimTransHandlerProvider::NegateGrayImage(iTVPScanLineProvider *img)
{
	tjs_int width, height, pitch;
	img->GetWidth(&width), img->GetHeight(&height), img->GetPitchBytes(&pitch);
	BYTE *p;

	for (int y = 0; y < height; y++) {
		img->GetScanLineForWrite(y, (void **)&p);
		for (int x = 0; x < width; x++) {
			*p = 255-*p;
			p++;
		}
	}
}

//---------------------------------------------------------------------------
void RegisterDimTransHandlerProvider()
{
	// TVPAddTransHandlerProvider ���g���ăg�����W�V�����n���h���v���o�C�_��
	// �o�^����
	DimTransHandlerProvider = new tTVPDimTransHandlerProvider();
	TVPAddTransHandlerProvider(DimTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterDimTransHandlerProvider()
{
	// TVPRemoveTransHandlerProvider ���g���ăg�����W�V�����n���h���v���o�C�_��
	// �o�^��������
	TVPRemoveTransHandlerProvider(DimTransHandlerProvider);
	DimTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
