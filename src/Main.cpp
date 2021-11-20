//---------------------------------------------------------------------------
#include <windows.h>

#include "tp_stub.h"
	// tp_stub.h �� tp_stub.cpp �͕K���v���W�F�N�g�ɒǉ�����

#include "blur.h"
#include "dim.h"


//---------------------------------------------------------------------------
//#pragma argsused
int WINAPI DllEntryPoint(HINSTANCE hinst, unsigned long reason, void* lpReserved)
{
	// DLL �G���g���|�C���g
	return 1;
}
//---------------------------------------------------------------------------
// V2Link �� DLL �������N�����Ƃ��Ɏ��s�����
extern "C" HRESULT __declspec(dllexport) V2Link(iTVPFunctionExporter *exporter)
{
	// �X�^�u�̏�����
	TVPInitImportStub(exporter); // �K������͋L�q
	/*
		TVPInitImpotStub �����s������͋g���g�������̊e�֐��� tTJSVariant ����
		��{�I�Ȍ^���g�p�\�ɂȂ�B
	*/

	// �g�����W�V�����n���h���v���o�C�_�̓o�^
	RegisterBlurTransHandlerProvider();
	RegisterDimTransHandlerProvider();

	return S_OK;
}
//---------------------------------------------------------------------------
// V2Unlink �� DLL ���A�������N�����Ƃ��Ɏ��s�����
extern "C" HRESULT __declspec(dllexport) V2Unlink()
{
	// �g�����W�V�����n���h���v���o�C�_�̓o�^�폜
	UnregisterBlurTransHandlerProvider();
	UnregisterDimTransHandlerProvider();

	// �X�^�u�̎g�p�I��
	TVPUninitImportStub(); // �K������͋L�q
	/*
		TVPUninitImpotStub �� TVPInitImportStub �Ŋm�ۂ������������������
		�̂ŕK���L�q����B
	*/
	return S_OK;
}
//---------------------------------------------------------------------------
/*
	V2Link �� V2Unlink �� DLL ����G�N�X�|�[�g����Ă���K�v������B
	�]���āA.def �t�@�C�����쐬��

EXPORTS
	V2Link
	V2Unlink

	�ƋL�q���ăv���W�F�N�g�ɒǉ�����K�v������B

�����́A�̂�VC++�̂��b�B����VC++2010�ł� __declspec(dllexport) �������Ă�
�@���� .def�t�@�C�������K�v�͂Ȃ��Ȃ����B

*/
//---------------------------------------------------------------------------

