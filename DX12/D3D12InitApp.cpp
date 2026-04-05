#include "D3D12InitApp.h"
#pragma comment(lib, "dxguid.lib")
using namespace DirectX;
D3D12InitApp::D3D12InitApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
}
D3D12InitApp::~D3D12InitApp()
{
}

void D3D12InitApp::Draw()
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());								  //复用记录命令的相关内存
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));//重置命令列表  复用相关内存
	//将后台缓冲资源从呈现状态转换为渲染目标状态
	UINT& ref_mCurrentBackBuffer = mCurrentBackBuffer;
	//告诉GPU：我现在要往这个纹理里写数据了，别拿去显示
	auto barrierPre = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),									   // 当前后台缓冲资源
		D3D12_RESOURCE_STATE_PRESENT,												   // 转换前：显示状态
		D3D12_RESOURCE_STATE_RENDER_TARGET);									   // 转换后：渲染目标状态
	mCommandList->ResourceBarrier(1, &barrierPre);

	//设置视口与裁剪矩形
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	//清除后台缓冲区与深度缓冲区
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),									   // 堆的起始位置
		mCurrentBackBuffer,															  // 偏移索引 (0 或 1)
		mRtvDescriptorSize);														   // 每个描述符的大小
	mCommandList->ClearRenderTargetView(rtvHandle, DirectX::Colors::LightSteelBlue, 0, nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE dsvhandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
	mCommandList->ClearDepthStencilView(
		dsvhandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,											//Flag
		1.0f,																				  //默认深度值
		0,																					  //默认模板值
		0,																					//裁剪矩形数量
		nullptr);																			//裁剪矩形指针

	//指定将要渲染的缓冲区
	mCommandList->OMSetRenderTargets(
		1,																				 //待绑定的RTV数量
		&rtvHandle,																	   //指向RTV数组的指针
		true,															   //RTV对象在堆内存中是连续存放的
		&dsvhandle);																	   //指向DSV的指针

	//设置描述符堆
	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	//设置根签名
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());	

	//设置常量缓冲区视图
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->IASetVertexBuffers(0, 1, &vbv);
	mCommandList->IASetIndexBuffer(&ibv);

	//设置根描述符表
	int mObjectCBIndex = 0;
	auto handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	handle.Offset(mObjectCBIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(
		0,
		handle);

	int mPassCBIndex = 1;
	handle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	handle.Offset(mPassCBIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(
		1,
		handle);

	//绘制调用
	mCommandList->DrawIndexedInstanced(
		(UINT)indices.size(),														  //每个实例的索引数量
		1,																						//实例数量
		0,																					//起始索引位置
		0,																					  //基顶点位置
		0);																				    //起始实例位置

	////绘制调用
	//mCommandList->DrawIndexedInstanced(
	//	mDrawArgs["box"].indexCount,
	//	1,				
	//	mDrawArgs["box"].startIndexLocation,
	//	mDrawArgs["box"].baseVertexLocation,
	//	0);		  
	//mCommandList->DrawIndexedInstanced(mDrawArgs["grid"].indexCount,
	//	1,	
	//	mDrawArgs["grid"].startIndexLocation,	
	//	mDrawArgs["grid"].baseVertexLocation,	
	//	0);	
	//mCommandList->DrawIndexedInstanced(mDrawArgs["sphere"].indexCount, 
	//	1,
	//	mDrawArgs["sphere"].startIndexLocation,	
	//	mDrawArgs["sphere"].baseVertexLocation,
	//	0);
	//mCommandList->DrawIndexedInstanced(mDrawArgs["cylinder"].indexCount, 
	//	1,	
	//	mDrawArgs["cylinder"].startIndexLocation,
	//	mDrawArgs["cylinder"].baseVertexLocation,	
	//	0);

	//再次对资源进行转换 使其从渲染目标状态转换回呈现状态
	auto barrierPost = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &barrierPost);

	//完成命令的记录
	ThrowIfFailed(mCommandList->Close());

	//将待执行的命令列表加入命令队列
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };						  //申明并定义命令列表
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);	    //将命令从命令列表传入命令队列

	//交换后台缓冲区与前台缓冲区
	ThrowIfFailed(mSwapChain->Present(0, 0));
	ref_mCurrentBackBuffer = (ref_mCurrentBackBuffer + 1) % 2;
}

//窗口大小更新
void D3D12InitApp::OnResize()
{
	D3DApp::OnResize();
	//更新投影矩阵
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, static_cast<float>(clientWidth) / clientHeight, 1.0f, 1000.0f);
	ObjectConstants constant;
	XMStoreFloat4x4(&constant.World, P);
}

//创建几何体
void D3D12InitApp::BuildGeometry()
{
	//实例化顶点结构体并填充（包含8个Vertex1类型的顶点数组）
	vertices =
	{
		// --- 正面 (Front Face, Z = -1) ---
		// 0: 左下前
		Vertex1({XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)}),
		// 1: 左上前
		Vertex1({XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black)}),
		// 2: 右上前
		Vertex1({XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red)}),
		// 3: 右下前
		Vertex1({XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green)}),

		// --- 背面 (Back Face, Z = +1) ---
		// 4: 左下后
		Vertex1({XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue)}),
		// 5: 左上后
		Vertex1({XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow)}),
		// 6: 右上后
		Vertex1({XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan)}),
		// 7: 右下后
		Vertex1({XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta)})


		//Vertex1({XMFLOAT3(+0.0f, +1.0f, +0.0f), XMFLOAT4(Colors::Red)}),
		//Vertex1({XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green)}),
		//Vertex1({XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green)}),
		//Vertex1({XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Green)}),
		//Vertex1({XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Green)}),
	};
	//连接顶点，构建三角形，进行索引数据传输(顺时针连接）
	indices =
	{
		//前
		0, 1, 2,
		0, 2, 3,
		//后
		4, 6, 5,
		4, 7, 6,
		//左
		4, 5, 1,
		4, 1, 0,
		//右
		3, 2, 6,
		3, 6, 7,
		//上
		1, 5, 6,
		1, 6, 2,
		//下
		4, 0, 3,
		4, 3, 7

		////三角锥体
		//0,2,1,
		//0,3,2,
		//0,4,3,
		//0,1,4,
		//1,2,4,
		//4,2,3
	};
	totalByteSize = sizeof(vertices);
	ibByteSize = sizeof(indices);

	//在CPU内存中进行备份
	ThrowIfFailed(D3DCreateBlob(totalByteSize, &mVertexBufferCPU));					     //创建顶点数据内存空间
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mIndexBufferCPU));							 //创建索引数据内存空间
	CopyMemory(mVertexBufferCPU->GetBufferPointer(), vertices.data(), totalByteSize);			 //复制顶点数据
	CopyMemory(mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);				 //复制索引数据
	//使用之前写好的函数
	//它会自动创建uploadHeap，defaultHeap，
	//并将数据从CPU->Upload->Default
	//最后返回Default Heap资源
	mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), totalByteSize, vertices.data(), mVertexBufferUploader);
	//创建顶点缓冲区视图
	vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();							 //数据在显存的哪里
	vbv.StrideInBytes = sizeof(Vertex1);												   //每个顶点占多少字节
	vbv.SizeInBytes = totalByteSize;												   //整个缓冲区一共多少字节
	//填写索引视图
	mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSize, indices.data(), mIndexBufferUploader);
	ibv.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = ibByteSize;				  
}

//返回顶点缓冲区描述符
D3D12_VERTEX_BUFFER_VIEW D3D12InitApp::GetVBV()const
{
	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	vbv.StrideInBytes = sizeof(Vertex1);
	vbv.SizeInBytes = totalByteSize;
	return vbv;
}

//返回索引缓冲区描述符
D3D12_INDEX_BUFFER_VIEW D3D12InitApp::GetIBV()const
{
	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = ibByteSize;
	return ibv;
}

//创建常量缓冲区
void D3D12InitApp::BuildConstBuffer()
{
	UINT mObjectConstSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT mPassConstSize = CalcConstantBufferByteSize(sizeof(PassConstants));
	//创建CBV堆
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 2;								   //创建几个描述符
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;	   //着色器可见
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
	//定义物体的常量缓冲区
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
		md3dDevice.Get(),
		1,												 //存储着几个物体的常量数据
		true);
	//获取常量缓冲区首地址
	D3D12_GPU_VIRTUAL_ADDRESS objCB_address;							//第一个CBV
	objCB_address = mObjectCB->GetResource()->GetGPUVirtualAddress();
	//通过偏移量计算出第一个物体常量数据的位置
	int boxCBufIndex = 0;							 //常量缓冲区子物体数量下标
	objCB_address += boxCBufIndex * mObjectConstSize;
	//获取CBV堆首地址
	auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
	//CBV堆中的CBV元素地址
	handle.Offset(boxCBufIndex, mCbvSrvUavDescriptorSize);
	//创建CBV描述符
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = objCB_address;
	cbvDesc.SizeInBytes = mObjectConstSize;
	md3dDevice->CreateConstantBufferView(&cbvDesc, handle);

	mPassCB = std::make_unique<UploadBuffer<PassConstants>>(
		md3dDevice.Get(),
		1,
		true);
	D3D12_GPU_VIRTUAL_ADDRESS passCB_address;							 //第二个CBV
	passCB_address = mPassCB->GetResource()->GetGPUVirtualAddress();
	int passCB_index = 0;
	passCB_address += passCB_index * mPassConstSize;
	boxCBufIndex++;
	handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
	handle.Offset(boxCBufIndex, mCbvSrvUavDescriptorSize);
	//创建第二个CBV描述符	
	D3D12_CONSTANT_BUFFER_VIEW_DESC passCBVDesc = {};
	passCBVDesc.BufferLocation = passCB_address;
	passCBVDesc.SizeInBytes = mPassConstSize;	
	md3dDevice->CreateConstantBufferView(&passCBVDesc, handle);
}

//创建根签名
void D3D12InitApp::BuildRootSignature()
{
	//创建根参数（它可以是根常量、根描述符、描述符表）
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	//创建只有一个CBV的描述符表
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_CBV,												//描述符类型
		1,																				//描述符数量
		0);																	//描述符绑定的寄存器槽号
	slotRootParameter[0].InitAsDescriptorTable(
		1,																			//描述符堆的数量
		&cbvTable0);															//指向描述符堆数组的指针

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_CBV,												//描述符类型
		1,																				//描述符数量
		1);																	//描述符绑定的寄存器槽号
	slotRootParameter[1].InitAsDescriptorTable(
		1,																			//描述符堆的数量
		&cbvTable1);														//指向描述符堆数组的指针
	//根签名由一组根参数组成
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
		2,																				//根参数数量
		slotRootParameter,															//根参数数组指针
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	//创建仅含一个槽位的根签名
	//该槽位指向一个只包含单个常量缓冲区的描述符区域
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	//序列化根签名
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&serializedRootSig,
		&errorBlob);
	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	//创建根签名
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

//读取着色器文件与输入布局描述
void D3D12InitApp::BuildByteCodeAndInputLayout()
{
	vsBytecode = CompileShader(
		L"Shaders\\Color.hlsl",		 //着色器文件路径
		nullptr,							 //宏定义
		"VS",						   //入口函数名称
		"vs_5_0");					 //着色器模型版本
	psBytecode = CompileShader(
		L"Shaders\\Color.hlsl",		 //着色器文件路径
		nullptr,							 //宏定义
		"PS",						   //入口函数名称
		"ps_5_0");					 //着色器模型版本

	//Vector1的输入布局描述
	inputLayoutDesc1 =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

//构建流水线状态对象(PSO):将之前定义的顶点布局描述、着色器字节码、光栅器状态、根签名等对象绑定到图形流水线上
void D3D12InitApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { inputLayoutDesc1.data(), (UINT)inputLayoutDesc1.size() };			  //顶点布局描述
	psoDesc.pRootSignature = mRootSignature.Get();														//根签名
	//挂载着色器
	psoDesc.VS = {reinterpret_cast<BYTE*>(
		vsBytecode->GetBufferPointer()),
		vsBytecode->GetBufferSize()};
	psoDesc.PS = { reinterpret_cast<BYTE*>(
		psBytecode->GetBufferPointer()),
		psBytecode->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);								//光栅器状态
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);											  //混合状态
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);						  //深度模板状态	
	psoDesc.SampleMask = UINT_MAX;																	  //采样掩码
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;							  //图元类型
	psoDesc.NumRenderTargets = 1;																  //渲染目标数量
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;											  //渲染目标格式
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;											  //深度模板格式
	psoDesc.SampleDesc.Count = 1;																	  //采样数量
	psoDesc.SampleDesc.Quality = 0;																	  //采样质量
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

bool D3D12InitApp::Init(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption)
{
	if (!D3DApp::Init(hInstance, nShowCmd, customCaption))
		return false;
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildConstBuffer();
	BuildRootSignature();
	BuildByteCodeAndInputLayout();
	BuildPSO();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	mAppInitialized = true;

	return true;
}

//每帧输入更新
void D3D12InitApp::Update(GameTimer& gt)
{
	ObjectConstants objConstants;	
	PassConstants passConstants;
	//构建观察矩阵
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float y = mRadius * cosf(mPhi);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);

	//构建投影矩阵
	XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * 3.1416f, 1280.0f / 720.0f, 1.0f, 1000.0f);	
	//构建世界矩阵
	XMMATRIX world = XMMatrixIdentity();
	//world *= XMMatrixTranslation(2.0f, 0.0f, 0.0f);
	//矩阵计算
	XMMATRIX VP_Matrix = view * proj;

	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(VP_Matrix));
	XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
	//获取时间数据
	objConstants.gTime = gameTimer.TotalTime();
	//将数据拷贝到GPU缓冲区
	mObjectCB->CopyData(0, objConstants);
	mPassCB->CopyData(0, passConstants);
}
