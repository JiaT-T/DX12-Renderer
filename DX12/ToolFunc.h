#pragma once
#include <Windows.h> 
#include <string> 
#include <comdef.h> 
#include <wrl/client.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include"D3DApp.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;
using namespace DirectX;

#ifndef ThrowIfFailed
//给x加括号是为了保证优先级
#define ThrowIfFailed(x) {														 \
    /* 执行并抓取结果 */														 \
    HRESULT hr__ = (x);															 \
    /* 使用 __FILEW__ 直接获取宽字符文件名，不需要 AnsiToWString 转换 */		 \
    std::wstring wfn = __FILEW__;												 \
    /* FAILED是系统预定义的一个宏，他会对hresult最高位进行检查（是不是1） */     \
    /* 如果是1，说明失败了，进入循环 */											 \
    /* 当throw被触发后，会直接进入catch代码块，对报错进行处理 */				 \
    if (FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); }			 \
}
#endif

//AnsiToWString函数（转换成宽字符类型的字符串，wstring）
inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

//异常类
class DxException
{
public:
	//生成默认构造函数
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& fileName, int lineNumber);

	std::wstring ToString()const;

	HRESULT ErrorCode = S_OK;  //报错的原因代码
	std::wstring FunctionName; //哪个函数出错了
	std::wstring FileName;     //在哪个源文件里
	int LineNumber = -1;       //在第几行
};

ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdlist, UINT byteSize, const void* initData, ComPtr<ID3D12Resource>& uploadBuffer);

ComPtr<ID3DBlob> CompileShader(const std::wstring& fileName, const D3D_SHADER_MACRO* defines, const std::string& entrypoint, const std::string& target);

//常量缓冲区必须是256b的倍数
//此函数用于计算距离当前缓冲区大小最近的256倍数
inline UINT CalcConstantBufferByteSize(UINT byteSize)
{
	return (byteSize + 255) & ~255;
};

struct Material
{
	std::string name;													//材质名字
	int          matCBIndex = -1;						    //材质常量缓冲区的索引
	int diffuseSrvHeapIndex = -1;						 //漫反射纹理在srv堆的索引
	int      numFramesDirty = 3;//已更新标志，表示材质有变动，需要更新常量缓冲区了

	XMFLOAT4 diffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };				  //反照率
	XMFLOAT3     fresnelR0 = { 0.01f, 0.01f, 0.01f };			 //RF0值，反射属性
	float        roughness = 0.25f;										  //粗糙度
};

struct Light
{
	XMFLOAT3  strength = { 0.5f, 0.5f, 0.5f };			 //光源颜色（三光通用）
	float falloffStart = 1.0f;					//点光灯和聚光灯的开始衰减距离
	XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };		//方向光和聚光灯的方向向量
	float   falloffEnd = 10.0f;					  //点光和聚光灯的衰减结束距离
	XMFLOAT3  position = { 0.0f, 0.0f, 0.0f };			  //点光和聚光灯的坐标
	float    spotPower = 64.0f;							  //聚光灯因子中的参数
};

struct Texture
{
	std::string name;												//纹理名
	std::wstring filename;							  //纹理所在路径的目录名
	ComPtr<ID3D12Resource> resource = nullptr;				//返回的纹理资源
	ComPtr<ID3D12Resource> uploadHeap = nullptr;  //返回的上传堆中的纹理资源
};