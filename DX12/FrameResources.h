#pragma once
#include"ToolFunc.h"
#include"UploadBuffer.h"

using namespace DirectX::PackedVector;
using namespace DirectX;
#define MAX_LIGHTS 16

struct Vertex
{
	XMFLOAT3 Pos;
	XMCOLOR Color;
};

struct ObjectConstants
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 WorldInvTrans = MathHelper::Identity4x4();
};

struct PassConstants
{
	XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();

	XMFLOAT3 cameraPosW = { 0.0f, 0.0f, 0.0f };
	float totalTime = 0.0f;
	XMFLOAT4 ambientLight = { 0.0f,0.0f,0.0f,1.0f };
	Light lights[MAX_LIGHTS];
};

struct MatConstants
{
	XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	XMFLOAT3     fresnelR0 = { 0.01f, 0.01f, 0.01f };
	float		 roughness = 0.25f;

	XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

class FrameResources
{
public :
	FrameResources(ID3D12Device* mDevice, UINT objCount, UINT materialCount, UINT passCount);
	FrameResources(const FrameResources& rhs) = delete;
	FrameResources& operator = (const FrameResources& rhs) = delete;
	~FrameResources();

	ComPtr<ID3D12CommandAllocator> mCmdAllocator;
	
	std::unique_ptr<UploadBuffer<ObjectConstants>>objCB = nullptr;
	std::unique_ptr<UploadBuffer<PassConstants>>passCB = nullptr;
	std::unique_ptr<UploadBuffer<MatConstants>>matCB = nullptr;
	UINT64 mFenceCPU = 0;
};