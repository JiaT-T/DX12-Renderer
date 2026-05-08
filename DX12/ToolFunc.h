#pragma once

#include <Windows.h>
#include <comdef.h>
#include <cstdint>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <string>
#include <wrl/client.h>

#include "d3dx12.h"
#include "D3DApp.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;
using namespace DirectX;

#ifndef ThrowIfFailed
#define ThrowIfFailed(x) { \
	HRESULT hr__ = (x); \
	std::wstring wfn = __FILEW__; \
	if (FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNumber);

	std::wstring ToString() const;

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring FileName;
	int LineNumber = -1;
};

ComPtr<ID3D12Resource> CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdlist,
	UINT byteSize,
	const void* initData,
	ComPtr<ID3D12Resource>& uploadBuffer);

ComPtr<ID3D12Resource> CreateTextureFromFile(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdlist,
	const std::wstring& filename,
	bool sRGB,
	ComPtr<ID3D12Resource>& uploadBuffer,
	DXGI_FORMAT& srvFormat,
	bool& isCubeMap);

ComPtr<ID3DBlob> CompileShader(
	const std::wstring& fileName,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target);

inline UINT CalcConstantBufferByteSize(UINT byteSize)
{
	return (byteSize + 255) & ~255;
}

struct Material
{
	std::string name;
	int matCBIndex = -1;
	int baseColorSrvHeapIndex = -1;
	int normalSrvHeapIndex = -1;
	int roughnessSrvHeapIndex = -1;
	int metallicSrvHeapIndex = -1;
	int numFramesDirty = 3;

	XMFLOAT4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
	XMFLOAT3 fresnelR0 = { 0.04f, 0.04f, 0.04f };
	float roughnessFactor = 1.0f;
	float metallicFactor = 0.0f;
	float normalScale = 1.0f;
	float normalMapFlipY = 0.0f;
	float alphaCutoff = 0.1f;
};

struct Light
{
	XMFLOAT3 strength = { 0.5f, 0.5f, 0.5f };
	float falloffStart = 1.0f;
	XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
	float falloffEnd = 10.0f;
	XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
	float spotPower = 64.0f;
};

struct Texture
{
	std::string name;
	std::wstring filename;
	ComPtr<ID3D12Resource> resource = nullptr;
	ComPtr<ID3D12Resource> uploadHeap = nullptr;
	DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
	bool isCubeMap = false;
	int srvHeapIndex = -1;
};

