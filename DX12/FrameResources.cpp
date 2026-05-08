#include "FrameResources.h"
FrameResources::FrameResources(ID3D12Device* mDevice, UINT objCount, UINT materialCount, UINT passCount)
{
	ThrowIfFailed(mDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&mCmdAllocator)));
	objCB = std::make_unique<UploadBuffer<ObjectConstants>>(mDevice, objCount, true);
	matCB = std::make_unique<UploadBuffer<MatConstants>>(mDevice, materialCount, true);
	passCB = std::make_unique<UploadBuffer<PassConstants>>(mDevice, passCount, true);
}

FrameResources::~FrameResources() {}