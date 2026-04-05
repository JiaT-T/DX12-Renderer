#pragma once
#include "ToolFunc.h" 
using namespace Microsoft::WRL;

template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ComPtr<ID3D12Device> device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);
		if (isConstantBuffer)
			mElementByteSize = CalcConstantBufferByteSize(sizeof(T));
		//创建上传堆和资源
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount);
		ThrowIfFailed(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(mUploadBuffer.GetAddressOf())));
		//映射资源
		ThrowIfFailed(mUploadBuffer->Map(0, nullptr,
			reinterpret_cast<void**>(&mMappedData)));//返回一个指向资源内存的指针，这样就可以直接往显存里写数据
	}
	~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
			mUploadBuffer->Unmap(0, nullptr);
		mMappedData = nullptr;
	}
	//将CPU上的常量数据复制到GPU上的缓冲区中
	void CopyData(int mElementIndex, const T& Data)
	{
		memcpy(&mMappedData[mElementIndex * mElementByteSize], &Data, sizeof(T));
	}
	//返回上传的指针
	ComPtr<ID3D12Resource> GetResource()const
	{
		return mUploadBuffer;
	}
private:
	ComPtr<ID3D12Resource> mUploadBuffer;
	BYTE* mMappedData = nullptr;
	UINT mElementByteSize = 0;
	bool mIsConstantBuffer = false;
};