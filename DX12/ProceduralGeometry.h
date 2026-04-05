#pragma once
#include <cstdint>	
#include <DirectXMath.h>
#include <vector>
class ProceduralGeometry
{
public :
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct Vertex
	{
		Vertex() {};
		Vertex(
			const DirectX::XMFLOAT3& p,
			const DirectX::XMFLOAT3& n,
			const DirectX::XMFLOAT3& t,
			const DirectX::XMFLOAT2& uv) :
			Position(p),
			Normal(n),
			TangentU(t),
			TexC(uv) {};
		Vertex(
			float px, float py, float pz,
			float nx, float ny, float nz,
			float tx, float ty, float tz,
			float u, float v) :
			Position(px, py, pz),
			Normal(nx, ny, nz),
			TangentU(tx, ty, tz),
			TexC(u, v) {};

		DirectX::XMFLOAT3 Position;		//物体坐标（物体空间）
		DirectX::XMFLOAT3 Normal;			//法线（物体空间）
		DirectX::XMFLOAT3 TangentU;			//切线（物体空间）
		DirectX::XMFLOAT2 TexC;			//纹理坐标（物体空间）
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;			//单个几何体的顶点数据
		std::vector<uint32> Indices32;      //单个几何体的32位索引数据

		//将32位索引数据转换为16位索引数据
		std::vector<uint16>& GetIndices16()
		{
			if (mIndices16.empty())
			{
				mIndices16.resize(Indices32.size());
				for (size_t i = 0; i < Indices32.size(); ++i)
				{
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
				}
			}
			return mIndices16;
		}

	private :
		std::vector<uint16> mIndices16;		//单个几何体的16位索引数据
	};

	//几何体创建函数
	ProceduralGeometry::MeshData CreateBox(float width, float height, float depth, uint32 numSubdivisions);
	ProceduralGeometry::MeshData CreateSphere(float radius, uint32 sliceCount, uint32 stackCount);
	ProceduralGeometry::MeshData CreateGeosphere(float radius, uint32 numSubdivisions);
	ProceduralGeometry::MeshData CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount);
	ProceduralGeometry::MeshData CreateGrid(float width, float depth, uint32 m, uint32 n);
	ProceduralGeometry::MeshData CreateQuad(float x, float y, float w, float h, float depth);

	void Subdivide(MeshData& meshData);
	Vertex MidPoint(const Vertex& v0, const Vertex& v1);
	void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
	void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount, MeshData& meshData);
};

