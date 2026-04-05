#pragma once
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <cassert>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>

#include "d3dx12.h"
#include "GameTimer.h"
#include "ToolFunc.h"
#include"DXMath/MathHelper.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
using namespace Microsoft::WRL;
using namespace DirectX;
class D3DApp
{
protected :
	D3DApp(HINSTANCE hInstance);
    ~D3DApp();
public:
	int Run();
	bool InitWindow(HINSTANCE hInstance, int nShowCmd);
	bool InitDirect3D(HWND mhMainWnd);
	bool Init(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption);
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);
	virtual void Draw() = 0;

	static D3DApp* mApp;
	static D3DApp* GetApp() { return mApp; }
	HINSTANCE mhAppInst = nullptr;

	void CreateFoundation();
	void GetDescriptorSize();
	void SetMSAA();
	void CreateCommandObjects();
	void CreateSwapChain();
	void CreateDescriptorHeaps();
	void FlushCommandQueue();
	virtual void OnResize();
	void CreateViewPortAndScissorRect();
	void CalculateFrameState();
	virtual void Update(GameTimer& gt) {};
protected :
	ComPtr<IDXGIFactory4> dxgiFactory;
	ComPtr<ID3D12Device> md3dDevice;
	ComPtr<ID3D12Fence> mFence;
	ComPtr<ID3D12CommandQueue> mCommandQueue;
	ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
	ComPtr<ID3D12GraphicsCommandList> mCommandList;
	ComPtr<IDXGISwapChain> mSwapChain;
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	ComPtr<ID3D12Resource> mSwapChainBuffer[2];
	ComPtr<ID3D12Resource> depthStencilBuffer;

	HWND mhMainWnd = 0;
	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;
	UINT m4xMsaaQuality = 0;
	UINT mCurrentFence = 0;
	UINT mCurrentBackBuffer = 0;

	HANDLE mFenceEvent = nullptr;
	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;
	UINT clientWidth = 1280;
	UINT clientHeight = 720;

	GameTimer gameTimer;

	bool mAppPaused = false;
	bool mMinimized = false;
	bool mMaximized = false;
	bool mResizing = false;
	bool mAppInitialized = false;

	POINT mLastMousePos;
	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV4;
	float mRadius = 5.0f;

	float sunTheta = 1.25f * XM_PI;
	float sunPhi = XM_PIDIV4;
};