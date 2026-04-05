#pragma once
#include"D3DApp.h"
#include"DirectXMath.h"
#include"UploadBuffer.h"
#include"ToolFunc.h"
#include"ProceduralGeometry.h"
#include"FrameResources.h"
#include<vector>
#include<array>

struct Vertex1
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};

class D3D12InitApp : public D3DApp
{
public :
	D3D12InitApp(HINSTANCE hInstance);
	~D3D12InitApp();
	virtual void Draw() override;
	bool Init(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption);

public :
	//物体空间 -> 裁剪空间（初始化为单位矩阵）
	XMFLOAT4X4 world = MathHelper::Identity4x4();
	XMFLOAT4X4 view = MathHelper::Identity4x4();

protected :
	std::array<Vertex1, 8> vertices;
	std::array<std::uint16_t, 36> indices;
	UINT totalByteSize;
	UINT ibByteSize;
	//顶点缓冲区
	ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;			//实际存放在显存中的定点数据
	ComPtr<ID3D12Resource> mVertexBufferUploader = nullptr;					//上传缓冲区数据
	D3D12_VERTEX_BUFFER_VIEW vbv;											//顶点缓冲区视图
	//索引缓冲区
	ComPtr<ID3D12Resource> mIndexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mIndexBufferUploader = nullptr;
	D3D12_INDEX_BUFFER_VIEW ibv;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mPSO = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3DBlob> vsBytecode = nullptr;
	ComPtr<ID3DBlob> psBytecode = nullptr;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayoutDesc1;

	//常量资源上传堆
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<PassConstants>> mPassCB = nullptr;

	ComPtr<ID3DBlob> mVertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> mIndexBufferCPU = nullptr;

	D3D12_VERTEX_BUFFER_VIEW GetVBV()const;
	D3D12_INDEX_BUFFER_VIEW GetIBV()const;
	void OnResize();
	void BuildGeometry();
	void BuildConstBuffer();
	void BuildRootSignature();
	void BuildByteCodeAndInputLayout();
	void BuildPSO();
	void Update(GameTimer& gt)override;
};

