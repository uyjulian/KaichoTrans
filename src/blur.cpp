//---------------------------------------------------------------------------
#include <windows.h>
#include "tp_stub.h"
#include <math.h>
#include "common.h"
#include "blur.h"

// for __aligned_malloc()
#include <malloc.h>
// for std:bad_alloc()
#include <new>

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
	boxBlur �g�����W�V����
	2012/01/29	0.2.5.0	���I�ȉ�ʂł�blur�ł���I�v�V����dynamic��ǉ�
	2011/12/09	0.2.1.0	"bad alloc exception thrown"������c��������
*/
//---------------------------------------------------------------------------

#define USE_SSE2 // SSE2���g���Ȃ��`


class tTVPBlurTransHandler : public iTVPDivisibleTransHandler
{
	//	boxBlur �g�����W�V�����n���h���N���X�̎���

	tjs_int RefCount; // �Q�ƃJ�E���^
	/*
	 * iTVPDivisibleTransHandler �� �Q�ƃJ�E���^�ɂ��Ǘ����s��
	 */

protected:
	bool          First;		// ��ԍŏ��̌Ăяo�����ǂ���
	bool          Dynamic;		// ���I�ȉ摜�ɑΉ����邩�ǂ���
	tjs_int64     StartTick;	// �g�����W�V�������J�n���� tick count
	tjs_int64     HalfTime;		// �g�����W�V�����ɗv���鎞�� / 2
	tjs_int64     Time;			// �g�����W�V�����ɗv���鎞��
	tTVPLayerType LayerType;	// ���C���^�C�v
	tjs_int       Width;		// ��������摜�̕�
	tjs_int       Height;		// ��������摜�̍���

	tjs_int64     CurTime;		// ���݂� tick count
	tjs_int       BlendRatio;	// �u�����h��

	double        Accel;		// �i�s�����x(def=1.0)
	double        CurRatio;		// ���݂̐i�s�䗦(0�`1.0)

	typedef tjs_uint32 IPIXELTYPE;	// �C���e�O�����C���[�W�̃h�b�g�^�C�v
	#define IPIXELCOLORNUM 4	// �C���e�O�����C���[�W�̃J���[��(ARGB)
	#define IPIXELSIZE (sizeof(IPIXELTYPE)*IPIXELCOLORNUM)

	BYTE*         Iimg1;		// src1�p�C���e�O�����C���[�W
	tjs_int       MaxXblur1, MaxYblur1; // src1�̍ő�u���[��
	tjs_int       Iimgwidth1;	// �C���e�O�����C���[�W�̉���
	tjs_int       Iimgheight1;	// �C���e�O�����C���[�W�̏c��
	tjs_int       Iimgpitch1;	// �C���e�O�����C���[�W�̏c1�h�b�g�Ԃ̍��كo�C�g��
	tjs_int       CurXblur1, CurYblur1; // src1�̌��݂̃u���[��

	BYTE*         Iimg2;		// src2�p�C���e�O�����C���[�W
	tjs_int       MaxXblur2, MaxYblur2;// src2�̍ő�u���[��
	tjs_int       Iimgwidth2;	// �C���e�O�����C���[�W�̉���
	tjs_int       Iimgheight2;	// �C���e�O�����C���[�W�̏c��
	tjs_int       Iimgpitch2;	// �C���e�O�����C���[�W�̏c1�h�b�g�Ԃ̍��كo�C�g��
	tjs_int       CurXblur2, CurYblur2; // src1�̌��݂̃u���[��

	tjs_uint32    *bdst1, *bdst2; // src1/src2��������1���C�����̃e���|�����摜
BYTE* p;
	// ���݂�Ratio(0�`1.0)�����߂�
	inline double setCurrentRatio() //(tjs_int64 starttick=StartTick, tjs_int64 time=Time, tjs_int64 curtick=CurTime, double accel=Accel)
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

	inline BYTE *getIimg1Addr(tjs_int x, tjs_int y) {
		return Iimg1 + ((MaxYblur1+1+1 + y)*Iimgpitch1 + (MaxXblur1+1 + x)*IPIXELSIZE);
	}
	inline BYTE *getIimg2Addr(tjs_int x, tjs_int y) {
		return Iimg2 + ((MaxYblur2+1+1 + y)*Iimgpitch2 + (MaxXblur2+1 + x)*IPIXELSIZE);
	}
	void addALineToIntegralImage32(
			IPIXELTYPE *dst, IPIXELTYPE *dstprev,
			tjs_uint32 *src, tjs_int srcwidth,
			tjs_int xblur);
	void drawALineFromIntegralImageToImage32(
			tjs_uint32 *dst, tjs_int dstwidth,
			IPIXELTYPE *src1, IPIXELTYPE *src2,
			tjs_int xblur, tjs_int sq);
	void tTVPBlurTransHandler::buildIntegralImage32(
			IPIXELTYPE *dst, tjs_int dstpitch,
			iTVPScanLineProvider *srcdat, tjs_int width, tjs_int height,
			tjs_int xblur, tjs_int yblur);

	// �������m�ۂ̃��b�p�֐��BTJSAlignedAlloc()������bad_alloc�ɂȂ�̂�
	void *local_alloc(size_t size, size_t align=0)
	{
		void * p = _aligned_malloc(size, align);
		if (p == NULL)
			throw(std::bad_alloc());
		return p;
	}
	// �����free�B
	void local_free(void* p)
	{
		_aligned_free(p);
	}

public:
	// �R���X�g���N�^
	tTVPBlurTransHandler(tjs_uint64 time, tTVPLayerType layertype,
		tjs_int width, tjs_int height,
		tjs_int xblur1, tjs_int yblur1, tjs_int xblur2, tjs_int yblur2,
		double accel, bool dynamic)
	{
		RefCount    = 1;

		First       = true;
		LayerType   = layertype;
		Width       = width;
		Height      = height;
		Time        = time;
		HalfTime    = time / 2;
		Accel       = accel;
		Dynamic     = dynamic;

		MaxXblur1   = xblur1;
		MaxYblur1   = yblur1;
		Iimgwidth1  = width+xblur1*2+1;
		Iimgpitch1  = Iimgwidth1*IPIXELSIZE;
		Iimgheight1 = height+yblur1*2+1+1;
		Iimg1       = (BYTE*)local_alloc(Iimgheight1*Iimgpitch1, 16);

		MaxXblur2   = xblur2;
		MaxYblur2   = yblur2;
		Iimgwidth2  = width+xblur2*2+1;
		Iimgpitch2  = Iimgwidth2*IPIXELSIZE;
		Iimgheight2 = height+yblur2*2+1+1;
		Iimg2       = (BYTE*)local_alloc(Iimgheight2*Iimgpitch2, 16);

		bdst1       = (tjs_uint32*)local_alloc(Width*sizeof(*bdst1), 16);
		bdst2       = (tjs_uint32*)local_alloc(Width*sizeof(*bdst2), 16);
	}

	// �f�X�g���N�^
	virtual ~tTVPBlurTransHandler()
	{
		local_free(Iimg1);
		local_free(Iimg2);
		local_free(bdst1);
		local_free(bdst2);
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
		if(RefCount == 1)
			delete this;
		else
			RefCount--;
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
tjs_error TJS_INTF_METHOD tTVPBlurTransHandler::StartProcess(tjs_uint64 tick)
{
	// �g�����W�V�����̉�ʍX�V��񂲂ƂɌĂ΂��

	// �g�����W�V�����̉�ʍX�V���ɂ��A�܂��ŏ��� StartProcess ���Ă΂��
	// ���̂��� Process ��������Ă΂�� ( �̈�𕪊��������Ă���ꍇ )
	// �Ō�� EndProcess ���Ă΂��

	if (First)	// �ŏ��̎��s
		StartTick = tick;
	if (Dynamic)		// ���I��ʍX�V���K�v�Ȃ玟��First�ɂ���
		First = true;	// Integral Image���č쐬����B�����ʁB

	// �摜���Z�ɕK�v�Ȋe�p�����[�^���v�Z
	CurTime = ((tjs_int64)tick - StartTick);
	if (CurTime > Time) CurTime = Time;

	// BlendRatio
	BlendRatio = tjs_int(CurTime * 255 / Time);
	if(BlendRatio > 255) BlendRatio = 255;

	//CurXblur/CurYblur
	CurXblur1 = tjs_int(MaxXblur1*CurTime/Time);
	CurYblur1 = tjs_int(MaxYblur1*CurTime/Time);
	CurXblur2 = tjs_int(MaxXblur2*(Time-CurTime)/Time);
	CurYblur2 = tjs_int(MaxYblur2*(Time-CurTime)/Time);

	return TJS_S_TRUE;
}

//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTVPBlurTransHandler::EndProcess()
{
	// �g�����W�V�����̉�ʍX�V��񕪂��I��邲�ƂɌĂ΂��

	if(BlendRatio == 255) return TJS_S_FALSE; // �g�����W�V�����I��

	return TJS_S_TRUE;
}


/*
 * �C���e�O�����C���[�W(=dst)�� src ����v�Z���Ĉꃉ�C�����ǉ�����
 * dst�ɂ̓������͊��Ɋm�ۂ���Ă�����̂Ƃ���
 */
void tTVPBlurTransHandler::addALineToIntegralImage32(
		IPIXELTYPE *dst, IPIXELTYPE *dstprev, tjs_uint32 *src, tjs_int srcwidth, tjs_int xblur)
{
#ifndef USE_SSE2
	tjs_uint32 r_sum = 0, g_sum = 0, b_sum = 0;
	tjs_uint32 col = *src;
	for (int x = -xblur-1; x < 0; x++) {
		b_sum += (col>> 0)&0xff;
		g_sum += (col>> 8)&0xff;
		r_sum += (col>>16)&0xff;
		*dst++ = b_sum + *dstprev++;
		*dst++ = g_sum + *dstprev++;
		*dst++ = r_sum + *dstprev++;
		dst++, dstprev++;	// �󂫕����X�L�b�v
	}
	for (int x = 0; x < srcwidth-1; x++) {
		col = *src++;
		b_sum += (col>> 0)&0xff;
		g_sum += (col>> 8)&0xff;
		r_sum += (col>>16)&0xff;
		*dst++ = b_sum + *dstprev++;
		*dst++ = g_sum + *dstprev++;
		*dst++ = r_sum + *dstprev++;
		dst++, dstprev++;	// �󂫕����X�L�b�v
	}
	col = *src++;
	for (int x = srcwidth-1; x < srcwidth+xblur; x++) {
		b_sum += (col>> 0)&0xff;
		g_sum += (col>> 8)&0xff;
		r_sum += (col>>16)&0xff;
		*dst++ = b_sum + *dstprev++;
		*dst++ = g_sum + *dstprev++;
		*dst++ = r_sum + *dstprev++;
		dst++, dstprev++;	// �󂫕����X�L�b�v
	}
#else
	// �C�����C���A�Z���u���̓N���X�����o�ϐ��ɃA�N�Z�X�ł��Ȃ�(����Ɖ������킸�ɕςȒl�ɂȂ�)
	// �̂ŁA���[�J���ϐ��ɂ̂݃A�N�Z�X���Ă��邱�Ƃɒ���
	__asm {
		mov			esi,  src
		mov			edi,  dst
		pxor		xmm1, xmm1	// unpack�p
		pxor		xmm2, xmm2	// sum�p
		mov			ebx,  dstprev
		mov			ecx,  xblur
		inc			ecx
		movd		xmm0, [esi]
		punpcklbw	xmm0, xmm1	// byte -> word
		punpcklwd	xmm0, xmm1	// word -> tjs_uint32
	BUILDINTIMAGE_LOOP1:
		paddd		xmm2, xmm0
		movdqa		xmm3, [ebx]
		paddd		xmm3, xmm2
		movdqa		[edi],xmm3
		add			edi,  16
		add			ebx,  16
		loop		BUILDINTIMAGE_LOOP1
		mov			ecx,  srcwidth
		dec			ecx
	BUILDINTIMAGE_LOOP2:
		movd		xmm0, [esi]
		punpcklbw	xmm0, xmm1	// byte -> word
		punpcklwd	xmm0, xmm1	// word -> tjs_uint32
		paddd		xmm2, xmm0
		movdqa		xmm3, [ebx]
		paddd		xmm3, xmm2
		movdqa		[edi],xmm3
		add			esi,  4
		add			edi,  16
		add			ebx,  16
		loop		BUILDINTIMAGE_LOOP2
		mov			ecx,  xblur
		inc			ecx
		movd		xmm0, [esi]
		punpcklbw	xmm0, xmm1	// byte -> word
		punpcklwd	xmm0, xmm1	// word -> tjs_uint32
	BUILDINTIMAGE_LOOP3:
		paddd		xmm2, xmm0
		movdqa		xmm3, [ebx]
		paddd		xmm3, xmm2
		movdqa		[edi],xmm3
		add			edi,  16
		add			ebx,  16
		loop		BUILDINTIMAGE_LOOP3
	}
#endif
}

/*
 * �C���e�O�����C���[�W(=src)������dst�Ɉꃉ�C���`�悷��
 */
void tTVPBlurTransHandler::drawALineFromIntegralImageToImage32(
		tjs_uint32 *dst, tjs_int dstwidth, IPIXELTYPE *src1, IPIXELTYPE *src2, tjs_int xblur, tjs_int sq)
{
#ifndef USE_SSE2
	int xblurwidth = (xblur*2+1)*IPIXELCOLORNUM;
	for (int x = 0; x < dstwidth; x++) {
		tjs_uint32 b = (*src1 - *(src1+xblurwidth) - *src2 + *(src2+xblurwidth))/sq;
		src1++, src2++;
		tjs_uint32 g = (*src1 - *(src1+xblurwidth) - *src2 + *(src2+xblurwidth))/sq;
		src1++, src2++;
		tjs_uint32 r = (*src1 - *(src1+xblurwidth) - *src2 + *(src2+xblurwidth))/sq;
		src1+=2, src2+=2;
		*dst++ = ((r&0xff)<<16) | ((g&0xff)<<8) | (b&0xff);
	}
#else
	int xblurwidthsize = (xblur*2+1)*IPIXELSIZE;
	// �������� rsqf �� 1/sq �łȂ��̂́A�v�Z����(�P���x�����_��)�덷���傫�߂���
	// �摜���ςɂȂ邱�Ƃ����邩��B�F�����Z�������̂Ɍ����߂������덷�����Ȃ����߁B
	float rsqf = (float)(1<<16)/sq;
	__asm {
		mov			esi,  src1
		mov			edx,  src2
		mov			edi,  dst
		mov			ebx,  xblurwidthsize
		mov			eax,  rsqf
		movd		xmm1, eax
		pshufd		xmm1, xmm1, 0x0
		mov			ecx,  dstwidth
		sar			ecx,  2
		// �ŏ��� 4dot ���Ƃɏ�������B����ŃX�s�[�h�񊄑����B
	BOXBLUR_LOOP1:
		movdqa		xmm0, [esi]
		paddd		xmm0, [edx+ebx]
		psubd		xmm0, [esi+ebx]
		psubd		xmm0, [edx]			// xmm0 = |sum_a|sum_r|sum_g|sum_b}
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1			// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16			// xmm0 = |a|r|g|b|(tjs_uint32)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0			// xmm0 = |a|r|g|b|(byte)
		movd		[edi],xmm0
		add			esi, 16
		add			edx, 16
		add			edi, 4

		movdqa		xmm0, [esi]
		paddd		xmm0, [edx+ebx]
		psubd		xmm0, [esi+ebx]
		psubd		xmm0, [edx]			// xmm0 = |sum_a|sum_r|sum_g|sum_b}
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1			// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16			// xmm0 = |a|r|g|b|(tjs_uint32)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0			// xmm0 = |a|r|g|b|(byte)
		movd		[edi],xmm0
		add			esi, 16
		add			edx, 16
		add			edi, 4

		movdqa		xmm0, [esi]
		paddd		xmm0, [edx+ebx]
		psubd		xmm0, [esi+ebx]
		psubd		xmm0, [edx]			// xmm0 = |sum_a|sum_r|sum_g|sum_b}
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1			// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16			// xmm0 = |a|r|g|b|(tjs_uint32)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0			// xmm0 = |a|r|g|b|(byte)
		movd		[edi],xmm0
		add			esi, 16
		add			edx, 16
		add			edi, 4

		movdqa		xmm0, [esi]
		paddd		xmm0, [edx+ebx]
		psubd		xmm0, [esi+ebx]
		psubd		xmm0, [edx]			// xmm0 = |sum_a|sum_r|sum_g|sum_b}
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1			// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16			// xmm0 = |a|r|g|b|(tjs_uint32)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0			// xmm0 = |a|r|g|b|(byte)
		movd		[edi],xmm0
		add			esi, 16
		add			edx, 16
		add			edi, 4
		dec			ecx
		jnz			BOXBLUR_LOOP1

		// �c��h�b�g������
		mov			ecx,  dstwidth
		and			ecx,  3
		jz			BOXBLUR_LOOP_END
	BOXBLUR_LOOP2:
		movdqa		xmm0, [esi]
		paddd		xmm0, [edx+ebx]
		psubd		xmm0, [esi+ebx]
		psubd		xmm0, [edx]			// xmm0 = |sum_a|sum_r|sum_g|sum_b}
		cvtdq2ps	xmm0, xmm0
		mulps		xmm0, xmm1			// xmm0 *= 65536/sq
		cvtps2dq	xmm0, xmm0
		psrld		xmm0, 16			// xmm0 = |a|r|g|b|(tjs_uint32)
		packssdw	xmm0, xmm0
		packuswb	xmm0, xmm0			// xmm0 = |a|r|g|b|(byte)
		movd		[edi],xmm0
		add			esi, 16
		add			edx, 16
		add			edi, 4
		loop		BOXBLUR_LOOP2
	BOXBLUR_LOOP_END:
	}
#endif
}

/*
 * buildIntegralImage: �摜����C���e�O�����C���[�W���쐬����
 */
void tTVPBlurTransHandler::buildIntegralImage32(
		IPIXELTYPE *dst, tjs_int dstpitch,
		iTVPScanLineProvider *srcdat, tjs_int width, tjs_int height,
		tjs_int xblur, tjs_int yblur)
{
	// iimgp �� ��ʂɏc���u���[+1�Əc��h�b�g��ǉ������傫�߉�ʁB
	// �ŏ��̗��0�Ŗ��߂�
	for	(int x = -xblur-1; x < width+xblur; x++)
		*dst++ = 0, *dst++ = 0, *dst++ = 0, dst++;
	// ������̗�́u�����܂ł̘a�v�{�u���̒l�v
	tjs_uint32 *src;
	srcdat->GetScanLine(0, (const void**)&src);	// �j�炵���G���[�`�F�b�N���Ȃ�
//log(L"dst=0x%08x, dstpitch=%d, src=0x%08x, width=%d, height=%d, xblur=%d, yblur=%d", 
//	dst, dstpitch, src, width, height, xblur, yblur);
	for (int y = -yblur-1; y < 0; y++) {
		addALineToIntegralImage32(dst, (tjs_uint32*)((BYTE*)dst-dstpitch), src, width, xblur);
		dst = (IPIXELTYPE*)((BYTE*)dst + dstpitch);
	}
	for (int y = 0; y < height-1; y++) {
		srcdat->GetScanLine(y, (const void**)&src);	// �j�炵���G���[�`�F�b�N���Ȃ�
		addALineToIntegralImage32(dst, (tjs_uint32*)((BYTE*)dst-dstpitch), src, width, xblur);
		dst = (IPIXELTYPE*)((BYTE*)dst + dstpitch);
	}
	srcdat->GetScanLine(height-1, (const void**)&src);	// �j�炵���G���[�`�F�b�N���Ȃ�
	for (int y = height; y < height+yblur; y++) {
		addALineToIntegralImage32(dst, (tjs_uint32*)((BYTE*)dst-dstpitch), src, width, xblur);
		dst = (IPIXELTYPE*)((BYTE*)dst + dstpitch);
	}
}


//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTVPBlurTransHandler::Process(
			tTVPDivisibleData *data)
{
	// �g�����W�V�����̊e�̈悲�ƂɌĂ΂��
	// �g���g���͉�ʂ��X�V����Ƃ��ɂ������̗̈�ɕ������Ȃ��珈�����s���̂�
	// ���̃��\�b�h�͒ʏ�A��ʍX�V���ɂ�������Ă΂��

	// data �ɂ͗̈��摜�Ɋւ����񂪓����Ă���

	if (First) {	// �ŏ��̎��s
		// ����́A�C���e�O�����C���[�W�����B
		buildIntegralImage32((IPIXELTYPE*)Iimg1, Iimgpitch1, data->Src1, Width, Height, MaxXblur1, MaxYblur1);
		buildIntegralImage32((IPIXELTYPE*)Iimg2, Iimgpitch2, data->Src2, Width, Height, MaxXblur2, MaxYblur2);
		First = false;
//log(L"Iimg1=0x%08x, Iimgwidth1=%d, Iimgheight1=%d, Iimgpitch1=%d, MaxXblur1=%d, MaxYblur1=%d",
//		Iimg1, Iimgwidth1, Iimgheight1, Iimgpitch1, MaxXblur1, MaxYblur1);
//log(L"Iimg2=0x%08x, Iimgwidth2=%d, Iimgheight2=%d, Iimgpitch2=%d, MaxXblur2=%d, MaxYblur2=%d",
//		Iimg2, Iimgwidth2, Iimgheight2, Iimgpitch2, MaxXblur2, MaxYblur2);
	}

	// �������� boxBlur
	int sq1 = (CurXblur1*2+1)*(CurYblur1*2+1), sq2 = (CurXblur2*2+1)*(CurYblur2*2+1);
	// ���C�����Ƃɏ���
	for (int n = 0; n < data->Height; n++) {
		int	dstx = data->DestLeft;
		int	dsty = data->DestTop + n;
		int srcx = data->Left;
		int srcy = data->Top + n;
		// bdst1 �� src1 ����̈ꃉ�C������Blur��z������߂�
		IPIXELTYPE *src_u, *src_d;
		src_u = (IPIXELTYPE*)getIimg1Addr(srcx-(CurXblur1+1), srcy-(CurYblur1+1));	// ��
		src_d = (IPIXELTYPE*)getIimg1Addr(srcx-(CurXblur1+1), srcy+(CurYblur1+0));	// ���
		drawALineFromIntegralImageToImage32(bdst1, data->Width, src_u, src_d, CurXblur1, sq1);
		// bdst2 �� src2 ����̈ꃉ�C������Blur��z������߂�
		src_u = (IPIXELTYPE*)getIimg2Addr(srcx-(CurXblur2+1), srcy-(CurYblur2+1));	// ��
		src_d = (IPIXELTYPE*)getIimg2Addr(srcx-(CurXblur2+1), srcy+(CurYblur2+0));	// ���
		drawALineFromIntegralImageToImage32(bdst2, data->Width, src_u, src_d, CurXblur2, sq2);

		tjs_uint32 *dst;
		data->Dest->GetScanLineForWrite(data->DestTop+n, (void**)&dst);
		// �ŁA������bdst1��bdst2����������dst�ɓ]������
		if (LayerType == ltAlpha) {
			TVPConstAlphaBlend_SD_d(dst+data->DestLeft-data->Left,
									bdst1, bdst2, data->Width, BlendRatio);
		} else if (LayerType == ltAddAlpha) {
			TVPConstAlphaBlend_SD_a(dst+data->DestLeft-data->Left,
									bdst1, bdst2, data->Width, BlendRatio);
		} else {
			TVPConstAlphaBlend_SD(dst+data->DestLeft-data->Left,
									bdst1, bdst2, data->Width, BlendRatio);
		}
	}

	return TJS_S_OK;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
class tTVPBlurTransHandlerProvider : public iTVPTransHandlerProvider
{
	tjs_uint RefCount; // �Q�ƃJ�E���^
public:
	tTVPBlurTransHandlerProvider() { RefCount = 1; }
	~tTVPBlurTransHandlerProvider() {; }

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
		if(name) *name = TJS_W("blur");
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
		tjs_uint64 time;
		tjs_uint32 xblur1 = 32, yblur1 = 32;
		tjs_uint32 xblur2 = 32, yblur2 = 32;
		double accel = 1.0;
		bool dynamic = false;

		if(TJS_FAILED(options->GetValue(TJS_W("time"), &tmp)))
			return TJS_E_FAIL; // time �������w�肳��Ă��Ȃ�
		if(tmp.Type() == tvtVoid) return TJS_E_FAIL;
		time = (tjs_int64)tmp;
		if(time < 2) time = 2; // ���܂菬���Ȑ��l���w�肷��Ɩ�肪�N����̂�

		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur1x"), &tmp)))
			if(tmp.Type() != tvtVoid) xblur1 = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur1y"), &tmp)))
			if(tmp.Type() != tvtVoid) yblur1 = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur1"), &tmp)))
			if(tmp.Type() != tvtVoid) xblur1 = yblur1 = (tjs_int)tmp;

		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur2x"), &tmp)))
			if(tmp.Type() != tvtVoid) xblur2 = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur2y"), &tmp)))
			if(tmp.Type() != tvtVoid) yblur2 = (tjs_int)tmp;
		if(TJS_SUCCEEDED(options->GetValue(TJS_W("blur2"), &tmp)))
			if(tmp.Type() != tvtVoid) xblur2 = yblur2 = (tjs_int)tmp;

		if(TJS_SUCCEEDED(options->GetValue(TJS_W("accel"), &tmp)))
			if(tmp.Type() != tvtVoid) accel = (double)tmp;

		if(TJS_SUCCEEDED(options->GetValue(TJS_W("dynamic"), &tmp)))
			if(tmp.Type() != tvtVoid) dynamic = ((tjs_int)tmp != 0);

		// �I�u�W�F�N�g���쐬
		*handler = new tTVPBlurTransHandler(time, layertype,
			src1w, src1h,
			xblur1, yblur1, xblur2, yblur2,
			accel, dynamic);

		return TJS_S_OK;
	}

} static * BlurTransHandlerProvider;
//---------------------------------------------------------------------------
void RegisterBlurTransHandlerProvider()
{
	// TVPAddTransHandlerProvider ���g���ăg�����W�V�����n���h���v���o�C�_��
	// �o�^����
	BlurTransHandlerProvider = new tTVPBlurTransHandlerProvider();
	TVPAddTransHandlerProvider(BlurTransHandlerProvider);
}
//---------------------------------------------------------------------------
void UnregisterBlurTransHandlerProvider()
{
	// TVPRemoveTransHandlerProvider ���g���ăg�����W�V�����n���h���v���o�C�_��
	// �o�^��������
	TVPRemoveTransHandlerProvider(BlurTransHandlerProvider);
	BlurTransHandlerProvider->Release();
}
//---------------------------------------------------------------------------
