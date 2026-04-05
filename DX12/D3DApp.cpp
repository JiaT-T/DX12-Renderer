#include "ToolFunc.h"
//#include "D3DApp.h"
#include"ShapesApp.h"

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif
D3DApp* ShapesApp::mApp = nullptr;

D3DApp::D3DApp(HINSTANCE hInstance) : mhAppInst(hInstance)
{
	assert(mApp == nullptr);
	mApp = this;
}
D3DApp::~D3DApp() 
{
	if (md3dDevice != nullptr)
	{
		// 只有当 GPU 闲下来了，才能安全地让 ComPtr 析构释放显存
		FlushCommandQueue();
	}
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

//窗口初始化函数
LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{	
	//消息处理 
	switch (msg)
	{
		//鼠标按下时的情况
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
			OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		//鼠标抬起时的情况
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
			OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		//鼠标移动时的情况
		case WM_MOUSEMOVE:
			OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			return 0;
		//窗口大小改变时的情况
		case WM_SIZE:
			clientWidth = LOWORD(lParam);
			clientHeight = HIWORD(lParam);
			if (md3dDevice && mAppInitialized)
			{
				//
				if (wParam == SIZE_MINIMIZED)
				{
					//最小化
					mAppPaused = true;
					mMinimized = true;
					mMaximized = false;
				}
				else if (wParam == SIZE_MAXIMIZED)
				{
					//最大化
					mAppPaused = false;
					mMinimized = false;
					mMaximized = true;
					OnResize();
				}
				else if (wParam == SIZE_RESTORED)
				{
					if (mMinimized)
					{
						mAppPaused = false;
						mMinimized = false;
						OnResize();
					}
					else if (mMaximized)
					{
						mAppPaused = false;
						mMaximized = false;
						OnResize();

					}
					else if (mResizing)
					{
						//正在调整大小
					}
					else
					{
						OnResize();
					}
				}
			}
			return 0;
		//窗口销毁时的情况（终止消息循环）
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*******d3d12初始化函数******/
bool D3DApp::InitDirect3D(HWND mhMainWnd)
{
	//启用调试层
	#if defined(DEBUG) || defined(_DEBUG)
		{
			ComPtr<ID3D12Debug> debugController;
			ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
			debugController->EnableDebugLayer();
		}
	#endif

	this->mhMainWnd = mhMainWnd;

	CreateFoundation();

	GetDescriptorSize();

	SetMSAA();

	CreateCommandObjects();

	CreateSwapChain();

	CreateDescriptorHeaps();

	OnResize();

	return true;
}

//工厂、设备、围栏的创建
void D3DApp::CreateFoundation()
{
	//创建工厂
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));
	//创建设备
	ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&md3dDevice)));
	//创建围栏
	ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
}

//获取描述符大小
void D3DApp::GetDescriptorSize()
{
	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

//设置MSAA抗锯齿属性
void D3DApp::SetMSAA()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;									 //后台缓冲区格式
	msQualityLevels.SampleCount = 4;											  //采样倍率(等于1时默认关闭)
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 4;													 //抗锯齿质量等级
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));
	m4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m4xMsaaQuality > 0);
}

//创建命令队列和命令列表
void D3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mDirectCmdListAlloc)));
	ThrowIfFailed(md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mDirectCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(&mCommandList)));
	mCommandList->Close();
}

//创建交换链
void D3DApp::CreateSwapChain()
{
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = 1280;														     //缓冲区分辨率的宽度
	sd.BufferDesc.Height = 720;														     //缓冲区分辨率的高度
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;									   //缓冲区的显示格式
	sd.BufferDesc.RefreshRate.Denominator = 1;												   //刷新率的分子
	sd.BufferDesc.RefreshRate.Numerator = 60;												   //刷新率的分母
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;					   //逐行扫描VS隔行扫描(未指定的)
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;    //图像相对屏幕的拉伸（未指定的）
	sd.SampleDesc.Count = 1;																   //多重采样数量
	sd.SampleDesc.Quality = 0;																   //多重采样质量
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;			    //将数据渲染至后台缓冲区（即作为渲染目标）
	sd.BufferCount = 2;														        //后台缓冲区数量（双缓冲）
	sd.Windowed = true;																			 //是否窗口化
	sd.OutputWindow = mhMainWnd;															   //渲染窗口句柄
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;												   //固定写法
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;//自适应窗口模式（自动选择最适于当前窗口尺寸的显示模式）

	ThrowIfFailed(dxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSwapChain.GetAddressOf()));
}

//创建描述符堆
void D3DApp::CreateDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = 2;														  // 对应 BufferCount
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;					  // RTV 堆通常不需要 Shader 可见
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;								// 这是一个渲染目标视图堆
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));
}

//调整窗口大小时调用
void D3DApp::OnResize()
{
	assert(md3dDevice);
	assert(mSwapChain);
	assert(mDirectCmdListAlloc);

	// 1. 既然要调整大小，先重置命令列表，准备录制初始化指令
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// 释放之前的资源
	for (int i = 0; i < 2; ++i)
	{
		mSwapChainBuffer[i].Reset();
	}
	depthStencilBuffer.Reset();

	// 重设交换链大小
	ThrowIfFailed(mSwapChain->ResizeBuffers(2, clientWidth, clientHeight, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
	mCurrentBackBuffer = 0;

	// 创建 RTV 描述符
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < 2; i++)
	{
		mSwapChain->GetBuffer(i, IID_PPV_ARGS(mSwapChainBuffer[i].GetAddressOf()));
		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	// 创建深度/模板缓冲区描述
	D3D12_RESOURCE_DESC dsvStencilDesc;
	dsvStencilDesc.Alignment = 0;
	dsvStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsvStencilDesc.DepthOrArraySize = 1;
	dsvStencilDesc.Width = clientWidth; // 使用 clientWidth 而不是硬编码
	dsvStencilDesc.Height = clientHeight; // 使用 clientHeight
	dsvStencilDesc.MipLevels = 1;
	dsvStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	dsvStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	dsvStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvStencilDesc.SampleDesc.Count = 1;
	dsvStencilDesc.SampleDesc.Quality = 0;

	CD3DX12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&dsvStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(&depthStencilBuffer)));

	// 创建 DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

	// 资源屏障转换
	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
		depthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->ResourceBarrier(1, &transition);

	// 2. 必须关闭命令列表
	ThrowIfFailed(mCommandList->Close());

	// 3. 执行并刷新
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();

	// 4. 设置视口 (不需要命令列表)
	CreateViewPortAndScissorRect();
}

//创建视口和裁剪矩形
void D3DApp::CreateViewPortAndScissorRect()
{
	//视口设置
	mScreenViewport.TopLeftX = 0.0f;
	mScreenViewport.TopLeftY = 0.0f;
	mScreenViewport.Width = static_cast<float>(clientWidth);
	mScreenViewport.Height = static_cast<float>(clientHeight);
	mScreenViewport.MaxDepth = 1.0f;
	mScreenViewport.MinDepth = 0.0f;

	//裁剪矩形   前两个为左上点坐标，后两个为右下点坐标
	mScissorRect.left = 0;
	mScissorRect.top = 0;
	mScissorRect.right = static_cast<float>(clientWidth);
	mScissorRect.bottom = static_cast<float>(clientHeight);
}

//计算每秒帧数和每帧耗时
void D3DApp::CalculateFrameState()
{
	//使用static是为了确保这一帧的函数进程结束之后
	//数值能够被保留，在下一帧继续使用
	static int frameCnt = 0;														 //总帧数
	static float timeElapsed = 0.0f;											 //经过的时间

	frameCnt++;											//在一秒的时间内，每画一帧，总帧数加一
	
	if ((gameTimer.TotalTime() - timeElapsed) >= 1.0f)
	{
		float fps = (float)frameCnt;											  //每秒的帧数
		float mspf = 1000.0f / fps;										//渲染每帧花费多少毫秒

		std::wstring fpsStr = std::to_wstring(fps);
		std::wstring mspfStr = std::to_wstring(mspf);

		std::wstring windowText = L"D3D12Init    fps:" + fpsStr + L"    mspf" + mspfStr;
		SetWindowText(mhMainWnd, windowText.c_str());
		//为计算下一组帧数而进行重置
		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}

void D3DApp::FlushCommandQueue()
{
	mCurrentFence++;	//CPU传完命令并关闭后，将当前围栏值+1
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);	//当GPU处理完CPU传入的命令后，将fence接口中的围栏值+1，即fence->GetCompletedValue()+1
	if (mFence->GetCompletedValue() < mCurrentFence)	//如果小于，说明GPU没有处理完所有命令
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");	//创建事件
		mFence->SetEventOnCompletion(mCurrentFence, eventHandle);//当围栏达到mCurrentFence值（即执行到Signal（）指令修改了围栏值）时触发的eventHandle事件
		WaitForSingleObject(eventHandle, INFINITE);//等待GPU命中围栏，激发事件（阻塞当前线程直到事件触发，注意此Enent需先设置再等待，
		//如果没有Set就Wait，就死锁了，Set永远不会调用，所以也就没线程可以唤醒这个线程）
		CloseHandle(eventHandle);
	}
}

//窗口初始化
bool D3DApp::InitWindow(HINSTANCE hInstance, int nShowCmd)
{
	WNDCLASS wc = { 0 };
	wc.style = CS_HREDRAW | CS_VREDRAW;							   //当窗口宽高改变时，重新渲染画面
	wc.lpfnWndProc = MainWndProc;		//将“点击鼠标”，“按下键盘”等操作传入MainWndProc函数处理
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;									   //让系统知道是哪个程序在创建窗口
	wc.hIcon = LoadIcon(0, IDC_ARROW);							    //指定应用图标样式（此处为默认）
	wc.hCursor = LoadCursor(0, IDC_ARROW);												 //设置光标
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);				//指定背景颜色（此处为白色）
	wc.lpszClassName = L"MainWnd";
	wc.lpszMenuName = 0;																   //窗口名
	//检测窗口是否创建成功   如果不是，抛出报错后直接终止程序
	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass failed", 0, 0);
		return 0;
	}
	//裁剪矩形
	RECT R;
	R.left = 0;
	R.top = 0;
	R.right = 1280;
	R.bottom = 720;
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);							//计算窗口的边框大小
	int width = R.right - R.left;														  //实际宽度
	int hight = R.bottom - R.top;														  //实际高度
	//创建窗口
	mhMainWnd = CreateWindow(L"MainWnd", L"DX12Initialize", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, hight, 0, 0, hInstance, 0);
	//检测窗口创建是否成功
	if (!mhMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed", 0, 0);
		return 0;
	}
	//显示并更新窗口
	ShowWindow(mhMainWnd, nShowCmd);
	UpdateWindow(mhMainWnd);
	return true;
}

//主循环
int D3DApp::Run()
{
	MSG msg = { 0 };																	  //定义消息结构体
	//每次循环开始前都要先重置计时器
	gameTimer.Reset();
	while (msg.message != WM_QUIT)						//如果GetMessage函数不等于0，说明没有接受到WM_QUIT
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))								//如果有窗口消息就进行处理
		{
			TranslateMessage(&msg);								//键盘按键转换，将虚拟键消息转换为字符消息
			DispatchMessage(&msg);											  //把消息分派给相应的窗口过程

		}
		else																	//否则就执行动画和游戏逻辑
		{
			gameTimer.Tick();															//计算两帧间隔时间
			if (!gameTimer.IsStoped())										//只有在非暂停状态下才运行游戏
			{
				CalculateFrameState();
				Update(gameTimer);
				Draw();
			}
			else
			{
				Sleep(100);													//如果处于暂停状态，就休眠100s
			}
		}
	}
	return (int)msg.wParam;
}

//检测窗口与D3D12是否初始化成功
bool D3DApp::Init(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption)
{
	if (!InitWindow(hInstance, nShowCmd))
	{
		return false;
	}
	else if (!InitDirect3D(mhMainWnd))
	{
		return false;
	}
	OnResize();
	return true;
}

//窗口过程函数
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return D3DApp::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
}

//处理鼠标按下
void D3DApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;						 //鼠标按下时记录x分量
	mLastMousePos.y = y;						 //鼠标按下时记录y分量	
	SetCapture(mhMainWnd);								    //捕获鼠标
}

void D3DApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();						  //按键抬起后释放鼠标捕获
}

void D3DApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)		  //按下左键
	{
		//根据鼠标移动的距离计算旋转角度，并让每个像素按照此角度的0.25倍进行旋转
		float dx = XMConvertToRadians(0.25f * static_cast<float>(mLastMousePos.x - x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(mLastMousePos.y - y));
		//更新摄像机参数
		mTheta += dx;
		mPhi += dy;
		//限制mPhi的范围在0.1到PI-0.1之间
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)	 //按下右键
	{	
		//每个像素按照鼠标移动距离0.005倍进行缩放
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx + dy;
		mRadius = MathHelper::Clamp(mRadius, 3.0f, 25.0f);
	}
	mLastMousePos.x = x;						 //更新鼠标位置x分量
	mLastMousePos.y = y;						 //更新鼠标位置y分量
}
