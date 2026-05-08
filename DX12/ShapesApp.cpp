#define TINYOBJLOADER_IMPLEMENTATION
#include "third_party/tiny_obj_loader.h"

#include "ShapesApp.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

using namespace DirectX;
using namespace Microsoft::WRL;

namespace
{
	XMFLOAT4 ComputeFallbackTangent(const XMFLOAT3& normal)
	{
		const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
		const XMVECTOR up = fabsf(normal.y) < 0.999f
			? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
			: XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		const XMVECTOR tangent = XMVector3Normalize(XMVector3Cross(up, n));

		XMFLOAT3 tangent3;
		XMStoreFloat3(&tangent3, tangent);
		return XMFLOAT4(tangent3.x, tangent3.y, tangent3.z, 1.0f);
	}

	XMFLOAT4 ComputeTriangleTangent(
		const XMFLOAT3& p0,
		const XMFLOAT3& p1,
		const XMFLOAT3& p2,
		const XMFLOAT2& uv0,
		const XMFLOAT2& uv1,
		const XMFLOAT2& uv2,
		const XMFLOAT3& normal)
	{
		const XMVECTOR P0 = XMLoadFloat3(&p0);
		const XMVECTOR P1 = XMLoadFloat3(&p1);
		const XMVECTOR P2 = XMLoadFloat3(&p2);

		const XMFLOAT3 edge1 = XMFLOAT3(p1.x - p0.x, p1.y - p0.y, p1.z - p0.z);
		const XMFLOAT3 edge2 = XMFLOAT3(p2.x - p0.x, p2.y - p0.y, p2.z - p0.z);
		const float du1 = uv1.x - uv0.x;
		const float dv1 = uv1.y - uv0.y;
		const float du2 = uv2.x - uv0.x;
		const float dv2 = uv2.y - uv0.y;
		const float det = du1 * dv2 - dv1 * du2;

		if (fabsf(det) < 1e-6f)
		{
			return ComputeFallbackTangent(normal);
		}

		const float invDet = 1.0f / det;
		XMFLOAT3 tangent = {
			(edge1.x * dv2 - edge2.x * dv1) * invDet,
			(edge1.y * dv2 - edge2.y * dv1) * invDet,
			(edge1.z * dv2 - edge2.z * dv1) * invDet
		};
		XMFLOAT3 bitangent = {
			(edge2.x * du1 - edge1.x * du2) * invDet,
			(edge2.y * du1 - edge1.y * du2) * invDet,
			(edge2.z * du1 - edge1.z * du2) * invDet
		};

		XMVECTOR tangentV = XMVector3Normalize(XMLoadFloat3(&tangent));
		const XMVECTOR normalV = XMVector3Normalize(XMLoadFloat3(&normal));
		const XMVECTOR bitangentV = XMLoadFloat3(&bitangent);
		tangentV = XMVector3Normalize(tangentV - XMVector3Dot(tangentV, normalV) * normalV);

		XMFLOAT3 tangent3;
		XMStoreFloat3(&tangent3, tangentV);
		const float handedness = XMVectorGetX(XMVector3Dot(XMVector3Cross(normalV, tangentV), bitangentV)) < 0.0f ? -1.0f : 1.0f;
		return XMFLOAT4(tangent3.x, tangent3.y, tangent3.z, handedness);
	}
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

bool ShapesApp::InitRnederItems(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption)
{
	if (!D3DApp::Init(hInstance, nShowCmd, customCaption))
	{
		return false;
	}

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mTheta = 1.45f * XM_PI;
	mPhi = 0.35f * XM_PI;
	mRadius = 14.0f;
	sunTheta = 1.15f * XM_PI;
	sunPhi = 0.18f * XM_PI;

	BuildRootSignature();
	BuildByteCodeAndInputLayout();
	BuildShapeGeometry();
	BuildSkull();
	BuildBoxGeometry();
	BuildGridGeomerty();
	BuildTreeBillboardGeometry();
	LoadTextures();
	BuildMaterial();
	BuildImportedPbrSphere();
	BuildRenderItems();
	BuildFrameResource();

	mCurrentFrameResourcesIndex = 0;
	mCurrentFrameResources = mFrameResourcesArray[mCurrentFrameResourcesIndex].get();

	BuildShaderResourceView();
	BuildPSO();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
	FlushCommandQueue();

	mAppInitialized = true;
	return true;
}

void ShapesApp::Draw()
{
	auto currentCmdAllocator = mCurrentFrameResources->mCmdAllocator;
	ThrowIfFailed(currentCmdAllocator->Reset());
	ThrowIfFailed(mCommandList->Reset(currentCmdAllocator.Get(), mPSOs["opaque"].Get()));

	auto barrierToRender = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRender);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrentBackBuffer,
		mRtvDescriptorSize);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	const float clearColor[] = { 0.08f, 0.10f, 0.14f, 1.0f };
	mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);

	mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	const UINT passCBByteSize = CalcConstantBufferByteSize(sizeof(PassConstants));
	const D3D12_GPU_VIRTUAL_ADDRESS passCBAddress =
		mCurrentFrameResources->passCB->GetResource()->GetGPUVirtualAddress();
	const D3D12_GPU_VIRTUAL_ADDRESS reflectPassCBAddress = passCBAddress + passCBByteSize;

	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["treeBillboard"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::AlphaTest]);

	mCommandList->OMSetStencilRef(1);
	mCommandList->SetPipelineState(mPSOs["markStencilMirror"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Mirrors]);

	mCommandList->SetGraphicsRootConstantBufferView(2, reflectPassCBAddress);
	mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Reflect]);

	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
	mCommandList->OMSetStencilRef(0);
	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Transparent]);

	mCommandList->SetPipelineState(mPSOs["shadow"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Shadow]);

	auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &barrierToPresent);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrentBackBuffer = (mCurrentBackBuffer + 1) % 2;

	mCurrentFrameResources->mFenceCPU = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
{
	const UINT objConstSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	const UINT matConstSize = CalcConstantBufferByteSize(sizeof(MatConstants));

	auto makeSrvHandle = [this](int srvIndex)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		handle.Offset(srvIndex, mCbvSrvUavDescriptorSize);
		return handle;
	};

	for (RenderItem* ritem : ritems)
	{
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
		D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

		if (ritem->geo != nullptr)
		{
			vertexBufferView.BufferLocation = ritem->geo->mVertexBufferGPU->GetGPUVirtualAddress();
			vertexBufferView.StrideInBytes = ritem->geo->mVertexByteStride;
			vertexBufferView.SizeInBytes = ritem->geo->mVertexBufferByteSize;

			indexBufferView.BufferLocation = ritem->geo->mIndexBufferGPU->GetGPUVirtualAddress();
			indexBufferView.Format = ritem->geo->mIndexFormat;
			indexBufferView.SizeInBytes = ritem->geo->mIndexBufferByteSize;
		}
		else
		{
			vertexBufferView = GetVBV();
			indexBufferView = GetIBV();
		}

		mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		mCommandList->IASetIndexBuffer(&indexBufferView);
		mCommandList->IASetPrimitiveTopology(ritem->mPrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = mCurrentFrameResources->objCB->GetResource()->GetGPUVirtualAddress();
		objCBAddress += static_cast<UINT64>(ritem->mObjCBIndex) * objConstSize;
		mCommandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		if (ritem->mat != nullptr)
		{
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = mCurrentFrameResources->matCB->GetResource()->GetGPUVirtualAddress();
			matCBAddress += static_cast<UINT64>(ritem->mat->matCBIndex) * matConstSize;
			mCommandList->SetGraphicsRootConstantBufferView(1, matCBAddress);

			mCommandList->SetGraphicsRootDescriptorTable(3, makeSrvHandle(ritem->mat->baseColorSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(4, makeSrvHandle(ritem->mat->normalSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(5, makeSrvHandle(ritem->mat->roughnessSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(6, makeSrvHandle(ritem->mat->metallicSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(7, makeSrvHandle(textures[mEnvironmentTextureName]->srvHeapIndex));
		}

		mCommandList->DrawIndexedInstanced(
			ritem->indexCount,
			1,
			ritem->startIndexLocation,
			ritem->baseVertexLocation,
			0);
	}
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();
	XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, static_cast<float>(clientWidth) / clientHeight, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, proj);
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER rootParameters[8];
	rootParameters[0].InitAsConstantBufferView(0);
	rootParameters[1].InitAsConstantBufferView(1);
	rootParameters[2].InitAsConstantBufferView(2);

	CD3DX12_DESCRIPTOR_RANGE baseColorSrvTable;
	CD3DX12_DESCRIPTOR_RANGE normalSrvTable;
	CD3DX12_DESCRIPTOR_RANGE roughnessSrvTable;
	CD3DX12_DESCRIPTOR_RANGE metallicSrvTable;
	CD3DX12_DESCRIPTOR_RANGE envSrvTable;

	baseColorSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	normalSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	roughnessSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	metallicSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
	envSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	rootParameters[3].InitAsDescriptorTable(1, &baseColorSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[4].InitAsDescriptorTable(1, &normalSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[5].InitAsDescriptorTable(1, &roughnessSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[6].InitAsDescriptorTable(1, &metallicSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[7].InitAsDescriptorTable(1, &envSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
		_countof(rootParameters),
		rootParameters,
		static_cast<UINT>(staticSamplers.size()),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&serializedRootSignature,
		&errorBlob);
	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSignature->GetBufferPointer(),
		serializedRootSignature->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void ShapesApp::BuildByteCodeAndInputLayout()
{
	vsBytecode = CompileShader(L"Shaders\\Color.hlsl", nullptr, "VS", "vs_5_0");
	psBytecode = CompileShader(L"Shaders\\Color.hlsl", nullptr, "PS", "ps_5_0");

	inputLayoutDesc1 =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	shaders["treeBillboardVS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "VS", "vs_5_0");
	shaders["treeBillboardGS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "GS", "gs_5_0");
	shaders["treeBillboardPS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "PS", "ps_5_0");

	treeBillboardInputLayoutDesc =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void ShapesApp::BuildShaderResourceView()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = mNextSrvHeapIndex;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

	for (auto& [name, texture] : textures)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texture->srvFormat == DXGI_FORMAT_UNKNOWN ? texture->resource->GetDesc().Format : texture->srvFormat;

		if (texture->isCubeMap)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = texture->resource->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
		}
		else
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = texture->resource->GetDesc().MipLevels;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			texture->srvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(texture->resource.Get(), &srvDesc, handle);
	}
}

void ShapesApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputLayoutDesc1.data(), static_cast<UINT>(inputLayoutDesc1.size()) };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS = { reinterpret_cast<BYTE*>(vsBytecode->GetBufferPointer()), vsBytecode->GetBufferSize() };
	psoDesc.PS = { reinterpret_cast<BYTE*>(psBytecode->GetBufferPointer()), psBytecode->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDSS = {};
	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;
	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	mirrorDSS.BackFace = mirrorDSS.FrontFace;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorPsoDesc = psoDesc;
	markMirrorPsoDesc.BlendState = mirrorBlendState;
	markMirrorPsoDesc.DepthStencilState = mirrorDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorPsoDesc, IID_PPV_ARGS(&mPSOs["markStencilMirror"])));

	D3D12_DEPTH_STENCIL_DESC reflectionDSS = {};
	reflectionDSS.DepthEnable = true;
	reflectionDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionDSS.StencilEnable = true;
	reflectionDSS.StencilReadMask = 0xff;
	reflectionDSS.StencilWriteMask = 0xff;
	reflectionDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	reflectionDSS.BackFace = reflectionDSS.FrontFace;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC reflectionPsoDesc = psoDesc;
	reflectionPsoDesc.DepthStencilState = reflectionDSS;
	reflectionPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	reflectionPsoDesc.RasterizerState.FrontCounterClockwise = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&reflectionPsoDesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

	D3D12_RENDER_TARGET_BLEND_DESC transparentBlendState = {};
	transparentBlendState.BlendEnable = true;
	transparentBlendState.LogicOpEnable = false;
	transparentBlendState.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparentBlendState.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparentBlendState.BlendOp = D3D12_BLEND_OP_ADD;
	transparentBlendState.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentBlendState.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparentBlendState.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentBlendState.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparentBlendState.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = psoDesc;
	transparentPsoDesc.BlendState.RenderTarget[0] = transparentBlendState;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	D3D12_DEPTH_STENCIL_DESC shadowDSS = {};
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;
	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	shadowDSS.BackFace = shadowDSS.FrontFace;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDSS;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeBillboardPsoDesc = psoDesc;
	treeBillboardPsoDesc.InputLayout = { treeBillboardInputLayoutDesc.data(), static_cast<UINT>(treeBillboardInputLayoutDesc.size()) };
	treeBillboardPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["treeBillboardVS"]->GetBufferPointer()), shaders["treeBillboardVS"]->GetBufferSize() };
	treeBillboardPsoDesc.GS = { reinterpret_cast<BYTE*>(shaders["treeBillboardGS"]->GetBufferPointer()), shaders["treeBillboardGS"]->GetBufferSize() };
	treeBillboardPsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["treeBillboardPS"]->GetBufferPointer()), shaders["treeBillboardPS"]->GetBufferSize() };
	treeBillboardPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeBillboardPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeBillboardPsoDesc, IID_PPV_ARGS(&mPSOs["treeBillboard"])));
}

void ShapesApp::BuildShapeGeometry()
{
	MeshData box = CreateBox(1.5f, 0.5f, 1.5f, 3);
	MeshData grid = CreateGrid(20.0f, 30.0f, 60, 40);
	MeshData sphere = CreateSphere(0.5f, 20, 20);
	MeshData cylinder = CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	const UINT boxVertexOffset = 0;
	const UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
	const UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	const UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());

	const UINT boxIndexOffset = 0;
	const UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	const UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	const UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());

	mDrawArgs["box"] = { static_cast<UINT>(box.Indices32.size()), boxVertexOffset, boxIndexOffset };
	mDrawArgs["grid"] = { static_cast<UINT>(grid.Indices32.size()), gridVertexOffset, gridIndexOffset };
	mDrawArgs["sphere"] = { static_cast<UINT>(sphere.Indices32.size()), sphereVertexOffset, sphereIndexOffset };
	mDrawArgs["cylinder"] = { static_cast<UINT>(cylinder.Indices32.size()), cylinderVertexOffset, cylinderIndexOffset };

	const size_t totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
	std::vector<Vertex1> vertices(totalVertexCount);
	size_t vertexCursor = 0;

	auto appendVertices = [&vertices, &vertexCursor](const MeshData& mesh)
	{
		for (const auto& srcVertex : mesh.Vertices)
		{
			Vertex1 dstVertex = {};
			dstVertex.Pos = srcVertex.Position;
			dstVertex.Normal = srcVertex.Normal;
			dstVertex.TangentU = XMFLOAT4(srcVertex.TangentU.x, srcVertex.TangentU.y, srcVertex.TangentU.z, 1.0f);
			dstVertex.TexC = srcVertex.TexC;
			vertices[vertexCursor++] = dstVertex;
		}
	};

	appendVertices(box);
	appendVertices(grid);
	appendVertices(sphere);
	appendVertices(cylinder);

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
	totalByteSize = vbByteSize;
	ibByteSize = ibByteSizeLocal;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mVertexBufferCPU));
	CopyMemory(mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &mIndexBufferCPU));
	CopyMemory(mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), mVertexBufferUploader);
	mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), mIndexBufferUploader);

	vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	vbv.StrideInBytes = sizeof(Vertex1);
	vbv.SizeInBytes = vbByteSize;

	ibv.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = ibByteSizeLocal;
}

void ShapesApp::BuildSkull()
{
	std::ifstream fin("Models/skull.txt");
	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found", 0, 0);
		return;
	}

	UINT vertexCount = 0;
	UINT triangleCount = 0;
	std::string ignore;

	fin >> ignore >> vertexCount;
	fin >> ignore >> triangleCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex1> vertices(vertexCount);
	for (UINT i = 0; i < vertexCount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
		vertices[i].TangentU = ComputeFallbackTangent(vertices[i].Normal);
		vertices[i].TexC = XMFLOAT2(0.0f, 0.0f);
	}

	fin >> ignore >> ignore >> ignore;
	std::vector<std::uint32_t> indices(triangleCount * 3);
	for (UINT i = 0; i < triangleCount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}
	fin.close();

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "skullGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R32_UINT;
	geometry->mDrawArgs["skull"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["skullGeo"] = std::move(geometry);
}

void ShapesApp::BuildBoxGeometry()
{
	MeshData box = CreateBox(8.0f, 8.0f, 8.0f, 3);
	std::vector<Vertex1> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TangentU = XMFLOAT4(box.Vertices[i].TangentU.x, box.Vertices[i].TangentU.y, box.Vertices[i].TangentU.z, 1.0f);
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "boxGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["woodBox"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["boxGeo"] = std::move(geometry);
}

void ShapesApp::BuildGridGeomerty()
{
	MeshData grid = CreateGrid(8.0f, 8.0f, 8, 3);
	std::vector<Vertex1> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Normal = grid.Vertices[i].Normal;
		vertices[i].TangentU = XMFLOAT4(grid.Vertices[i].TangentU.x, grid.Vertices[i].TangentU.y, grid.Vertices[i].TangentU.z, 1.0f);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "gridGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["mirrorGrid"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["gridGeo"] = std::move(geometry);
}

void ShapesApp::BuildTreeBillboardGeometry()
{
	struct BillboardVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	const UINT treeCount = 16;
	std::array<BillboardVertex, treeCount> vertices = {};
	std::array<std::uint16_t, treeCount> indices = {};

	for (UINT i = 0; i < treeCount; ++i)
	{
		vertices[i].Pos = XMFLOAT3(MathHelper::RandF(-45.0f, 45.0f), 4.5f, MathHelper::RandF(-45.0f, 45.0f));
		vertices[i].Size = XMFLOAT2(10.0f, 10.0f);
		indices[i] = static_cast<std::uint16_t>(i);
	}

	const UINT vbByteSize = treeCount * sizeof(BillboardVertex);
	const UINT ibByteSizeLocal = treeCount * sizeof(std::uint16_t);

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "treeBillboardGeo";
	geometry->mVertexByteStride = sizeof(BillboardVertex);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["treeBillboard"] = { treeCount, 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["treeBillboardGeo"] = std::move(geometry);
}

Texture* ShapesApp::GetTextureAsset(const std::string& textureName)
{
	auto it = textures.find(textureName);
	return it == textures.end() ? nullptr : it->second.get();
}

Texture* ShapesApp::LoadTextureAsset(const std::string& textureName, const std::wstring& filename, bool sRGB)
{
	if (auto* existing = GetTextureAsset(textureName))
	{
		return existing;
	}

	auto texture = std::make_unique<Texture>();
	texture->name = textureName;
	texture->filename = filename;
	texture->resource = CreateTextureFromFile(
		md3dDevice.Get(),
		mCommandList.Get(),
		filename,
		sRGB,
		texture->uploadHeap,
		texture->srvFormat,
		texture->isCubeMap);
	texture->srvHeapIndex = static_cast<int>(mNextSrvHeapIndex++);

	Texture* result = texture.get();
	textures[textureName] = std::move(texture);
	return result;
}

void ShapesApp::LoadTextures()
{
	LoadTextureAsset("woodTex", L"Textures/WoodCrate01.dds", true);
	LoadTextureAsset("white1x1Tex", L"Textures/white1x1.dds", false);
	LoadTextureAsset("defaultNormalTex", L"Textures/default_nmap.dds", false);
	LoadTextureAsset("bricksTex", L"Textures/bricks.dds", true);
	LoadTextureAsset("bricksNormalTex", L"Textures/bricks2_nmap.dds", false);
	LoadTextureAsset("tileTex", L"Textures/tile.dds", true);
	LoadTextureAsset("tileNormalTex", L"Textures/tile_nmap.dds", false);
	LoadTextureAsset("treeArray2Tex", L"Textures/treearray.dds", true);
	LoadTextureAsset("sunsetCubeTex", L"Textures/sunsetcube1024.dds", true);
}

void ShapesApp::BuildMaterial()
{
	auto addMaterial = [this](
		const std::string& materialName,
		const std::string& baseColorTexture,
		const std::string& normalTexture,
		XMFLOAT4 baseColorFactor,
		XMFLOAT3 dielectricF0,
		float roughnessFactor,
		float metallicFactor,
		float normalMapFlipY = 0.0f,
		float alphaCutoff = 0.1f)
	{
		auto material = std::make_unique<Material>();
		material->name = materialName;
		material->matCBIndex = static_cast<int>(materials.size());
		material->baseColorSrvHeapIndex = textures[baseColorTexture]->srvHeapIndex;
		material->normalSrvHeapIndex = textures[normalTexture]->srvHeapIndex;
		material->roughnessSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->metallicSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->baseColorFactor = baseColorFactor;
		material->fresnelR0 = dielectricF0;
		material->roughnessFactor = roughnessFactor;
		material->metallicFactor = metallicFactor;
		material->normalScale = 1.0f;
		material->normalMapFlipY = normalMapFlipY;
		material->alphaCutoff = alphaCutoff;
		materials[materialName] = std::move(material);
	};

	addMaterial("sphere", "white1x1Tex", "defaultNormalTex", { 0.95f, 0.64f, 0.54f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.18f, 1.0f);
	addMaterial("cylinder", "bricksTex", "bricksNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.55f, 0.0f);
	addMaterial("grid", "tileTex", "tileNormalTex", { 0.9f, 0.9f, 0.9f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.75f, 0.0f);
	addMaterial("box", "woodTex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.65f, 0.0f);
	addMaterial("skull", "white1x1Tex", "defaultNormalTex", { 0.8f, 0.82f, 0.85f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.2f, 1.0f);
	addMaterial("wood", "woodTex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.7f, 0.0f);
	addMaterial("mirror", "white1x1Tex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 0.25f }, { 0.04f, 0.04f, 0.04f }, 0.05f, 1.0f);
	addMaterial("shadow", "white1x1Tex", "defaultNormalTex", { 0.0f, 0.0f, 0.0f, 0.25f }, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f);
	addMaterial("treeBillboard", "treeArray2Tex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.85f, 0.0f, 0.0f, 0.1f);
}

void ShapesApp::BuildImportedPbrSphere()
{
	const std::filesystem::path objPath = std::filesystem::path("Models") / "Obj_PBRTest" / "Sphere.obj";
	const std::filesystem::path objDirectory = objPath.parent_path();

	tinyobj::ObjReaderConfig readerConfig;
	readerConfig.mtl_search_path = objDirectory.string();
	readerConfig.triangulate = true;

	tinyobj::ObjReader reader;
	if (!reader.ParseFromFile(objPath.string(), readerConfig))
	{
		OutputDebugStringA((std::string("Failed to load OBJ: ") + reader.Error() + "\n").c_str());
		return;
	}

	const auto& attrib = reader.GetAttrib();
	const auto& shapes = reader.GetShapes();
	const auto& sourceMaterials = reader.GetMaterials();

	auto materialKeyForId = [&](int materialId)
	{
		if (materialId >= 0 && materialId < static_cast<int>(sourceMaterials.size()) && !sourceMaterials[materialId].name.empty())
		{
			return std::string("ornament_") + sourceMaterials[materialId].name;
		}

		return std::string("ornament_default");
	};

	auto ensureMaterial = [&](int materialId)
	{
		const std::string materialKey = materialKeyForId(materialId);
		if (materials.find(materialKey) != materials.end())
		{
			return;
		}

		auto material = std::make_unique<Material>();
		material->name = materialKey;
		material->matCBIndex = static_cast<int>(materials.size());
		material->baseColorSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->normalSrvHeapIndex = textures["defaultNormalTex"]->srvHeapIndex;
		material->roughnessSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->metallicSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->baseColorFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material->fresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
		material->roughnessFactor = 1.0f;
		material->metallicFactor = 0.0f;
		material->normalScale = 1.0f;
		material->alphaCutoff = 0.1f;

		if (materialId >= 0 && materialId < static_cast<int>(sourceMaterials.size()))
		{
			const auto& src = sourceMaterials[materialId];
			material->baseColorFactor = XMFLOAT4(
				src.diffuse[0] > 0.0f ? src.diffuse[0] : 1.0f,
				src.diffuse[1] > 0.0f ? src.diffuse[1] : 1.0f,
				src.diffuse[2] > 0.0f ? src.diffuse[2] : 1.0f,
				src.dissolve > 0.0f ? src.dissolve : 1.0f);

			material->roughnessFactor = src.roughness > 0.0f ? src.roughness : 1.0f;
			material->metallicFactor = src.metallic_texname.empty() ? src.metallic : 1.0f;

			if (!src.diffuse_texname.empty())
			{
				const auto texturePath = objDirectory / src.diffuse_texname;
				material->baseColorSrvHeapIndex = LoadTextureAsset(materialKey + "_base", texturePath.wstring(), true)->srvHeapIndex;
			}

			const std::string normalTextureName = !src.normal_texname.empty() ? src.normal_texname : src.bump_texname;
			if (!normalTextureName.empty())
			{
				const auto texturePath = objDirectory / normalTextureName;
				material->normalSrvHeapIndex = LoadTextureAsset(materialKey + "_normal", texturePath.wstring(), false)->srvHeapIndex;

				std::string lowered = normalTextureName;
				std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch)
				{
					return static_cast<char>(std::tolower(ch));
				});
				if (lowered.find("normalgl") != std::string::npos)
				{
					material->normalMapFlipY = 0.0f;
				}

				material->normalScale = 3.5f;
			}

			if (!src.roughness_texname.empty())
			{
				const auto texturePath = objDirectory / src.roughness_texname;
				material->roughnessSrvHeapIndex = LoadTextureAsset(materialKey + "_roughness", texturePath.wstring(), false)->srvHeapIndex;
			}

			if (!src.metallic_texname.empty())
			{
				const auto texturePath = objDirectory / src.metallic_texname;
				material->metallicSrvHeapIndex = LoadTextureAsset(materialKey + "_metallic", texturePath.wstring(), false)->srvHeapIndex;
			}
		}

		materials[materialKey] = std::move(material);
	};

	std::map<std::string, std::vector<Vertex1>> groupedVertices;
	std::map<std::string, std::vector<std::uint32_t>> groupedIndices;

	for (const auto& shape : shapes)
	{
		size_t indexOffset = 0;
		for (size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size(); ++faceIndex)
		{
			const size_t faceVertexCount = static_cast<size_t>(shape.mesh.num_face_vertices[faceIndex]);
			if (faceVertexCount != 3)
			{
				indexOffset += faceVertexCount;
				continue;
			}

			const int materialId = faceIndex < shape.mesh.material_ids.size() ? shape.mesh.material_ids[faceIndex] : -1;
			ensureMaterial(materialId);
			const std::string materialKey = materialKeyForId(materialId);

			XMFLOAT3 facePositions[3];
			XMFLOAT3 faceNormals[3];
			XMFLOAT2 faceTexcoords[3];
			bool hasNormals = true;

			for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
			{
				const tinyobj::index_t idx = shape.mesh.indices[indexOffset + vertexIndex];
				facePositions[vertexIndex] = XMFLOAT3(
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 0],
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 1],
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 2]);

				if (idx.normal_index >= 0)
				{
					faceNormals[vertexIndex] = XMFLOAT3(
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 0],
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 1],
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 2]);
				}
				else
				{
					hasNormals = false;
				}

				if (idx.texcoord_index >= 0)
				{
					faceTexcoords[vertexIndex] = XMFLOAT2(
						attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 0],
						1.0f - attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 1]);
				}
				else
				{
					faceTexcoords[vertexIndex] = XMFLOAT2(0.0f, 0.0f);
				}
			}

			if (!hasNormals)
			{
				const XMVECTOR p0 = XMLoadFloat3(&facePositions[0]);
				const XMVECTOR p1 = XMLoadFloat3(&facePositions[1]);
				const XMVECTOR p2 = XMLoadFloat3(&facePositions[2]);
				const XMVECTOR faceNormal = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
				for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
				{
					XMStoreFloat3(&faceNormals[vertexIndex], faceNormal);
				}
			}

			auto& vertices = groupedVertices[materialKey];
			auto& indices = groupedIndices[materialKey];
			for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
			{
				const XMFLOAT4 faceTangent = ComputeTriangleTangent(
					facePositions[0],
					facePositions[1],
					facePositions[2],
					faceTexcoords[0],
					faceTexcoords[1],
					faceTexcoords[2],
					faceNormals[vertexIndex]);

				Vertex1 vertex = {};
				vertex.Pos = facePositions[vertexIndex];
				vertex.Normal = faceNormals[vertexIndex];
				vertex.TangentU = faceTangent;
				vertex.TexC = faceTexcoords[vertexIndex];
				vertices.push_back(vertex);
				indices.push_back(static_cast<std::uint32_t>(vertices.size() - 1));
			}

			indexOffset += faceVertexCount;
		}
	}

	if (groupedVertices.empty())
	{
		return;
	}

	std::vector<Vertex1> mergedVertices;
	std::vector<std::uint32_t> mergedIndices;
	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "ornamentGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mIndexFormat = DXGI_FORMAT_R32_UINT;

	for (const auto& [materialKey, vertices] : groupedVertices)
	{
		const auto& indices = groupedIndices[materialKey];
		SubmeshGeometry submesh = {};
		submesh.baseVertexLocation = static_cast<UINT>(mergedVertices.size());
		submesh.startIndexLocation = static_cast<UINT>(mergedIndices.size());
		submesh.indexCount = static_cast<UINT>(indices.size());

		mergedVertices.insert(mergedVertices.end(), vertices.begin(), vertices.end());
		for (std::uint32_t index : indices)
		{
			mergedIndices.push_back(index);
		}

		geometry->mDrawArgs[materialKey] = submesh;
	}

	const UINT vbByteSize = static_cast<UINT>(mergedVertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(mergedIndices.size() * sizeof(std::uint32_t));
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), mergedVertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), mergedIndices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, mergedVertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, mergedIndices.data(), geometry->mIndexBufferUploader);

	geometries["ornamentGeo"] = std::move(geometry);
}

void ShapesApp::BuildRenderItems()
{
	for (auto& layer : mRitemLayer)
	{
		layer.clear();
	}
	mAllItem.clear();
	mShadowPairs.clear();

	UINT objCBIndex = 0;

	auto addMergedGeometryItem = [&](const std::string& drawKey, const std::string& materialKey, CXMMATRIX worldMatrix)
	{
		auto renderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&renderItem->world, worldMatrix);
		renderItem->mObjCBIndex = objCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->indexCount = mDrawArgs[drawKey].indexCount;
		renderItem->baseVertexLocation = mDrawArgs[drawKey].baseVertexLocation;
		renderItem->startIndexLocation = mDrawArgs[drawKey].startIndexLocation;
		renderItem->mat = materials[materialKey].get();
		mAllItem.push_back(std::move(renderItem));
	};

	addMergedGeometryItem("box", "box", XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	addMergedGeometryItem("grid", "grid", XMMatrixScaling(3.0f, 3.0f, 3.0f));

	for (int i = 0; i < 5; ++i)
	{
		const XMMATRIX leftCylinderWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightCylinderWorld = XMMatrixTranslation(5.0f, 1.5f, -10.0f + i * 5.0f);
		const XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightSphereWorld = XMMatrixTranslation(5.0f, 3.5f, -10.0f + i * 5.0f);

		addMergedGeometryItem("cylinder", "cylinder", leftCylinderWorld);
		addMergedGeometryItem("cylinder", "cylinder", rightCylinderWorld);
		addMergedGeometryItem("sphere", "sphere", leftSphereWorld);
		addMergedGeometryItem("sphere", "sphere", rightSphereWorld);
	}

	if (auto it = geometries.find("skullGeo"); it != geometries.end())
	{
		auto renderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&renderItem->world, XMMatrixScaling(0.4f, 0.4f, 0.4f) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
		renderItem->mObjCBIndex = objCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->geo = it->second.get();
		const auto& submesh = it->second->mDrawArgs["skull"];
		renderItem->indexCount = submesh.indexCount;
		renderItem->baseVertexLocation = submesh.baseVertexLocation;
		renderItem->startIndexLocation = submesh.startIndexLocation;
		renderItem->mat = materials["skull"].get();
		skullRitem = renderItem.get();
		mAllItem.push_back(std::move(renderItem));
	}

	if (auto it = geometries.find("boxGeo"); it != geometries.end())
	{
		auto renderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&renderItem->world, XMMatrixScaling(0.3f, 0.3f, 0.3f) * XMMatrixTranslation(0.0f, 1.0f, -5.0f));
		renderItem->mObjCBIndex = objCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->geo = it->second.get();
		const auto& submesh = it->second->mDrawArgs["woodBox"];
		renderItem->indexCount = submesh.indexCount;
		renderItem->baseVertexLocation = submesh.baseVertexLocation;
		renderItem->startIndexLocation = submesh.startIndexLocation;
		renderItem->mat = materials["wood"].get();
		mAllItem.push_back(std::move(renderItem));
	}

	if (auto it = geometries.find("ornamentGeo"); it != geometries.end())
	{
		for (const auto& [materialKey, submesh] : it->second->mDrawArgs)
		{
			auto renderItem = std::make_unique<RenderItem>();
			const XMMATRIX ornamentWorld =
				XMMatrixScaling(1.2f, 1.2f, 1.2f) *
				XMMatrixRotationY(0.25f * XM_PI) *
				XMMatrixTranslation(1.5f, 1.45f, -1.6f);
			XMStoreFloat4x4(&renderItem->world, ornamentWorld);
			renderItem->mObjCBIndex = objCBIndex++;
			renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderItem->geo = it->second.get();
			renderItem->indexCount = submesh.indexCount;
			renderItem->baseVertexLocation = submesh.baseVertexLocation;
			renderItem->startIndexLocation = submesh.startIndexLocation;
			renderItem->mat = materials[materialKey].get();
			mAllItem.push_back(std::move(renderItem));
		}
	}

	RenderItem* mirrorItem = nullptr;
	if (auto it = geometries.find("gridGeo"); it != geometries.end())
	{
		auto renderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&renderItem->world, XMMatrixScaling(0.6f, 0.6f, 0.6f) * XMMatrixRotationX(-1.5f) * XMMatrixTranslation(0.0f, 3.0f, 4.0f));
		renderItem->mObjCBIndex = objCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->geo = it->second.get();
		const auto& submesh = it->second->mDrawArgs["mirrorGrid"];
		renderItem->indexCount = submesh.indexCount;
		renderItem->baseVertexLocation = submesh.baseVertexLocation;
		renderItem->startIndexLocation = submesh.startIndexLocation;
		renderItem->mat = materials["mirror"].get();
		renderItem->layer = RenderLayer::Mirrors;
		mirrorItem = renderItem.get();
		mAllItem.push_back(std::move(renderItem));
	}

	if (skullRitem != nullptr)
	{
		if (auto it = geometries.find("skullGeo"); it != geometries.end())
		{
			auto renderItem = std::make_unique<RenderItem>();
			XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, -4.0f);
			XMMATRIX reflectionMatrix = XMMatrixReflect(mirrorPlane);
			XMMATRIX skullWorld = XMLoadFloat4x4(&skullRitem->world);
			XMStoreFloat4x4(&renderItem->world, skullWorld * reflectionMatrix);
			renderItem->mObjCBIndex = objCBIndex++;
			renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderItem->geo = it->second.get();
			const auto& submesh = it->second->mDrawArgs["skull"];
			renderItem->indexCount = submesh.indexCount;
			renderItem->baseVertexLocation = submesh.baseVertexLocation;
			renderItem->startIndexLocation = submesh.startIndexLocation;
			renderItem->mat = materials["skull"].get();
			renderItem->layer = RenderLayer::Reflect;
			skullMirrorItem = renderItem.get();
			mAllItem.push_back(std::move(renderItem));
		}
	}

	if (auto it = geometries.find("treeBillboardGeo"); it != geometries.end())
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->world = MathHelper::Identity4x4();
		renderItem->mObjCBIndex = objCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		renderItem->geo = it->second.get();
		const auto& submesh = it->second->mDrawArgs["treeBillboard"];
		renderItem->indexCount = submesh.indexCount;
		renderItem->baseVertexLocation = submesh.baseVertexLocation;
		renderItem->startIndexLocation = submesh.startIndexLocation;
		renderItem->mat = materials["treeBillboard"].get();
		renderItem->layer = RenderLayer::AlphaTest;
		mAllItem.push_back(std::move(renderItem));
	}

	for (auto& renderItem : mAllItem)
	{
		mRitemLayer[(int)renderItem->layer].push_back(renderItem.get());
	}

	for (RenderItem* opaqueItem : mRitemLayer[(int)RenderLayer::Opaque])
	{
		if (opaqueItem->mat == nullptr)
		{
			continue;
		}

		const bool castsPlanarShadow =
			opaqueItem == skullRitem ||
			(opaqueItem->geo != nullptr && opaqueItem->geo->name == "ornamentGeo");

		if (!castsPlanarShadow)
		{
			continue;
		}

		auto shadowItem = std::make_unique<RenderItem>(*opaqueItem);
		shadowItem->mObjCBIndex = objCBIndex++;
		shadowItem->layer = RenderLayer::Shadow;
		shadowItem->mat = materials["shadow"].get();

		if (opaqueItem == skullRitem)
		{
			skullShadowItem = shadowItem.get();
		}

		RenderItem* shadowPtr = shadowItem.get();
		mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowPtr);
		mShadowPairs.push_back({ opaqueItem, shadowPtr });
		mAllItem.push_back(std::move(shadowItem));
	}

	if (mirrorItem != nullptr)
	{
		mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorItem);
	}
}

void ShapesApp::BuildFrameResource()
{
	mFrameResourcesArray.clear();
	for (UINT i = 0; i < mFrameResourcesCount; ++i)
	{
		mFrameResourcesArray.push_back(std::make_unique<FrameResources>(
			md3dDevice.Get(),
			static_cast<UINT>(mAllItem.size()),
			static_cast<UINT>(materials.size()),
			2));
	}

	mCurrentFrameResourcesIndex = 0;
	mCurrentFrameResources = mFrameResourcesArray.empty() ? nullptr : mFrameResourcesArray[0].get();
}

void ShapesApp::Update(GameTimer& gt)
{
	mCurrentFrameResourcesIndex = (mCurrentFrameResourcesIndex + 1) % mFrameResourcesCount;
	mCurrentFrameResources = mFrameResourcesArray[mCurrentFrameResourcesIndex].get();

	if (mCurrentFrameResources->mFenceCPU != 0 && mFence->GetCompletedValue() < mCurrentFrameResources->mFenceCPU)
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFrameResources->mFenceCPU, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	OnKeyboardInput(gt);
	UpdateObjCBs();
	UpdateMatCBs();
	UpdatePassCBs(gt);
}

void ShapesApp::UpdateObjCBs()
{
	ObjectConstants objConstants = {};

	for (auto& renderItem : mAllItem)
	{
		if (renderItem->numFramesDirty <= 0)
		{
			continue;
		}

		const XMMATRIX world = XMLoadFloat4x4(&renderItem->world);
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

		XMVECTOR determinant = XMMatrixDeterminant(world);
		const XMMATRIX invWorld = XMMatrixInverse(&determinant, world);
		XMStoreFloat4x4(&objConstants.WorldInvTrans, XMMatrixTranspose(invWorld));

		mCurrentFrameResources->objCB->CopyData(renderItem->mObjCBIndex, objConstants);
		renderItem->numFramesDirty--;
	}
}

void ShapesApp::UpdatePassCBs(const GameTimer& gt)
{
	const float x = mRadius * sinf(mPhi) * cosf(mTheta);
	const float y = mRadius * cosf(mPhi);
	const float z = mRadius * sinf(mPhi) * sinf(mTheta);

	const XMVECTOR eyePos = XMVectorSet(x, y, z, 1.0f);
	const XMVECTOR target = XMVectorZero();
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMMATRIX view = XMMatrixLookAtLH(eyePos, target, up);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX viewProj = view * proj;

	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	passConstants.cameraPosW = XMFLOAT3(x, y, z);
	passConstants.totalTime = gt.TotalTime();
	passConstants.ambientLight = { 0.012f, 0.012f, 0.016f, 1.0f };
	passConstants.envMapMipCount = static_cast<float>(textures[mEnvironmentTextureName]->resource->GetDesc().MipLevels);
	passConstants.iblStrength = 0.6f;

	for (int i = 0; i < MAX_LIGHTS; ++i)
	{
		passConstants.lights[i] = Light{};
	}

	passConstants.lights[0].strength = { 2.8f, 2.7f, 2.55f };
	const XMVECTOR keyLightDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);
	XMStoreFloat3(&passConstants.lights[0].direction, keyLightDir);

	passConstants.lights[1].strength = { 0.55f, 0.52f, 0.48f };
	const XMVECTOR fillLightDir = XMVector3Normalize(XMVectorSet(0.35f, -0.75f, 0.45f, 0.0f));
	XMStoreFloat3(&passConstants.lights[1].direction, fillLightDir);

	mCurrentFrameResources->passCB->CopyData(0, passConstants);
	UpdateReflectPassCBs();
}

void ShapesApp::UpdateReflectPassCBs()
{
	reflectPassConstant = passConstants;

	const XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, -4.0f);
	const XMMATRIX reflectionMatrix = XMMatrixReflect(mirrorPlane);

	for (int i = 0; i < 2; ++i)
	{
		const XMVECTOR lightDir = XMLoadFloat3(&passConstants.lights[i].direction);
		const XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, reflectionMatrix);
		XMStoreFloat3(&reflectPassConstant.lights[i].direction, reflectedLightDir);
	}

	mCurrentFrameResources->passCB->CopyData(1, reflectPassConstant);
}

void ShapesApp::UpdateMatCBs()
{
	for (auto& materialPair : materials)
	{
		Material* material = materialPair.second.get();
		if (material->numFramesDirty <= 0)
		{
			continue;
		}

		MatConstants matConstants = {};
		matConstants.baseColorFactor = material->baseColorFactor;
		matConstants.fresnelR0 = material->fresnelR0;
		matConstants.roughnessFactor = material->roughnessFactor;
		matConstants.metallicFactor = material->metallicFactor;
		matConstants.normalScale = material->normalScale;
		matConstants.normalMapFlipY = material->normalMapFlipY;
		matConstants.alphaCutoff = material->alphaCutoff;

		mCurrentFrameResources->matCB->CopyData(material->matCBIndex, matConstants);
		material->numFramesDirty--;
	}
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState(VK_LEFT) & 0x8000) sunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000) sunTheta += 1.0f * dt;
	if (GetAsyncKeyState(VK_UP) & 0x8000) sunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(VK_DOWN) & 0x8000) sunPhi += 1.0f * dt;
	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);

	if (GetAsyncKeyState('A') & 0x8000) skullTranslation.x -= 2.0f * dt;
	if (GetAsyncKeyState('D') & 0x8000) skullTranslation.x += 2.0f * dt;
	if (GetAsyncKeyState('W') & 0x8000) skullTranslation.y += 2.0f * dt;
	if (GetAsyncKeyState('S') & 0x8000) skullTranslation.y -= 2.0f * dt;
	skullTranslation.y = MathHelper::Max(0.0f, skullTranslation.y);

	if (skullRitem != nullptr)
	{
		const XMMATRIX skullScale = XMMatrixScaling(0.4f, 0.4f, 0.4f);
		const XMMATRIX skullTranslate = XMMatrixTranslation(skullTranslation.x, skullTranslation.y + 1.0f, skullTranslation.z);
		const XMMATRIX skullWorld = skullScale * skullTranslate;
		XMStoreFloat4x4(&skullRitem->world, skullWorld);
		skullRitem->numFramesDirty = mFrameResourcesCount;

		if (skullMirrorItem != nullptr)
		{
			const XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, -4.0f);
			const XMMATRIX reflectionMatrix = XMMatrixReflect(mirrorPlane);
			XMStoreFloat4x4(&skullMirrorItem->world, skullWorld * reflectionMatrix);
			skullMirrorItem->numFramesDirty = mFrameResourcesCount;
		}
	}

	const XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMVECTOR toMainLight = MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);
	const XMMATRIX shadowMatrix = XMMatrixShadow(shadowPlane, toMainLight);
	const XMMATRIX shadowOffset = XMMatrixTranslation(0.0f, 0.001f, 0.0f);

	for (auto& [sourceItem, shadowItem] : mShadowPairs)
	{
		const XMMATRIX sourceWorld = XMLoadFloat4x4(&sourceItem->world);
		XMStoreFloat4x4(&shadowItem->world, sourceWorld * shadowMatrix * shadowOffset);
		shadowItem->numFramesDirty = mFrameResourcesCount;
	}
}

std::array<CD3DX12_STATIC_SAMPLER_DESC, 7> ShapesApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicMirror(
		6,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		0.0f,
		8);

	return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp, anisotropicMirror };
}

D3D12_VERTEX_BUFFER_VIEW ShapesApp::GetVBV() const
{
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	view.StrideInBytes = sizeof(Vertex1);
	view.SizeInBytes = totalByteSize;
	return view;
}

D3D12_INDEX_BUFFER_VIEW ShapesApp::GetIBV() const
{
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	view.Format = DXGI_FORMAT_R16_UINT;
	view.SizeInBytes = ibByteSize;
	return view;
}
