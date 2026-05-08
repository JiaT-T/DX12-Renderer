#include "ToolFunc.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>

#include "DDSTextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

namespace
{
	std::wstring ToLower(std::wstring value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
		{
			return static_cast<wchar_t>(towlower(ch));
		});
		return value;
	}
}

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNumber) :
	ErrorCode(hr),
	FunctionName(functionName),
	FileName(fileName),
	LineNumber(lineNumber)
{
}

std::wstring DxException::ToString() const
{
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();
	return FunctionName + L" failed in " + FileName + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

ComPtr<ID3DBlob> CompileShader(
	const std::wstring& fileName,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors = nullptr;
	HRESULT hr = D3DCompileFromFile(
		fileName.c_str(),
		defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(),
		target.c_str(),
		compileFlags,
		0,
		&byteCode,
		&errors);

	if (errors != nullptr)
	{
		OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		MessageBoxA(nullptr, static_cast<const char*>(errors->GetBufferPointer()), "Shader Compilation Error", MB_OK | MB_ICONERROR);
	}

	ThrowIfFailed(hr);
	return byteCode;
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdlist,
	UINT byteSize,
	const void* initData,
	ComPtr<ID3D12Resource>& uploadBuffer)
{
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> defaultBuffer = nullptr;
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	auto barrierCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST);
	cmdlist->ResourceBarrier(1, &barrierCopyDest);

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	UpdateSubresources<1>(cmdlist, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);

	auto barrierGenericRead = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdlist->ResourceBarrier(1, &barrierGenericRead);

	return defaultBuffer;
}

ComPtr<ID3D12Resource> CreateTextureFromFile(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdlist,
	const std::wstring& filename,
	bool sRGB,
	ComPtr<ID3D12Resource>& uploadBuffer,
	DXGI_FORMAT& srvFormat,
	bool& isCubeMap)
{
	const std::wstring lowered = ToLower(filename);

	if (lowered.size() >= 4 && lowered.substr(lowered.size() - 4) == L".dds")
	{
		ComPtr<ID3D12Resource> texture = nullptr;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
			device,
			cmdlist,
			filename.c_str(),
			texture,
			uploadBuffer));

		srvFormat = texture->GetDesc().Format;
		isCubeMap = texture->GetDesc().DepthOrArraySize == 6;
		return texture;
	}

	const std::string narrowPath(filename.begin(), filename.end());
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load(narrowPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
	if (!pixels)
	{
		throw std::runtime_error("Failed to load texture with stb_image.");
	}

	const DXGI_FORMAT textureFormat = sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		textureFormat,
		static_cast<UINT64>(width),
		static_cast<UINT>(height),
		1,
		1);

	auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> texture = nullptr;
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(texture.GetAddressOf())));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));

	D3D12_SUBRESOURCE_DATA subresource = {};
	subresource.pData = pixels;
	subresource.RowPitch = static_cast<LONG_PTR>(width) * 4;
	subresource.SlicePitch = subresource.RowPitch * height;

	UpdateSubresources(cmdlist, texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresource);

	auto barrierShaderRead = CD3DX12_RESOURCE_BARRIER::Transition(
		texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdlist->ResourceBarrier(1, &barrierShaderRead);

	stbi_image_free(pixels);

	srvFormat = textureFormat;
	isCubeMap = false;
	return texture;
}
