//---------------------------------------------------------------------------
#include <windows.h>

#include "tp_stub.h"
	// tp_stub.h と tp_stub.cpp は必ずプロジェクトに追加する

#include "blur.h"
#include "dim.h"


//---------------------------------------------------------------------------
//#pragma argsused
int WINAPI DllEntryPoint(HINSTANCE hinst, unsigned long reason, void* lpReserved)
{
	// DLL エントリポイント
	return 1;
}
//---------------------------------------------------------------------------
// V2Link は DLL がリンクされるときに実行される
extern "C" HRESULT __declspec(dllexport) V2Link(iTVPFunctionExporter *exporter)
{
	// スタブの初期化
	TVPInitImportStub(exporter); // 必ずこれは記述
	/*
		TVPInitImpotStub を実行した後は吉里吉里内部の各関数や tTJSVariant 等の
		基本的な型が使用可能になる。
	*/

	// トランジションハンドラプロバイダの登録
	RegisterBlurTransHandlerProvider();
	RegisterDimTransHandlerProvider();

	return S_OK;
}
//---------------------------------------------------------------------------
// V2Unlink は DLL がアンリンクされるときに実行される
extern "C" HRESULT __declspec(dllexport) V2Unlink()
{
	// トランジションハンドラプロバイダの登録削除
	UnregisterBlurTransHandlerProvider();
	UnregisterDimTransHandlerProvider();

	// スタブの使用終了
	TVPUninitImportStub(); // 必ずこれは記述
	/*
		TVPUninitImpotStub は TVPInitImportStub で確保したメモリを解放する
		ので必ず記述する。
	*/
	return S_OK;
}
//---------------------------------------------------------------------------
/*
	V2Link と V2Unlink は DLL からエクスポートされている必要がある。
	従って、.def ファイルを作成し

EXPORTS
	V2Link
	V2Unlink

	と記述してプロジェクトに追加する必要がある。

※↑は、昔のVC++のお話。今のVC++2010では __declspec(dllexport) くっつけてる
　から .defファイルを作る必要はなくなった。

*/
//---------------------------------------------------------------------------

