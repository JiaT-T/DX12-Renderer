#include <Windows.h>
#include "D3D12InitApp.h"
#include"ShapesApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE preInstance, PSTR cmdLine, int nShowCmd)
{
	//D3D12InitApp theApp(hInstance);
	ShapesApp shapesApp(hInstance);
#if defined(DEBUG) | defined(_DEBUG)	
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		if (!shapesApp.InitRnederItems(hInstance, nShowCmd, L""))
		{
			return 0;
		}
		return shapesApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}