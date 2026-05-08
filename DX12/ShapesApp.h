#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "D3DApp.h"
#include "DDSTextureLoader.h"
#include "DXMath/MathHelper.h"
#include "FrameResources.h"
#include "ProceduralGeometry.h"
#include "ToolFunc.h"
#include "UploadBuffer.h"

class ShapesApp : public D3DApp, public ProceduralGeometry
{
public:
	ShapesApp(HINSTANCE hInstance) : D3DApp(hInstance) {}
	~ShapesApp();

	struct Vertex1
	{
		XMFLOAT3 Pos;
		XMFLOAT3 Normal;
		XMFLOAT4 TangentU;
		XMFLOAT2 TexC;
	};

	enum RenderLayer : int
	{
		Opaque = 0,
		Mirrors,
		Reflect,
		Transparent,
		AlphaTest,
		Shadow,
		Count
	};

	struct SubmeshGeometry
	{
		UINT indexCount = 0;
		UINT baseVertexLocation = 0;
		UINT startIndexLocation = 0;
	};

	struct MeshGeometry
	{
		std::string name;

		ComPtr<ID3DBlob> mVertexBufferCPU = nullptr;
		ComPtr<ID3DBlob> mIndexBufferCPU = nullptr;

		ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;
		ComPtr<ID3D12Resource> mVertexBufferUploader = nullptr;
		ComPtr<ID3D12Resource> mIndexBufferGPU = nullptr;
		ComPtr<ID3D12Resource> mIndexBufferUploader = nullptr;

		UINT mVertexByteStride = sizeof(Vertex1);
		UINT mVertexBufferByteSize = 0;
		UINT mIndexBufferByteSize = 0;
		DXGI_FORMAT mIndexFormat = DXGI_FORMAT_R32_UINT;

		std::unordered_map<std::string, SubmeshGeometry> mDrawArgs;
	};

	struct RenderItem
	{
		RenderItem() = default;

		MeshGeometry* geo = nullptr;
		XMFLOAT4X4 world = MathHelper::Identity4x4();
		XMFLOAT4X4 WorldInversed = MathHelper::Identity4x4();

		UINT mObjCBIndex = static_cast<UINT>(-1);
		D3D12_PRIMITIVE_TOPOLOGY mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		RenderLayer layer = RenderLayer::Opaque;

		UINT indexCount = 0;
		UINT baseVertexLocation = 0;
		UINT startIndexLocation = 0;
		int numFramesDirty = 3;

		Material* mat = nullptr;
	};

	void Draw() override;
	void DrawRenderItems(const std::vector<RenderItem*>& ritems);
	void OnResize() override;

	void BuildRootSignature();
	void BuildByteCodeAndInputLayout();
	void BuildShaderResourceView();
	void BuildPSO();

	void BuildShapeGeometry();
	void BuildSkull();
	void BuildBoxGeometry();
	void BuildGridGeomerty();
	void BuildImportedPbrSphere();
	void BuildTreeBillboardGeometry();

	void LoadTextures();
	void BuildMaterial();
	void BuildRenderItems();
	void BuildFrameResource();

	void Update(GameTimer& gt) override;
	void UpdateObjCBs();
	void UpdatePassCBs(const GameTimer& gt);
	void UpdateReflectPassCBs();
	void UpdateMatCBs();

	void OnKeyboardInput(const GameTimer& gt);
	Texture* LoadTextureAsset(const std::string& textureName, const std::wstring& filename, bool sRGB);
	Texture* GetTextureAsset(const std::string& textureName);

	D3D12_VERTEX_BUFFER_VIEW GetVBV() const;
	D3D12_INDEX_BUFFER_VIEW GetIBV() const;
	std::array<CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

	bool InitRnederItems(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption);

	UINT mFrameResourcesCount = 3;

protected:
	std::unordered_map<std::string, SubmeshGeometry> mDrawArgs;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::vector<std::unique_ptr<RenderItem>> mAllItem;
	std::string mEnvironmentTextureName = "sunsetCubeTex";
	UINT mNextSrvHeapIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3DBlob> vsBytecode = nullptr;
	ComPtr<ID3DBlob> psBytecode = nullptr;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayoutDesc1;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::vector<D3D12_INPUT_ELEMENT_DESC> treeBillboardInputLayoutDesc;

	ComPtr<ID3D12DescriptorHeap> mSrvHeap = nullptr;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

	UINT totalByteSize = 0;
	UINT ibByteSize = 0;

	ComPtr<ID3D12Resource> mVertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mVertexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vbv = {};

	ComPtr<ID3D12Resource> mIndexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> mIndexBufferUploader = nullptr;
	D3D12_INDEX_BUFFER_VIEW ibv = {};

	ComPtr<ID3DBlob> mVertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> mIndexBufferCPU = nullptr;

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	RenderItem* skullRitem = nullptr;
	RenderItem* skullMirrorItem = nullptr;
	RenderItem* skullShadowItem = nullptr;

	XMFLOAT3 skullTranslation = { 0.0f, 0.0f, 0.0f };
	std::vector<std::pair<RenderItem*, RenderItem*>> mShadowPairs;

protected:
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();
	std::vector<std::unique_ptr<FrameResources>> mFrameResourcesArray;
	UINT mCurrentFrameResourcesIndex = 0;
	FrameResources* mCurrentFrameResources = nullptr;
	PassConstants passConstants;
	PassConstants reflectPassConstant;
};
