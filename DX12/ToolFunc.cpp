#include "ToolFunc.h"
#include <string>
DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNumber) :
	ErrorCode(hr),              //记录错误码
	FunctionName(functionName), //记录错误函数名
	FileName(fileName),         //记录错误文件名
	LineNumber(lineNumber)      //记录错误代码行数
{
}
//这个函数的作用就是把前面那个函数获取到的错误信息
//翻译成人能听懂的的话
std::wstring DxException::ToString()const
{
	//  _com_error 是 Windows 提供的一个神器类（来自 <comdef.h>）
	// 它的作用就是查字典：把 HRESULT 数字查表，找到对应的文字描述。
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	//std::to_wstring(...)：这是 C++ 标准库函数，它的作用是把数字转换成宽字符串。比如把数字 50 变成字符串 L"50"。
	return FunctionName + L" failed in " + FileName + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}

ComPtr<ID3DBlob> CompileShader(const std::wstring& fileName, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& target)
{
	//若处于调试模式，则使用调试标志
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	/*       用调试模式来编译着色器 | 指示编译器跳过优化阶段     */
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(
		fileName.c_str(),								//hlsl源文件
		defines,										  //高级选项
		D3D_COMPILE_STANDARD_FILE_INCLUDE,				  //高级选项
		entrypoint.c_str(),					  //着色器的入口点函数名
		target.c_str(),							  //着色器类型与版本
		compileFlags,					  //着色器应当如何编译的标志
		0,												  //高级选项
		&byteCode,							  //编译好的着色器字节码
		&errors);										  //错误信息
	if (errors != nullptr)
	{
		OutputDebugStringA((char*)errors->GetBufferPointer());
		MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "Shader Compilation Error", MB_OK | MB_ICONERROR);
	}
	ThrowIfFailed(hr);
	return byteCode;
};

//顶点数据传输函数
ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist, UINT byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer)
{
	//创建上传缓冲堆
	//CPU将数据写入这个堆，作为上传到GPU可直接访问的默认堆的中介
	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);

	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(uploadBuffer.GetAddressOf())));
	//创建默认缓冲堆，作为上传缓冲堆的数据传输对象
	//对于这个堆当中的内容，GPU可以高速访问
	auto defaultHeapPros = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ComPtr<ID3D12Resource> defaultBuffer;
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapPros,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));
	//将资源从COMMON状态转换到COPY_DEST状态
	auto barrierCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST);

	cmdlist->ResourceBarrier(
		1,
		&barrierCopyDest);
	//将数据从CPU内存拷贝到GPU内存
	D3D12_SUBRESOURCE_DATA subResourceData;
	subResourceData.pData = initData;					//指向初始化缓冲区所用数据的内存块的指针
	subResourceData.RowPitch = byteSize;									//欲拷贝数据的字节数
	subResourceData.SlicePitch = subResourceData.RowPitch;					//欲拷贝数据的字节数
	//核心函数UpdateSubresources
	//将数据先从CPU拷贝至上传堆，再从上传堆拷贝至默认堆
	UpdateSubresources<1>(cmdlist, defaultBuffer.Get(),
		uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	//拷贝完毕后，将资源从COPY_DEST状态转换到GENERIC_READ状态（只供着色器访问）
	auto barrierGenericRead = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdlist->ResourceBarrier(
		1,
		&barrierGenericRead);
	return defaultBuffer;
}
