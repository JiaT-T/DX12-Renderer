#define TINYOBJLOADER_IMPLEMENTATION
#include "third_party/tiny_obj_loader.h"

#include "ShapesApp.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <commdlg.h>
#include <filesystem>
#include <map>
#include <vector>
#include <wincodec.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "ImGuizmo.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace DirectX;
using namespace Microsoft::WRL;

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	XMFLOAT4 ComputeFallbackTangent(const XMFLOAT3& normal)
	{
		const XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
		const XMVECTOR up = fabsf(normal.y) < 0.999f
			? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
			: XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		const XMVECTOR tangent = XMVector3Normalize(XMVector3Cross(up, n));

		XMFLOAT3 tangent3;
		XMStoreFloat3(&tangent3, tangent);
		return XMFLOAT4(tangent3.x, tangent3.y, tangent3.z, 1.0f);
	}

	XMFLOAT4 ComputeTriangleTangent(
		const XMFLOAT3& p0,
		const XMFLOAT3& p1,
		const XMFLOAT3& p2,
		const XMFLOAT2& uv0,
		const XMFLOAT2& uv1,
		const XMFLOAT2& uv2,
		const XMFLOAT3& normal)
	{
		const XMVECTOR P0 = XMLoadFloat3(&p0);
		const XMVECTOR P1 = XMLoadFloat3(&p1);
		const XMVECTOR P2 = XMLoadFloat3(&p2);

		const XMFLOAT3 edge1 = XMFLOAT3(p1.x - p0.x, p1.y - p0.y, p1.z - p0.z);
		const XMFLOAT3 edge2 = XMFLOAT3(p2.x - p0.x, p2.y - p0.y, p2.z - p0.z);
		const float du1 = uv1.x - uv0.x;
		const float dv1 = uv1.y - uv0.y;
		const float du2 = uv2.x - uv0.x;
		const float dv2 = uv2.y - uv0.y;
		const float det = du1 * dv2 - dv1 * du2;

		if (fabsf(det) < 1e-6f)
		{
			return ComputeFallbackTangent(normal);
		}

		const float invDet = 1.0f / det;
		XMFLOAT3 tangent = {
			(edge1.x * dv2 - edge2.x * dv1) * invDet,
			(edge1.y * dv2 - edge2.y * dv1) * invDet,
			(edge1.z * dv2 - edge2.z * dv1) * invDet
		};
		XMFLOAT3 bitangent = {
			(edge2.x * du1 - edge1.x * du2) * invDet,
			(edge2.y * du1 - edge1.y * du2) * invDet,
			(edge2.z * du1 - edge1.z * du2) * invDet
		};

		XMVECTOR tangentV = XMVector3Normalize(XMLoadFloat3(&tangent));
		const XMVECTOR normalV = XMVector3Normalize(XMLoadFloat3(&normal));
		const XMVECTOR bitangentV = XMLoadFloat3(&bitangent);
		tangentV = XMVector3Normalize(tangentV - XMVector3Dot(tangentV, normalV) * normalV);

		XMFLOAT3 tangent3;
		XMStoreFloat3(&tangent3, tangentV);
		const float handedness = XMVectorGetX(XMVector3Dot(XMVector3Cross(normalV, tangentV), bitangentV)) < 0.0f ? -1.0f : 1.0f;
		return XMFLOAT4(tangent3.x, tangent3.y, tangent3.z, handedness);
	}

	std::string SanitizeKey(const std::string& text)
	{
		std::string result;
		result.reserve(text.size());
		bool lastWasSeparator = false;

		for (unsigned char ch : text)
		{
			if (std::isalnum(ch))
			{
				result.push_back(static_cast<char>(ch));
				lastWasSeparator = false;
			}
			else if (!lastWasSeparator)
			{
				result.push_back('_');
				lastWasSeparator = true;
			}
		}

		while (!result.empty() && result.back() == '_')
		{
			result.pop_back();
		}

		return result.empty() ? "default" : result;
	}

	std::string ToLowerAscii(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
		return text;
	}

	std::filesystem::path NormalizeObjTexturePath(const std::string& textureName)
	{
		std::string normalized = textureName;
		std::replace(normalized.begin(), normalized.end(), '\\', '/');
		return std::filesystem::path(normalized);
	}

	std::filesystem::path ResolveObjTexturePath(
		const std::filesystem::path& objDirectory,
		const std::string& textureName)
	{
		if (textureName.empty())
		{
			return {};
		}

		const std::filesystem::path rawPath = NormalizeObjTexturePath(textureName);
		if (rawPath.is_absolute() && std::filesystem::exists(rawPath))
		{
			return rawPath;
		}

		const std::filesystem::path candidates[] =
		{
			objDirectory / rawPath,
			objDirectory / rawPath.filename(),
			objDirectory / "textures" / rawPath.filename(),
			objDirectory.parent_path() / rawPath,
			objDirectory.parent_path() / rawPath.filename(),
			objDirectory.parent_path() / "textures" / rawPath.filename()
		};

		for (const auto& candidate : candidates)
		{
			if (std::filesystem::exists(candidate))
			{
				return candidate;
			}
		}

		return objDirectory / rawPath;
	}

	bool ShouldFlipNormalY(const std::string& textureName)
	{
		const std::string lowered = ToLowerAscii(textureName);
		return lowered.find("normalgl") != std::string::npos ||
			lowered.find("normal_gl") != std::string::npos ||
			lowered.find("opengl") != std::string::npos;
	}

	void ExpandBounds(ShapesApp::Bounds& bounds, const XMFLOAT3& position)
	{
		if (!bounds.valid)
		{
			bounds.min = position;
			bounds.max = position;
			bounds.valid = true;
			return;
		}

		bounds.min.x = std::min(bounds.min.x, position.x);
		bounds.min.y = std::min(bounds.min.y, position.y);
		bounds.min.z = std::min(bounds.min.z, position.z);
		bounds.max.x = std::max(bounds.max.x, position.x);
		bounds.max.y = std::max(bounds.max.y, position.y);
		bounds.max.z = std::max(bounds.max.z, position.z);
	}

	void FinalizeBounds(ShapesApp::Bounds& bounds, const std::vector<ShapesApp::Vertex1>& vertices)
	{
		if (!bounds.valid)
		{
			return;
		}

		bounds.center = XMFLOAT3(
			0.5f * (bounds.min.x + bounds.max.x),
			0.5f * (bounds.min.y + bounds.max.y),
			0.5f * (bounds.min.z + bounds.max.z));

		float radiusSq = 0.0f;
		const XMVECTOR center = XMLoadFloat3(&bounds.center);
		for (const auto& vertex : vertices)
		{
			const XMVECTOR p = XMLoadFloat3(&vertex.Pos);
			const XMVECTOR d = p - center;
			radiusSq = std::max(radiusSq, XMVectorGetX(XMVector3LengthSq(d)));
		}
		bounds.radius = std::sqrt(radiusSq);
	}
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
	ShutdownEditorUI();
}

bool ShapesApp::InitRnederItems(HINSTANCE hInstance, int nShowCmd, std::wstring customCaption)
{
	if (!D3DApp::Init(hInstance, nShowCmd, customCaption))
	{
		return false;
	}

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mTheta = 1.45f * XM_PI;
	mPhi = 0.35f * XM_PI;
	mRadius = 14.0f;
	sunTheta = 1.15f * XM_PI;
	sunPhi = 0.18f * XM_PI;

	BuildRootSignature();
	BuildByteCodeAndInputLayout();
	BuildShapeGeometry();
	LoadTextures();
	BuildMaterial();
	BuildStaticSceneModels();
	BuildRenderItems();
	BuildFrameResource();
	BuildShadowMapResources();
	BuildBrdfLutResources();
	BuildPrefilteredEnvironmentMapResources();
	BuildIrradianceMapResources();

	mCurrentFrameResourcesIndex = 0;
	mCurrentFrameResources = mFrameResourcesArray[mCurrentFrameResourcesIndex].get();

	BuildShaderResourceView();
	BuildPSO();
	RenderBrdfLut();
	RenderPrefilteredEnvironmentMap();
	RenderIrradianceMap();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
	FlushCommandQueue();

	InitEditorUI();

	mAppInitialized = true;
	return true;
}

void ShapesApp::Draw()
{
	auto currentCmdAllocator = mCurrentFrameResources->mCmdAllocator;
	ThrowIfFailed(currentCmdAllocator->Reset());
	ThrowIfFailed(mCommandList->Reset(currentCmdAllocator.Get(), mPSOs["opaque"].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	const D3D12_GPU_VIRTUAL_ADDRESS passCBAddress =
		mCurrentFrameResources->passCB->GetResource()->GetGPUVirtualAddress();

	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
	DrawSceneToShadowMap();

	auto barrierToRender = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRender);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrentBackBuffer,
		mRtvDescriptorSize);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	const float clearColor[] = { 0.08f, 0.10f, 0.14f, 1.0f };
	mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr);

	mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

	mCommandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Sky]);

	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
	shadowSrvHandle.Offset(mShadowMapSrvHeapIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(8, shadowSrvHandle);

	mCommandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Opaque]);

	DrawEditorUI();
	const bool frameCaptureScheduled = ScheduleFrameCapture();

	auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChainBuffer[mCurrentBackBuffer].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &barrierToPresent);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrentBackBuffer = (mCurrentBackBuffer + 1) % 2;

	mCurrentFrameResources->mFenceCPU = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);

	if (frameCaptureScheduled)
	{
		FlushCommandQueue();
		WritePendingFrameCaptureToPng();
	}
}

LRESULT ShapesApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (mEditorInitialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
	{
		return true;
	}

	return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if (EditorWantsMouse())
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;
		return;
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	if (EditorWantsMouse())
	{
		ReleaseCapture();
		return;
	}

	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (EditorWantsMouse())
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;
		return;
	}

	const float dx = static_cast<float>(x - mLastMousePos.x);
	const float dy = static_cast<float>(y - mLastMousePos.y);
	const float moveUnitsPerPixel = GetCameraMoveUnitsPerPixel();

	if ((btnState & MK_RBUTTON) != 0)
	{
		constexpr float rotateRadiansPerPixel = 0.005f;
		mCameraYaw -= dx * rotateRadiansPerPixel;
		mCameraPitch -= dy * rotateRadiansPerPixel;
		mCameraPitch = MathHelper::Clamp(
			mCameraPitch,
			XMConvertToRadians(-89.0f),
			XMConvertToRadians(89.0f));
	}
	else if ((btnState & MK_LBUTTON) != 0)
	{
		const XMVECTOR forward = GetCameraForwardVector();
		const XMVECTOR position = XMLoadFloat3(&mCameraPosition);
		XMStoreFloat3(&mCameraPosition, position + forward * (-dy * moveUnitsPerPixel));
	}
	else if ((btnState & MK_MBUTTON) != 0)
	{
		const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		const XMVECTOR position = XMLoadFloat3(&mCameraPosition);
		XMStoreFloat3(&mCameraPosition, position + up * (-dy * moveUnitsPerPixel));
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::InitEditorUI()
{
	if (mEditorInitialized)
	{
		return;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mImGuiSrvHeap)));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(mhMainWnd);
	ImGui_ImplDX12_Init(
		md3dDevice.Get(),
		static_cast<int>(mFrameResourcesCount),
		DXGI_FORMAT_R8G8B8A8_UNORM,
		mImGuiSrvHeap.Get(),
		mImGuiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
		mImGuiSrvHeap->GetGPUDescriptorHandleForHeapStart());

	mEditorInitialized = true;
}

void ShapesApp::ShutdownEditorUI()
{
	if (!mEditorInitialized)
	{
		return;
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	mEditorInitialized = false;
}

void ShapesApp::DrawEditorUI()
{
	if (!mEditorInitialized)
	{
		return;
	}

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();

	if (mEditorVisible)
	{
		DrawDebugWindow();
	}
	DrawTransformGizmo();

	ImGui::Render();
	ID3D12DescriptorHeap* descriptorHeaps[] = { mImGuiSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
}

void ShapesApp::DrawDebugWindow()
{
	ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(320.0f, 360.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Renderer Debug", &mEditorVisible, ImGuiWindowFlags_NoCollapse);
	ImGui::Text("Frame %.3f ms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::Text("Scene items: %d", static_cast<int>(mAllItem.size()));
	ImGui::Text("Objects: %d", static_cast<int>(mSceneObjects.size()));

	if (ImGui::Button("Import OBJ"))
	{
		mOpenImportDialogRequested = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Save Current Frame"))
	{
		QueueFrameCapture();
	}

	ImGui::SliderAngle("Light Yaw", &sunTheta, -180.0f, 180.0f);
	ImGui::SliderAngle("Light Pitch", &sunPhi, 5.0f, 90.0f);
	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);

	if (ImGui::RadioButton("Move", mTransformTool == TransformTool::Translate))
	{
		mTransformTool = TransformTool::Translate;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mTransformTool == TransformTool::Rotate))
	{
		mTransformTool = TransformTool::Rotate;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mTransformTool == TransformTool::Scale))
	{
		mTransformTool = TransformTool::Scale;
	}
	ImGui::Checkbox("Local space", &mUseLocalGizmoMode);

	if (ImGui::BeginListBox("Object", ImVec2(-FLT_MIN, 120.0f)))
	{
		for (int i = 0; i < static_cast<int>(mSceneObjects.size()); ++i)
		{
			const bool selected = i == mSelectedSceneObjectIndex;
			if (ImGui::Selectable(mSceneObjects[i].name.c_str(), selected))
			{
				mSelectedSceneObjectIndex = i;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndListBox();
	}

	if (SceneObject* selectedObject = GetSelectedSceneObject())
	{
		float position[3] =
		{
			selectedObject->position.x,
			selectedObject->position.y,
			selectedObject->position.z
		};
		float rotationDegrees[3] =
		{
			XMConvertToDegrees(selectedObject->rotation.x),
			XMConvertToDegrees(selectedObject->rotation.y),
			XMConvertToDegrees(selectedObject->rotation.z)
		};
		float scale[3] =
		{
			selectedObject->scale.x,
			selectedObject->scale.y,
			selectedObject->scale.z
		};

		bool changed = false;
		changed |= ImGui::InputFloat3("Position", position, "%.3f");
		changed |= ImGui::InputFloat3("Rotation", rotationDegrees, "%.3f");
		changed |= ImGui::InputFloat3("Scale", scale, "%.3f");

		if (changed)
		{
			selectedObject->position = XMFLOAT3(position[0], position[1], position[2]);
			selectedObject->rotation = XMFLOAT3(
				XMConvertToRadians(rotationDegrees[0]),
				XMConvertToRadians(rotationDegrees[1]),
				XMConvertToRadians(rotationDegrees[2]));
			selectedObject->scale = XMFLOAT3(
				std::max(0.001f, scale[0]),
				std::max(0.001f, scale[1]),
				std::max(0.001f, scale[2]));
			ApplySceneObjectTransform(*selectedObject);
		}
	}
	if (!mEditorStatus.empty())
	{
		ImGui::Separator();
		ImGui::TextWrapped("%s", mEditorStatus.c_str());
	}
	ImGui::End();
}

void ShapesApp::DrawTransformGizmo()
{
	SceneObject* selectedObject = GetSelectedSceneObject();
	if (selectedObject == nullptr)
	{
		return;
	}

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
	ImGuizmo::SetRect(0.0f, 0.0f, static_cast<float>(clientWidth), static_cast<float>(clientHeight));

	ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
	switch (mTransformTool)
	{
	case TransformTool::Translate:
		operation = ImGuizmo::TRANSLATE;
		break;
	case TransformTool::Rotate:
		operation = ImGuizmo::ROTATE;
		break;
	case TransformTool::Scale:
		operation = ImGuizmo::SCALE;
		break;
	default:
		break;
	}

	ImGuizmo::MODE mode = mUseLocalGizmoMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	if (operation == ImGuizmo::SCALE)
	{
		mode = ImGuizmo::LOCAL;
	}

	float objectMatrix[16] = {};
	std::copy(
		&selectedObject->world.m[0][0],
		&selectedObject->world.m[0][0] + 16,
		objectMatrix);

	if (ImGuizmo::Manipulate(
		&mView.m[0][0],
		&mProj.m[0][0],
		operation,
		mode,
		objectMatrix))
	{
		float position[3] = {};
		float rotationDegrees[3] = {};
		float scale[3] = {};
		ImGuizmo::DecomposeMatrixToComponents(objectMatrix, position, rotationDegrees, scale);

		selectedObject->position = XMFLOAT3(position[0], position[1], position[2]);
		selectedObject->rotation = XMFLOAT3(
			XMConvertToRadians(rotationDegrees[0]),
			XMConvertToRadians(rotationDegrees[1]),
			XMConvertToRadians(rotationDegrees[2]));
		selectedObject->scale = XMFLOAT3(
			std::max(0.001f, scale[0]),
			std::max(0.001f, scale[1]),
			std::max(0.001f, scale[2]));
		ApplySceneObjectTransform(*selectedObject);
	}
}

bool ShapesApp::EditorWantsMouse() const
{
	if (!mEditorInitialized || ImGui::GetCurrentContext() == nullptr)
	{
		return false;
	}

	return ImGui::GetIO().WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

void ShapesApp::ProcessEditorCommands()
{
	if (!mOpenImportDialogRequested)
	{
		return;
	}

	mOpenImportDialogRequested = false;

	std::wstring objPath;
	if (!TryOpenObjFile(objPath))
	{
		mEditorStatus = "Import canceled";
		return;
	}

	if (ImportObjModelAtRuntime(objPath))
	{
		mEditorStatus = "Imported " + std::filesystem::path(objPath).filename().string();
	}
	else
	{
		mEditorStatus = "Failed to import OBJ";
	}
}

bool ShapesApp::TryOpenObjFile(std::wstring& outPath) const
{
	wchar_t filename[MAX_PATH] = {};
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = mhMainWnd;
	ofn.lpstrFilter = L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = _countof(filename);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameW(&ofn))
	{
		return false;
	}

	outPath = filename;
	return true;
}

bool ShapesApp::ImportObjModelAtRuntime(const std::wstring& objPath)
{
	try
	{
		FlushCommandQueue();
		ThrowIfFailed(mDirectCmdListAlloc->Reset());
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

		const std::filesystem::path path(objPath);
		const std::string stem = SanitizeKey(path.stem().string());
		const std::string baseName = "import_" + std::to_string(++mImportedModelCounter) + "_" + (stem.empty() ? "model" : stem);
		const std::string geometryName = baseName + "Geo";

		const bool built = BuildObjModel(objPath, geometryName, baseName);

		ThrowIfFailed(mCommandList->Close());
		if (!built)
		{
			return false;
		}

		ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
		FlushCommandQueue();

		BuildRenderItems();
		BuildFrameResource();
		BuildShaderResourceView();

		for (int i = 0; i < static_cast<int>(mSceneObjects.size()); ++i)
		{
			if (mSceneObjects[i].name == geometryName)
			{
				mSelectedSceneObjectIndex = i;
				break;
			}
		}

		return true;
	}
	catch (const std::exception& e)
	{
		mEditorStatus = std::string("Import error: ") + e.what();
		return false;
	}
}

void ShapesApp::QueueFrameCapture()
{
	if (mFrameCaptureRequested || mPendingFrameCapture.valid)
	{
		mEditorStatus = "Frame capture already pending";
		return;
	}

	mPendingFrameCapture.filename = MakeFrameCapturePath();
	mFrameCaptureRequested = true;
	mEditorStatus = "Frame capture queued";
}

bool ShapesApp::ScheduleFrameCapture()
{
	if (!mFrameCaptureRequested)
	{
		return false;
	}

	mFrameCaptureRequested = false;
	mPendingFrameCapture.valid = false;

	ID3D12Resource* backBuffer = mSwapChainBuffer[mCurrentBackBuffer].Get();
	const D3D12_RESOURCE_DESC backBufferDesc = backBuffer->GetDesc();
	if (backBufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
	{
		mEditorStatus = "Frame capture failed: unsupported back buffer format";
		return false;
	}

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	UINT64 totalBytes = 0;
	md3dDevice->GetCopyableFootprints(
		&backBufferDesc,
		0,
		1,
		0,
		&footprint,
		&numRows,
		&rowSizeInBytes,
		&totalBytes);

	const CD3DX12_HEAP_PROPERTIES readbackHeapProperties(D3D12_HEAP_TYPE_READBACK);
	const CD3DX12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&readbackHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&readbackDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mPendingFrameCapture.readbackBuffer)));

	auto barrierToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	mCommandList->ResourceBarrier(1, &barrierToCopy);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = mPendingFrameCapture.readbackBuffer.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = backBuffer;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	auto barrierToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRenderTarget);

	mPendingFrameCapture.footprint = footprint;
	mPendingFrameCapture.width = static_cast<UINT>(backBufferDesc.Width);
	mPendingFrameCapture.height = backBufferDesc.Height;
	mPendingFrameCapture.valid = true;
	return true;
}

bool ShapesApp::WritePendingFrameCaptureToPng()
{
	if (!mPendingFrameCapture.valid || mPendingFrameCapture.readbackBuffer == nullptr)
	{
		return false;
	}

	HRESULT coInitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool shouldUninitializeCom = SUCCEEDED(coInitHr);
	if (FAILED(coInitHr) && coInitHr != RPC_E_CHANGED_MODE)
	{
		mEditorStatus = "Frame capture failed: COM initialization failed";
		mPendingFrameCapture.valid = false;
		return false;
	}

	ComPtr<IWICImagingFactory> factory;
	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory2,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&factory));
	if (FAILED(hr))
	{
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory));
	}

	bool saved = false;
	if (SUCCEEDED(hr))
	{
		std::uint8_t* mappedData = nullptr;
		const size_t compactRowPitch = static_cast<size_t>(mPendingFrameCapture.width) * 4;
		std::vector<std::uint8_t> pixels(compactRowPitch * mPendingFrameCapture.height);

		const D3D12_RANGE readRange =
		{
			static_cast<SIZE_T>(mPendingFrameCapture.footprint.Offset),
			static_cast<SIZE_T>(mPendingFrameCapture.footprint.Offset + static_cast<UINT64>(mPendingFrameCapture.footprint.Footprint.RowPitch) * mPendingFrameCapture.height)
		};
		hr = mPendingFrameCapture.readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
		if (SUCCEEDED(hr))
		{
			const std::uint8_t* srcBase = mappedData + mPendingFrameCapture.footprint.Offset;
			for (UINT y = 0; y < mPendingFrameCapture.height; ++y)
			{
				const std::uint8_t* srcRow = srcBase + static_cast<size_t>(y) * mPendingFrameCapture.footprint.Footprint.RowPitch;
				std::uint8_t* dstRow = pixels.data() + static_cast<size_t>(y) * compactRowPitch;
				std::copy(srcRow, srcRow + compactRowPitch, dstRow);
			}
			const D3D12_RANGE emptyWriteRange = { 0, 0 };
			mPendingFrameCapture.readbackBuffer->Unmap(0, &emptyWriteRange);

			ComPtr<IWICStream> stream;
			ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;
			ComPtr<IPropertyBag2> propertyBag;
			WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppRGBA;

			hr = factory->CreateStream(&stream);
			if (SUCCEEDED(hr))
			{
				hr = stream->InitializeFromFilename(mPendingFrameCapture.filename.c_str(), GENERIC_WRITE);
			}
			if (SUCCEEDED(hr))
			{
				hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
			}
			if (SUCCEEDED(hr))
			{
				hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
			}
			if (SUCCEEDED(hr))
			{
				hr = encoder->CreateNewFrame(&frame, &propertyBag);
			}
			if (SUCCEEDED(hr))
			{
				hr = frame->Initialize(propertyBag.Get());
			}
			if (SUCCEEDED(hr))
			{
				hr = frame->SetSize(mPendingFrameCapture.width, mPendingFrameCapture.height);
			}
			if (SUCCEEDED(hr))
			{
				hr = frame->SetPixelFormat(&pixelFormat);
			}
			if (SUCCEEDED(hr) && pixelFormat == GUID_WICPixelFormat32bppRGBA)
			{
				hr = frame->WritePixels(
					mPendingFrameCapture.height,
					static_cast<UINT>(compactRowPitch),
					static_cast<UINT>(pixels.size()),
					pixels.data());
			}
			if (SUCCEEDED(hr))
			{
				hr = frame->Commit();
			}
			if (SUCCEEDED(hr))
			{
				hr = encoder->Commit();
			}

			saved = SUCCEEDED(hr);
		}
	}

	if (saved)
	{
		mEditorStatus = "Saved frame to " + std::filesystem::path(mPendingFrameCapture.filename).generic_string();
	}
	else
	{
		mEditorStatus = "Frame capture failed";
	}

	mPendingFrameCapture.valid = false;
	mPendingFrameCapture.readbackBuffer.Reset();

	if (shouldUninitializeCom)
	{
		CoUninitialize();
	}

	return saved;
}

std::wstring ShapesApp::MakeFrameCapturePath() const
{
	std::filesystem::path captureDir = std::filesystem::current_path() / L"Captures";
	std::filesystem::create_directories(captureDir);

	SYSTEMTIME localTime = {};
	GetLocalTime(&localTime);

	wchar_t filename[128] = {};
	swprintf_s(
		filename,
		L"frame_%04hu%02hu%02hu_%02hu%02hu%02hu.png",
		localTime.wYear,
		localTime.wMonth,
		localTime.wDay,
		localTime.wHour,
		localTime.wMinute,
		localTime.wSecond);

	return (captureDir / filename).wstring();
}

void ShapesApp::RegisterSceneObject(
	const std::string& name,
	const std::vector<RenderItem*>& renderItems,
	const XMFLOAT3& position,
	const XMFLOAT3& rotation,
	const XMFLOAT3& scale)
{
	if (renderItems.empty())
	{
		return;
	}

	SceneObject sceneObject;
	sceneObject.name = name;
	sceneObject.renderItems = renderItems;
	sceneObject.position = position;
	sceneObject.rotation = rotation;
	sceneObject.scale = scale;
	mSceneObjects.push_back(sceneObject);
	ApplySceneObjectTransform(mSceneObjects.back());
}

void ShapesApp::ApplySceneObjectTransform(SceneObject& sceneObject)
{
	const XMMATRIX objectWorld =
		XMMatrixScaling(sceneObject.scale.x, sceneObject.scale.y, sceneObject.scale.z) *
		XMMatrixRotationRollPitchYaw(sceneObject.rotation.x, sceneObject.rotation.y, sceneObject.rotation.z) *
		XMMatrixTranslation(sceneObject.position.x, sceneObject.position.y, sceneObject.position.z);

	XMStoreFloat4x4(&sceneObject.world, objectWorld);

	for (RenderItem* renderItem : sceneObject.renderItems)
	{
		if (renderItem == nullptr)
		{
			continue;
		}

		const XMMATRIX localWorld = XMLoadFloat4x4(&renderItem->localWorld);
		XMStoreFloat4x4(&renderItem->world, localWorld * objectWorld);
		renderItem->numFramesDirty = static_cast<int>(mFrameResourcesCount);
	}
}

ShapesApp::SceneObject* ShapesApp::GetSelectedSceneObject()
{
	if (mSelectedSceneObjectIndex < 0 || mSelectedSceneObjectIndex >= static_cast<int>(mSceneObjects.size()))
	{
		return nullptr;
	}

	return &mSceneObjects[mSelectedSceneObjectIndex];
}

void ShapesApp::ResetCameraToSceneView()
{
	const float x = mRadius * sinf(mPhi) * cosf(mTheta);
	const float y = mRadius * cosf(mPhi);
	const float z = mRadius * sinf(mPhi) * sinf(mTheta);

	const XMVECTOR target = XMLoadFloat3(&mSceneBoundsCenter);
	const XMVECTOR eyePos = target + XMVectorSet(x, y, z, 0.0f);
	XMStoreFloat3(&mCameraPosition, eyePos);

	const XMVECTOR forward = XMVector3Normalize(target - eyePos);
	XMFLOAT3 forward3 = {};
	XMStoreFloat3(&forward3, forward);

	mCameraYaw = std::atan2(forward3.z, forward3.x);
	mCameraPitch = std::asin(MathHelper::Clamp(forward3.y, -1.0f, 1.0f));
}

XMVECTOR ShapesApp::GetCameraForwardVector() const
{
	const float cosPitch = cosf(mCameraPitch);
	const XMVECTOR forward = XMVectorSet(
		cosPitch * cosf(mCameraYaw),
		sinf(mCameraPitch),
		cosPitch * sinf(mCameraYaw),
		0.0f);
	return XMVector3Normalize(forward);
}

float ShapesApp::GetCameraMoveUnitsPerPixel() const
{
	return std::max(0.01f, mSceneBoundsRadius * 0.0025f);
}

void ShapesApp::DrawSceneToShadowMap()
{
	auto barrierToDepthWrite = CD3DX12_RESOURCE_BARRIER::Transition(
		mShadowMap.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->ResourceBarrier(1, &barrierToDepthWrite);

	mCommandList->RSSetViewports(1, &mShadowViewport);
	mCommandList->RSSetScissorRects(1, &mShadowScissorRect);

	D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = mShadowMapDsvHeap->GetCPUDescriptorHandleForHeapStart();
	mCommandList->ClearDepthStencilView(shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(0, nullptr, false, &shadowDsvHandle);

	mCommandList->SetPipelineState(mPSOs["shadowMap"].Get());
	DrawRenderItems(mRitemLayer[(int)RenderLayer::Opaque]);

	auto barrierToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
		mShadowMap.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &barrierToShaderResource);
}

void ShapesApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
{
	const UINT objConstSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	const UINT matConstSize = CalcConstantBufferByteSize(sizeof(MatConstants));

	auto makeSrvHandle = [this](int srvIndex)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		handle.Offset(srvIndex, mCbvSrvUavDescriptorSize);
		return handle;
	};

	for (RenderItem* ritem : ritems)
	{
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
		D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

		if (ritem->geo != nullptr)
		{
			vertexBufferView.BufferLocation = ritem->geo->mVertexBufferGPU->GetGPUVirtualAddress();
			vertexBufferView.StrideInBytes = ritem->geo->mVertexByteStride;
			vertexBufferView.SizeInBytes = ritem->geo->mVertexBufferByteSize;

			indexBufferView.BufferLocation = ritem->geo->mIndexBufferGPU->GetGPUVirtualAddress();
			indexBufferView.Format = ritem->geo->mIndexFormat;
			indexBufferView.SizeInBytes = ritem->geo->mIndexBufferByteSize;
		}
		else
		{
			vertexBufferView = GetVBV();
			indexBufferView = GetIBV();
		}

		mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		mCommandList->IASetIndexBuffer(&indexBufferView);
		mCommandList->IASetPrimitiveTopology(ritem->mPrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = mCurrentFrameResources->objCB->GetResource()->GetGPUVirtualAddress();
		objCBAddress += static_cast<UINT64>(ritem->mObjCBIndex) * objConstSize;
		mCommandList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		mCommandList->SetGraphicsRootDescriptorTable(7, makeSrvHandle(textures[mEnvironmentTextureName]->srvHeapIndex));
		mCommandList->SetGraphicsRootDescriptorTable(9, makeSrvHandle(mBrdfLutSrvHeapIndex));
		mCommandList->SetGraphicsRootDescriptorTable(10, makeSrvHandle(mPrefilteredEnvMapSrvHeapIndex));
		mCommandList->SetGraphicsRootDescriptorTable(11, makeSrvHandle(mIrradianceMapSrvHeapIndex));

		if (ritem->mat != nullptr)
		{
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = mCurrentFrameResources->matCB->GetResource()->GetGPUVirtualAddress();
			matCBAddress += static_cast<UINT64>(ritem->mat->matCBIndex) * matConstSize;
			mCommandList->SetGraphicsRootConstantBufferView(1, matCBAddress);

			mCommandList->SetGraphicsRootDescriptorTable(3, makeSrvHandle(ritem->mat->baseColorSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(4, makeSrvHandle(ritem->mat->normalSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(5, makeSrvHandle(ritem->mat->roughnessSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(6, makeSrvHandle(ritem->mat->metallicSrvHeapIndex));
			mCommandList->SetGraphicsRootDescriptorTable(13, makeSrvHandle(ritem->mat->alphaSrvHeapIndex));
		}

		mCommandList->DrawIndexedInstanced(
			ritem->indexCount,
			1,
			ritem->startIndexLocation,
			ritem->baseVertexLocation,
			0);
	}
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();
	UpdateProjectionMatrix();
}

void ShapesApp::UpdateProjectionMatrix()
{
	const float aspect = clientHeight > 0
		? static_cast<float>(clientWidth) / static_cast<float>(clientHeight)
		: 1.0f;
	const float farZ = std::max(1000.0f, mSceneBoundsRadius * 6.0f);
	const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, aspect, 0.1f, farZ);
	XMStoreFloat4x4(&mProj, proj);
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER rootParameters[14];
	rootParameters[0].InitAsConstantBufferView(0);
	rootParameters[1].InitAsConstantBufferView(1);
	rootParameters[2].InitAsConstantBufferView(2);

	CD3DX12_DESCRIPTOR_RANGE baseColorSrvTable;
	CD3DX12_DESCRIPTOR_RANGE normalSrvTable;
	CD3DX12_DESCRIPTOR_RANGE roughnessSrvTable;
	CD3DX12_DESCRIPTOR_RANGE metallicSrvTable;
	CD3DX12_DESCRIPTOR_RANGE envSrvTable;
	CD3DX12_DESCRIPTOR_RANGE shadowMapSrvTable;
	CD3DX12_DESCRIPTOR_RANGE brdfLutSrvTable;
	CD3DX12_DESCRIPTOR_RANGE prefilteredEnvSrvTable;
	CD3DX12_DESCRIPTOR_RANGE irradianceMapSrvTable;
	CD3DX12_DESCRIPTOR_RANGE alphaSrvTable;

	baseColorSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	normalSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	roughnessSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
	metallicSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
	envSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
	shadowMapSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
	brdfLutSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
	prefilteredEnvSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);
	irradianceMapSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
	alphaSrvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);

	rootParameters[3].InitAsDescriptorTable(1, &baseColorSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[4].InitAsDescriptorTable(1, &normalSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[5].InitAsDescriptorTable(1, &roughnessSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[6].InitAsDescriptorTable(1, &metallicSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[7].InitAsDescriptorTable(1, &envSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[8].InitAsDescriptorTable(1, &shadowMapSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[9].InitAsDescriptorTable(1, &brdfLutSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[10].InitAsDescriptorTable(1, &prefilteredEnvSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[11].InitAsDescriptorTable(1, &irradianceMapSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[12].InitAsConstants(2, 3, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[13].InitAsDescriptorTable(1, &alphaSrvTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
		_countof(rootParameters),
		rootParameters,
		static_cast<UINT>(staticSamplers.size()),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSignature = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&serializedRootSignature,
		&errorBlob);
	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSignature->GetBufferPointer(),
		serializedRootSignature->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void ShapesApp::BuildByteCodeAndInputLayout()
{
	vsBytecode = CompileShader(L"Shaders\\Color.hlsl", nullptr, "VS", "vs_5_0");
	psBytecode = CompileShader(L"Shaders\\Color.hlsl", nullptr, "PS", "ps_5_0");
	shaders["shadowMapVS"] = CompileShader(L"Shaders\\ShadowMap.hlsl", nullptr, "VS", "vs_5_0");
	shaders["skyVS"] = CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_0");
	shaders["skyPS"] = CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_0");
	shaders["brdfLutVS"] = CompileShader(L"Shaders\\BrdfLut.hlsl", nullptr, "VS", "vs_5_0");
	shaders["brdfLutPS"] = CompileShader(L"Shaders\\BrdfLut.hlsl", nullptr, "PS", "ps_5_0");
	shaders["prefilterEnvVS"] = CompileShader(L"Shaders\\PrefilterEnvMap.hlsl", nullptr, "VS", "vs_5_0");
	shaders["prefilterEnvPS"] = CompileShader(L"Shaders\\PrefilterEnvMap.hlsl", nullptr, "PS", "ps_5_0");
	shaders["irradianceVS"] = CompileShader(L"Shaders\\IrradianceMap.hlsl", nullptr, "VS", "vs_5_0");
	shaders["irradiancePS"] = CompileShader(L"Shaders\\IrradianceMap.hlsl", nullptr, "PS", "ps_5_0");

	inputLayoutDesc1 =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	shaders["treeBillboardVS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "VS", "vs_5_0");
	shaders["treeBillboardGS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "GS", "gs_5_0");
	shaders["treeBillboardPS"] = CompileShader(L"Shaders\\TreeBillboard.hlsl", nullptr, "PS", "ps_5_0");

	treeBillboardInputLayoutDesc =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void ShapesApp::BuildShadowMapResources()
{
	mShadowMapSrvHeapIndex = mNextSrvHeapIndex++;

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowMapDsvHeap)));

	D3D12_RESOURCE_DESC shadowMapDesc = {};
	shadowMapDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	shadowMapDesc.Alignment = 0;
	shadowMapDesc.Width = ShadowMapSize;
	shadowMapDesc.Height = ShadowMapSize;
	shadowMapDesc.DepthOrArraySize = 1;
	shadowMapDesc.MipLevels = 1;
	shadowMapDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	shadowMapDesc.SampleDesc.Count = 1;
	shadowMapDesc.SampleDesc.Quality = 0;
	shadowMapDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	shadowMapDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&shadowMapDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&mShadowMap)));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateDepthStencilView(
		mShadowMap.Get(),
		&dsvDesc,
		mShadowMapDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShapesApp::BuildBrdfLutResources()
{
	mBrdfLutSrvHeapIndex = mNextSrvHeapIndex++;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mBrdfLutRtvHeap)));

	const DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_R16G16_FLOAT;
	CD3DX12_RESOURCE_DESC brdfLutDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		brdfLutFormat,
		BrdfLutSize,
		BrdfLutSize,
		1,
		1);
	brdfLutDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = brdfLutFormat;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&brdfLutDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&mBrdfLut)));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = brdfLutFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateRenderTargetView(
		mBrdfLut.Get(),
		&rtvDesc,
		mBrdfLutRtvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ShapesApp::BuildPrefilteredEnvironmentMapResources()
{
	mPrefilteredEnvMapSrvHeapIndex = mNextSrvHeapIndex++;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = PrefilteredEnvMapMipCount * 6;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mPrefilteredEnvMapRtvHeap)));

	const DXGI_FORMAT prefilteredEnvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	CD3DX12_RESOURCE_DESC prefilteredEnvDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		prefilteredEnvFormat,
		PrefilteredEnvMapSize,
		PrefilteredEnvMapSize,
		6,
		PrefilteredEnvMapMipCount);
	prefilteredEnvDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = prefilteredEnvFormat;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&prefilteredEnvDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&mPrefilteredEnvMap)));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = prefilteredEnvFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.PlaneSlice = 0;

	for (UINT mip = 0; mip < PrefilteredEnvMapMipCount; ++mip)
	{
		rtvDesc.Texture2DArray.MipSlice = mip;

		for (UINT face = 0; face < 6; ++face)
		{
			rtvDesc.Texture2DArray.FirstArraySlice = face;

			CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
				mPrefilteredEnvMapRtvHeap->GetCPUDescriptorHandleForHeapStart(),
				static_cast<INT>(mip * 6 + face),
				mRtvDescriptorSize);
			md3dDevice->CreateRenderTargetView(mPrefilteredEnvMap.Get(), &rtvDesc, handle);
		}
	}
}

void ShapesApp::BuildIrradianceMapResources()
{
	mIrradianceMapSrvHeapIndex = mNextSrvHeapIndex++;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 6;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mIrradianceMapRtvHeap)));

	const DXGI_FORMAT irradianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	CD3DX12_RESOURCE_DESC irradianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		irradianceFormat,
		IrradianceMapSize,
		IrradianceMapSize,
		6,
		1);
	irradianceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = irradianceFormat;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.0f;
	clearValue.Color[2] = 0.0f;
	clearValue.Color[3] = 0.0f;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&irradianceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&mIrradianceMap)));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = irradianceFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.MipSlice = 0;
	rtvDesc.Texture2DArray.ArraySize = 1;
	rtvDesc.Texture2DArray.PlaneSlice = 0;

	for (UINT face = 0; face < 6; ++face)
	{
		rtvDesc.Texture2DArray.FirstArraySlice = face;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mIrradianceMapRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			static_cast<INT>(face),
			mRtvDescriptorSize);
		md3dDevice->CreateRenderTargetView(mIrradianceMap.Get(), &rtvDesc, handle);
	}
}

void ShapesApp::BuildShaderResourceView()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = mNextSrvHeapIndex;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

	for (auto& [name, texture] : textures)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = texture->srvFormat == DXGI_FORMAT_UNKNOWN ? texture->resource->GetDesc().Format : texture->srvFormat;

		if (texture->isCubeMap)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = texture->resource->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
		}
		else
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = texture->resource->GetDesc().MipLevels;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			texture->srvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(texture->resource.Get(), &srvDesc, handle);
	}

	if (mShadowMap != nullptr && mShadowMapSrvHeapIndex != std::numeric_limits<UINT>::max())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
		shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		shadowSrvDesc.Texture2D.MostDetailedMip = 0;
		shadowSrvDesc.Texture2D.MipLevels = 1;
		shadowSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			mShadowMapSrvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(mShadowMap.Get(), &shadowSrvDesc, handle);
	}

	if (mBrdfLut != nullptr && mBrdfLutSrvHeapIndex != std::numeric_limits<UINT>::max())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC brdfLutSrvDesc = {};
		brdfLutSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		brdfLutSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		brdfLutSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		brdfLutSrvDesc.Texture2D.MostDetailedMip = 0;
		brdfLutSrvDesc.Texture2D.MipLevels = 1;
		brdfLutSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			mBrdfLutSrvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(mBrdfLut.Get(), &brdfLutSrvDesc, handle);
	}

	if (mPrefilteredEnvMap != nullptr && mPrefilteredEnvMapSrvHeapIndex != std::numeric_limits<UINT>::max())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC prefilteredEnvSrvDesc = {};
		prefilteredEnvSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		prefilteredEnvSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		prefilteredEnvSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		prefilteredEnvSrvDesc.TextureCube.MostDetailedMip = 0;
		prefilteredEnvSrvDesc.TextureCube.MipLevels = PrefilteredEnvMapMipCount;
		prefilteredEnvSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			mPrefilteredEnvMapSrvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(mPrefilteredEnvMap.Get(), &prefilteredEnvSrvDesc, handle);
	}

	if (mIrradianceMap != nullptr && mIrradianceMapSrvHeapIndex != std::numeric_limits<UINT>::max())
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC irradianceSrvDesc = {};
		irradianceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		irradianceSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		irradianceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		irradianceSrvDesc.TextureCube.MostDetailedMip = 0;
		irradianceSrvDesc.TextureCube.MipLevels = 1;
		irradianceSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
			mSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			mIrradianceMapSrvHeapIndex,
			mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(mIrradianceMap.Get(), &irradianceSrvDesc, handle);
	}
}

void ShapesApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputLayoutDesc1.data(), static_cast<UINT>(inputLayoutDesc1.size()) };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS = { reinterpret_cast<BYTE*>(vsBytecode->GetBufferPointer()), vsBytecode->GetBufferSize() };
	psoDesc.PS = { reinterpret_cast<BYTE*>(psBytecode->GetBufferPointer()), psBytecode->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowMapPsoDesc = psoDesc;
	shadowMapPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["shadowMapVS"]->GetBufferPointer()), shaders["shadowMapVS"]->GetBufferSize() };
	shadowMapPsoDesc.PS = { nullptr, 0 };
	shadowMapPsoDesc.RasterizerState.DepthBias = 100000;
	shadowMapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	shadowMapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	shadowMapPsoDesc.NumRenderTargets = 0;
	for (auto& rtvFormat : shadowMapPsoDesc.RTVFormats)
	{
		rtvFormat = DXGI_FORMAT_UNKNOWN;
	}
	shadowMapPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowMapPsoDesc, IID_PPV_ARGS(&mPSOs["shadowMap"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = psoDesc;
	skyPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["skyVS"]->GetBufferPointer()), shaders["skyVS"]->GetBufferSize() };
	skyPsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["skyPS"]->GetBufferPointer()), shaders["skyPS"]->GetBufferSize() };
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.DepthStencilState.DepthEnable = false;
	skyPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC brdfLutPsoDesc = psoDesc;
	brdfLutPsoDesc.InputLayout = { nullptr, 0 };
	brdfLutPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["brdfLutVS"]->GetBufferPointer()), shaders["brdfLutVS"]->GetBufferSize() };
	brdfLutPsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["brdfLutPS"]->GetBufferPointer()), shaders["brdfLutPS"]->GetBufferSize() };
	brdfLutPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	brdfLutPsoDesc.DepthStencilState.DepthEnable = false;
	brdfLutPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	brdfLutPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	brdfLutPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&brdfLutPsoDesc, IID_PPV_ARGS(&mPSOs["brdfLut"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC prefilterEnvPsoDesc = psoDesc;
	prefilterEnvPsoDesc.InputLayout = { nullptr, 0 };
	prefilterEnvPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["prefilterEnvVS"]->GetBufferPointer()), shaders["prefilterEnvVS"]->GetBufferSize() };
	prefilterEnvPsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["prefilterEnvPS"]->GetBufferPointer()), shaders["prefilterEnvPS"]->GetBufferSize() };
	prefilterEnvPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	prefilterEnvPsoDesc.DepthStencilState.DepthEnable = false;
	prefilterEnvPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	prefilterEnvPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	prefilterEnvPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&prefilterEnvPsoDesc, IID_PPV_ARGS(&mPSOs["prefilterEnv"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC irradiancePsoDesc = psoDesc;
	irradiancePsoDesc.InputLayout = { nullptr, 0 };
	irradiancePsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["irradianceVS"]->GetBufferPointer()), shaders["irradianceVS"]->GetBufferSize() };
	irradiancePsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["irradiancePS"]->GetBufferPointer()), shaders["irradiancePS"]->GetBufferSize() };
	irradiancePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	irradiancePsoDesc.DepthStencilState.DepthEnable = false;
	irradiancePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	irradiancePsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	irradiancePsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&irradiancePsoDesc, IID_PPV_ARGS(&mPSOs["irradiance"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeBillboardPsoDesc = psoDesc;
	treeBillboardPsoDesc.InputLayout = { treeBillboardInputLayoutDesc.data(), static_cast<UINT>(treeBillboardInputLayoutDesc.size()) };
	treeBillboardPsoDesc.VS = { reinterpret_cast<BYTE*>(shaders["treeBillboardVS"]->GetBufferPointer()), shaders["treeBillboardVS"]->GetBufferSize() };
	treeBillboardPsoDesc.GS = { reinterpret_cast<BYTE*>(shaders["treeBillboardGS"]->GetBufferPointer()), shaders["treeBillboardGS"]->GetBufferSize() };
	treeBillboardPsoDesc.PS = { reinterpret_cast<BYTE*>(shaders["treeBillboardPS"]->GetBufferPointer()), shaders["treeBillboardPS"]->GetBufferSize() };
	treeBillboardPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeBillboardPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeBillboardPsoDesc, IID_PPV_ARGS(&mPSOs["treeBillboard"])));
}

void ShapesApp::RenderBrdfLut()
{
	auto barrierToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		mBrdfLut.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRenderTarget);

	mCommandList->RSSetViewports(1, &mBrdfLutViewport);
	mCommandList->RSSetScissorRects(1, &mBrdfLutScissorRect);

	D3D12_CPU_DESCRIPTOR_HANDLE brdfLutRtvHandle = mBrdfLutRtvHeap->GetCPUDescriptorHandleForHeapStart();
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mCommandList->ClearRenderTargetView(brdfLutRtvHandle, clearColor, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &brdfLutRtvHandle, false, nullptr);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetPipelineState(mPSOs["brdfLut"].Get());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawInstanced(3, 1, 0, 0);

	auto barrierToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
		mBrdfLut.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &barrierToShaderResource);
}

void ShapesApp::RenderPrefilteredEnvironmentMap()
{
	auto barrierToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		mPrefilteredEnvMap.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRenderTarget);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetPipelineState(mPSOs["prefilterEnv"].Get());
	mCommandList->SetGraphicsRootDescriptorTable(7, [&]()
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		handle.Offset(textures[mEnvironmentTextureName]->srvHeapIndex, mCbvSrvUavDescriptorSize);
		return handle;
	}());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (UINT mip = 0; mip < PrefilteredEnvMapMipCount; ++mip)
	{
		const UINT mipSize = std::max<UINT>(1, PrefilteredEnvMapSize >> mip);
		D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(mipSize), static_cast<float>(mipSize), 0.0f, 1.0f };
		D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(mipSize), static_cast<LONG>(mipSize) };
		mCommandList->RSSetViewports(1, &viewport);
		mCommandList->RSSetScissorRects(1, &scissorRect);

		const float roughness = PrefilteredEnvMapMipCount > 1
			? static_cast<float>(mip) / static_cast<float>(PrefilteredEnvMapMipCount - 1)
			: 0.0f;

		for (UINT face = 0; face < 6; ++face)
		{
			const float prefilterConstants[2] = { roughness, static_cast<float>(face) };
			mCommandList->SetGraphicsRoot32BitConstants(12, _countof(prefilterConstants), prefilterConstants, 0);

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
				mPrefilteredEnvMapRtvHeap->GetCPUDescriptorHandleForHeapStart(),
				static_cast<INT>(mip * 6 + face),
				mRtvDescriptorSize);

			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
			mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
			mCommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
			mCommandList->DrawInstanced(3, 1, 0, 0);
		}
	}

	auto barrierToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
		mPrefilteredEnvMap.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &barrierToShaderResource);
}

void ShapesApp::RenderIrradianceMap()
{
	auto barrierToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		mIrradianceMap.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrierToRenderTarget);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetPipelineState(mPSOs["irradiance"].Get());
	mCommandList->SetGraphicsRootDescriptorTable(7, [&]()
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
		handle.Offset(textures[mEnvironmentTextureName]->srvHeapIndex, mCbvSrvUavDescriptorSize);
		return handle;
	}());
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(IrradianceMapSize), static_cast<float>(IrradianceMapSize), 0.0f, 1.0f };
	D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(IrradianceMapSize), static_cast<LONG>(IrradianceMapSize) };
	mCommandList->RSSetViewports(1, &viewport);
	mCommandList->RSSetScissorRects(1, &scissorRect);

	for (UINT face = 0; face < 6; ++face)
	{
		const float irradianceConstants[2] = { 0.0f, static_cast<float>(face) };
		mCommandList->SetGraphicsRoot32BitConstants(12, _countof(irradianceConstants), irradianceConstants, 0);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
			mIrradianceMapRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			static_cast<INT>(face),
			mRtvDescriptorSize);

		const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		mCommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
		mCommandList->DrawInstanced(3, 1, 0, 0);
	}

	auto barrierToShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
		mIrradianceMap.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCommandList->ResourceBarrier(1, &barrierToShaderResource);
}

void ShapesApp::BuildShapeGeometry()
{
	MeshData box = CreateBox(1.5f, 0.5f, 1.5f, 3);
	MeshData grid = CreateGrid(20.0f, 30.0f, 60, 40);
	MeshData sphere = CreateSphere(0.5f, 20, 20);
	MeshData cylinder = CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	const UINT boxVertexOffset = 0;
	const UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
	const UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	const UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());

	const UINT boxIndexOffset = 0;
	const UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	const UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	const UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());

	mDrawArgs["box"] = { static_cast<UINT>(box.Indices32.size()), boxVertexOffset, boxIndexOffset };
	mDrawArgs["grid"] = { static_cast<UINT>(grid.Indices32.size()), gridVertexOffset, gridIndexOffset };
	mDrawArgs["sphere"] = { static_cast<UINT>(sphere.Indices32.size()), sphereVertexOffset, sphereIndexOffset };
	mDrawArgs["cylinder"] = { static_cast<UINT>(cylinder.Indices32.size()), cylinderVertexOffset, cylinderIndexOffset };

	const size_t totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
	std::vector<Vertex1> vertices(totalVertexCount);
	size_t vertexCursor = 0;

	auto appendVertices = [&vertices, &vertexCursor](const MeshData& mesh)
	{
		for (const auto& srcVertex : mesh.Vertices)
		{
			Vertex1 dstVertex = {};
			dstVertex.Pos = srcVertex.Position;
			dstVertex.Normal = srcVertex.Normal;
			dstVertex.TangentU = XMFLOAT4(srcVertex.TangentU.x, srcVertex.TangentU.y, srcVertex.TangentU.z, 1.0f);
			dstVertex.TexC = srcVertex.TexC;
			vertices[vertexCursor++] = dstVertex;
		}
	};

	appendVertices(box);
	appendVertices(grid);
	appendVertices(sphere);
	appendVertices(cylinder);

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
	totalByteSize = vbByteSize;
	ibByteSize = ibByteSizeLocal;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mVertexBufferCPU));
	CopyMemory(mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &mIndexBufferCPU));
	CopyMemory(mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), mVertexBufferUploader);
	mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), mIndexBufferUploader);

	vbv.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	vbv.StrideInBytes = sizeof(Vertex1);
	vbv.SizeInBytes = vbByteSize;

	ibv.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	ibv.Format = DXGI_FORMAT_R16_UINT;
	ibv.SizeInBytes = ibByteSizeLocal;
}

void ShapesApp::BuildSkull()
{
	std::ifstream fin("Models/skull.txt");
	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found", 0, 0);
		return;
	}

	UINT vertexCount = 0;
	UINT triangleCount = 0;
	std::string ignore;

	fin >> ignore >> vertexCount;
	fin >> ignore >> triangleCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex1> vertices(vertexCount);
	for (UINT i = 0; i < vertexCount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
		vertices[i].TangentU = ComputeFallbackTangent(vertices[i].Normal);
		vertices[i].TexC = XMFLOAT2(0.0f, 0.0f);
	}

	fin >> ignore >> ignore >> ignore;
	std::vector<std::uint32_t> indices(triangleCount * 3);
	for (UINT i = 0; i < triangleCount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}
	fin.close();

	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "skullGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R32_UINT;
	geometry->mDrawArgs["skull"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["skullGeo"] = std::move(geometry);
}

void ShapesApp::BuildBoxGeometry()
{
	MeshData box = CreateBox(8.0f, 8.0f, 8.0f, 3);
	std::vector<Vertex1> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TangentU = XMFLOAT4(box.Vertices[i].TangentU.x, box.Vertices[i].TangentU.y, box.Vertices[i].TangentU.z, 1.0f);
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = box.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "boxGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["woodBox"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["boxGeo"] = std::move(geometry);
}

void ShapesApp::BuildGridGeomerty()
{
	MeshData grid = CreateGrid(8.0f, 8.0f, 8, 3);
	std::vector<Vertex1> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Normal = grid.Vertices[i].Normal;
		vertices[i].TangentU = XMFLOAT4(grid.Vertices[i].TangentU.x, grid.Vertices[i].TangentU.y, grid.Vertices[i].TangentU.z, 1.0f);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(indices.size() * sizeof(std::uint16_t));

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "gridGeo";
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["mirrorGrid"] = { static_cast<UINT>(indices.size()), 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["gridGeo"] = std::move(geometry);
}

void ShapesApp::BuildTreeBillboardGeometry()
{
	struct BillboardVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	const UINT treeCount = 16;
	std::array<BillboardVertex, treeCount> vertices = {};
	std::array<std::uint16_t, treeCount> indices = {};

	for (UINT i = 0; i < treeCount; ++i)
	{
		vertices[i].Pos = XMFLOAT3(MathHelper::RandF(-45.0f, 45.0f), 4.5f, MathHelper::RandF(-45.0f, 45.0f));
		vertices[i].Size = XMFLOAT2(10.0f, 10.0f);
		indices[i] = static_cast<std::uint16_t>(i);
	}

	const UINT vbByteSize = treeCount * sizeof(BillboardVertex);
	const UINT ibByteSizeLocal = treeCount * sizeof(std::uint16_t);

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = "treeBillboardGeo";
	geometry->mVertexByteStride = sizeof(BillboardVertex);
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;
	geometry->mIndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->mDrawArgs["treeBillboard"] = { treeCount, 0, 0 };

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, vertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, indices.data(), geometry->mIndexBufferUploader);

	geometries["treeBillboardGeo"] = std::move(geometry);
}

Texture* ShapesApp::GetTextureAsset(const std::string& textureName)
{
	auto it = textures.find(textureName);
	return it == textures.end() ? nullptr : it->second.get();
}

Texture* ShapesApp::LoadTextureAsset(const std::string& textureName, const std::wstring& filename, bool sRGB)
{
	if (auto* existing = GetTextureAsset(textureName))
	{
		return existing;
	}

	auto texture = std::make_unique<Texture>();
	texture->name = textureName;
	texture->filename = filename;
	texture->resource = CreateTextureFromFile(
		md3dDevice.Get(),
		mCommandList.Get(),
		filename,
		sRGB,
		texture->uploadHeap,
		texture->srvFormat,
		texture->isCubeMap);
	texture->srvHeapIndex = static_cast<int>(mNextSrvHeapIndex++);

	Texture* result = texture.get();
	textures[textureName] = std::move(texture);
	return result;
}

void ShapesApp::LoadTextures()
{
	LoadTextureAsset("woodTex", L"Textures/WoodCrate01.dds", true);
	LoadTextureAsset("white1x1Tex", L"Textures/white1x1.dds", false);
	LoadTextureAsset("defaultNormalTex", L"Textures/default_nmap.dds", false);
	LoadTextureAsset("bricksTex", L"Textures/bricks.dds", true);
	LoadTextureAsset("bricksNormalTex", L"Textures/bricks2_nmap.dds", false);
	LoadTextureAsset("tileTex", L"Textures/tile.dds", true);
	LoadTextureAsset("tileNormalTex", L"Textures/tile_nmap.dds", false);
	LoadTextureAsset("treeArray2Tex", L"Textures/treearray.dds", true);
	LoadTextureAsset("suburbanGardenHdrTex", L"D:/Computer Graphics/PathTracer/PathTracer-CPP/images/HDR/suburban_garden_2k.hdr", false);
	LoadTextureAsset("metal1BaseColorTex", L"D:/Computer Graphics/PathTracer/PathTracer-CPP/images/Metal1/Metal049A_2K-JPG_Color.jpg", true);
	LoadTextureAsset("metal1NormalTex", L"D:/Computer Graphics/PathTracer/PathTracer-CPP/images/Metal1/Metal049A_2K-JPG_NormalDX.jpg", false);
	LoadTextureAsset("metal1RoughnessTex", L"D:/Computer Graphics/PathTracer/PathTracer-CPP/images/Metal1/Metal049A_2K-JPG_Roughness.jpg", false);
	LoadTextureAsset("metal1MetallicTex", L"D:/Computer Graphics/PathTracer/PathTracer-CPP/images/Metal1/Metal049A_2K-JPG_Metalness.jpg", false);
}

void ShapesApp::BuildMaterial()
{
	auto addMaterial = [this](
		const std::string& materialName,
		const std::string& baseColorTexture,
		const std::string& normalTexture,
		XMFLOAT4 baseColorFactor,
		XMFLOAT3 dielectricF0,
		float roughnessFactor,
		float metallicFactor,
		float normalMapFlipY = 0.0f,
		float alphaCutoff = 0.1f,
		const std::string& roughnessTexture = "white1x1Tex",
		const std::string& metallicTexture = "white1x1Tex",
		float normalScale = 1.0f)
	{
		auto material = std::make_unique<Material>();
		material->name = materialName;
		material->matCBIndex = static_cast<int>(materials.size());
		material->baseColorSrvHeapIndex = textures[baseColorTexture]->srvHeapIndex;
		material->normalSrvHeapIndex = textures[normalTexture]->srvHeapIndex;
		material->roughnessSrvHeapIndex = textures[roughnessTexture]->srvHeapIndex;
		material->metallicSrvHeapIndex = textures[metallicTexture]->srvHeapIndex;
		material->alphaSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->baseColorFactor = baseColorFactor;
		material->fresnelR0 = dielectricF0;
		material->roughnessFactor = roughnessFactor;
		material->metallicFactor = metallicFactor;
		material->normalScale = normalScale;
		material->normalMapFlipY = normalMapFlipY;
		material->alphaCutoff = alphaCutoff;
		materials[materialName] = std::move(material);
	};

	addMaterial("sphere", "white1x1Tex", "defaultNormalTex", { 0.95f, 0.64f, 0.54f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.18f, 1.0f);
	addMaterial("cylinder", "bricksTex", "bricksNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.55f, 0.0f);
	addMaterial("grid", "tileTex", "tileNormalTex", { 0.9f, 0.9f, 0.9f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.75f, 0.0f);
	addMaterial("box", "woodTex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.65f, 0.0f);
	addMaterial("skull", "white1x1Tex", "defaultNormalTex", { 0.8f, 0.82f, 0.85f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.2f, 1.0f);
	addMaterial("wood", "woodTex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.7f, 0.0f);
	addMaterial("mirror", "white1x1Tex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 0.25f }, { 0.04f, 0.04f, 0.04f }, 0.05f, 1.0f);
	addMaterial("shadow", "white1x1Tex", "defaultNormalTex", { 0.0f, 0.0f, 0.0f, 0.25f }, { 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f);
	addMaterial("treeBillboard", "treeArray2Tex", "defaultNormalTex", { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.85f, 0.0f, 0.0f, 0.1f);
	addMaterial(
		"pbrMetalSphere",
		"metal1BaseColorTex",
		"metal1NormalTex",
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 0.04f, 0.04f, 0.04f },
		1.0f,
		1.0f,
		0.0f,
		0.1f,
		"metal1RoughnessTex",
		"metal1MetallicTex",
		1.0f);
	addMaterial("pbrFloor", "tileTex", "tileNormalTex", { 0.82f, 0.84f, 0.82f, 1.0f }, { 0.04f, 0.04f, 0.04f }, 0.95f, 0.0f);
}

bool ShapesApp::BuildObjModel(
	const std::wstring& objFilename,
	const std::string& geometryName,
	const std::string& materialPrefix)
{
	std::filesystem::path objPath(objFilename);
	if (!std::filesystem::exists(objPath))
	{
		return false;
	}

	objPath = std::filesystem::weakly_canonical(objPath);
	const std::filesystem::path objDirectory = objPath.parent_path();

	tinyobj::ObjReaderConfig readerConfig;
	readerConfig.mtl_search_path = objDirectory.string();
	readerConfig.triangulate = true;

	tinyobj::ObjReader reader;
	if (!reader.ParseFromFile(objPath.string(), readerConfig))
	{
		OutputDebugStringA((std::string("Failed to load OBJ: ") + reader.Error() + "\n").c_str());
		return false;
	}

	if (!reader.Warning().empty())
	{
		OutputDebugStringA((std::string("OBJ warning: ") + reader.Warning() + "\n").c_str());
	}

	const auto& attrib = reader.GetAttrib();
	const auto& shapes = reader.GetShapes();
	const auto& sourceMaterials = reader.GetMaterials();
	if (attrib.vertices.empty() || shapes.empty())
	{
		return false;
	}

	auto materialKeyForId = [&](int materialId)
	{
		if (materialId >= 0 && materialId < static_cast<int>(sourceMaterials.size()) && !sourceMaterials[materialId].name.empty())
		{
			return materialPrefix + "_" + SanitizeKey(sourceMaterials[materialId].name);
		}

		return materialPrefix + "_default";
	};

	auto loadMaterialTexture = [&](const std::string& textureName, const std::string& semantic, bool sRGB, int fallbackSrvIndex)
	{
		if (textureName.empty())
		{
			return fallbackSrvIndex;
		}

		const std::filesystem::path texturePath = ResolveObjTexturePath(objDirectory, textureName);
		if (!std::filesystem::exists(texturePath))
		{
			OutputDebugStringA((std::string("Missing OBJ texture: ") + texturePath.string() + "\n").c_str());
			return fallbackSrvIndex;
		}

		const std::string textureKey = "objtex_" + SanitizeKey(texturePath.lexically_normal().generic_string()) + "_" + semantic;
		try
		{
			return LoadTextureAsset(textureKey, texturePath.wstring(), sRGB)->srvHeapIndex;
		}
		catch (const std::exception& e)
		{
			OutputDebugStringA((std::string("Failed to load OBJ texture: ") + texturePath.string() + " - " + e.what() + "\n").c_str());
			return fallbackSrvIndex;
		}
	};

	auto ensureMaterial = [&](int materialId)
	{
		const std::string materialKey = materialKeyForId(materialId);
		if (materials.find(materialKey) != materials.end())
		{
			return materialKey;
		}

		auto material = std::make_unique<Material>();
		material->name = materialKey;
		material->matCBIndex = static_cast<int>(materials.size());
		material->baseColorSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->normalSrvHeapIndex = textures["defaultNormalTex"]->srvHeapIndex;
		material->roughnessSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->metallicSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->alphaSrvHeapIndex = textures["white1x1Tex"]->srvHeapIndex;
		material->baseColorFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		material->fresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
		material->roughnessFactor = 0.82f;
		material->metallicFactor = 0.0f;
		material->normalScale = 1.0f;
		material->normalMapFlipY = 0.0f;
		material->alphaCutoff = 0.1f;

		if (materialId >= 0 && materialId < static_cast<int>(sourceMaterials.size()))
		{
			const auto& src = sourceMaterials[materialId];
			const bool hasDiffuseColor = src.diffuse[0] > 0.0f || src.diffuse[1] > 0.0f || src.diffuse[2] > 0.0f;
			material->baseColorFactor = XMFLOAT4(
				hasDiffuseColor ? src.diffuse[0] : 1.0f,
				hasDiffuseColor ? src.diffuse[1] : 1.0f,
				hasDiffuseColor ? src.diffuse[2] : 1.0f,
				src.dissolve > 0.0f ? src.dissolve : 1.0f);

			if (src.roughness > 0.0f)
			{
				material->roughnessFactor = std::clamp(src.roughness, 0.05f, 1.0f);
			}
			else if (src.shininess > 0.0f)
			{
				material->roughnessFactor = std::clamp(std::sqrt(2.0f / (src.shininess + 2.0f)), 0.05f, 1.0f);
			}

			material->metallicFactor = src.metallic > 0.0f ? std::clamp(src.metallic, 0.0f, 1.0f) : 0.0f;
			material->baseColorSrvHeapIndex = loadMaterialTexture(src.diffuse_texname, "base", true, material->baseColorSrvHeapIndex);

			const std::string normalTextureName = !src.normal_texname.empty() ? src.normal_texname : src.bump_texname;
			if (!normalTextureName.empty())
			{
				material->normalSrvHeapIndex = loadMaterialTexture(normalTextureName, "normal", false, material->normalSrvHeapIndex);
				material->normalMapFlipY = ShouldFlipNormalY(normalTextureName) ? 1.0f : 0.0f;
				material->normalScale = 1.0f;
			}

			material->roughnessSrvHeapIndex = loadMaterialTexture(src.roughness_texname, "roughness", false, material->roughnessSrvHeapIndex);
			material->metallicSrvHeapIndex = loadMaterialTexture(src.metallic_texname, "metallic", false, material->metallicSrvHeapIndex);
			material->alphaSrvHeapIndex = loadMaterialTexture(src.alpha_texname, "alpha", false, material->alphaSrvHeapIndex);
			if (!src.alpha_texname.empty())
			{
				material->alphaCutoff = 0.5f;
			}
		}

		materials[materialKey] = std::move(material);
		return materialKey;
	};

	std::map<std::string, std::vector<Vertex1>> groupedVertices;
	std::map<std::string, std::vector<std::uint32_t>> groupedIndices;

	for (const auto& shape : shapes)
	{
		size_t indexOffset = 0;
		for (size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size(); ++faceIndex)
		{
			const size_t faceVertexCount = static_cast<size_t>(shape.mesh.num_face_vertices[faceIndex]);
			if (faceVertexCount != 3)
			{
				indexOffset += faceVertexCount;
				continue;
			}

			const int materialId = faceIndex < shape.mesh.material_ids.size() ? shape.mesh.material_ids[faceIndex] : -1;
			const std::string materialKey = ensureMaterial(materialId);

			XMFLOAT3 facePositions[3];
			XMFLOAT3 faceNormals[3];
			XMFLOAT2 faceTexcoords[3];
			bool hasNormals = true;
			bool validFace = true;

			for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
			{
				const tinyobj::index_t idx = shape.mesh.indices[indexOffset + vertexIndex];
				if (idx.vertex_index < 0)
				{
					validFace = false;
					break;
				}

				facePositions[vertexIndex] = XMFLOAT3(
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 0],
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 1],
					attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 2]);

				if (idx.normal_index >= 0)
				{
					faceNormals[vertexIndex] = XMFLOAT3(
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 0],
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 1],
						attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 2]);
				}
				else
				{
					hasNormals = false;
				}

				if (idx.texcoord_index >= 0)
				{
					faceTexcoords[vertexIndex] = XMFLOAT2(
						attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 0],
						1.0f - attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 1]);
				}
				else
				{
					faceTexcoords[vertexIndex] = XMFLOAT2(0.0f, 0.0f);
				}
			}

			if (!validFace)
			{
				indexOffset += faceVertexCount;
				continue;
			}

			if (!hasNormals)
			{
				const XMVECTOR p0 = XMLoadFloat3(&facePositions[0]);
				const XMVECTOR p1 = XMLoadFloat3(&facePositions[1]);
				const XMVECTOR p2 = XMLoadFloat3(&facePositions[2]);
				const XMVECTOR faceNormal = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
				for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
				{
					XMStoreFloat3(&faceNormals[vertexIndex], faceNormal);
				}
			}

			auto& vertices = groupedVertices[materialKey];
			auto& indices = groupedIndices[materialKey];
			const XMFLOAT4 faceTangent = ComputeTriangleTangent(
				facePositions[0],
				facePositions[1],
				facePositions[2],
				faceTexcoords[0],
				faceTexcoords[1],
				faceTexcoords[2],
				faceNormals[0]);

			for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
			{
				Vertex1 vertex = {};
				vertex.Pos = facePositions[vertexIndex];
				vertex.Normal = faceNormals[vertexIndex];
				vertex.TangentU = faceTangent;
				vertex.TexC = faceTexcoords[vertexIndex];
				vertices.push_back(vertex);
				indices.push_back(static_cast<std::uint32_t>(vertices.size() - 1));
			}

			indexOffset += faceVertexCount;
		}
	}

	if (groupedVertices.empty())
	{
		return false;
	}

	std::vector<Vertex1> mergedVertices;
	std::vector<std::uint32_t> mergedIndices;
	Bounds modelBounds;
	auto geometry = std::make_unique<MeshGeometry>();
	geometry->name = geometryName;
	geometry->mVertexByteStride = sizeof(Vertex1);
	geometry->mIndexFormat = DXGI_FORMAT_R32_UINT;

	for (const auto& [materialKey, vertices] : groupedVertices)
	{
		const auto& indices = groupedIndices[materialKey];
		SubmeshGeometry submesh = {};
		submesh.baseVertexLocation = static_cast<UINT>(mergedVertices.size());
		submesh.startIndexLocation = static_cast<UINT>(mergedIndices.size());
		submesh.indexCount = static_cast<UINT>(indices.size());

		for (const auto& vertex : vertices)
		{
			ExpandBounds(modelBounds, vertex.Pos);
		}

		mergedVertices.insert(mergedVertices.end(), vertices.begin(), vertices.end());
		for (std::uint32_t index : indices)
		{
			mergedIndices.push_back(index);
		}

		geometry->mDrawArgs[materialKey] = submesh;
		geometry->mSubmeshMaterials[materialKey] = materialKey;
		geometry->mSubmeshOrder.push_back(materialKey);
	}

	FinalizeBounds(modelBounds, mergedVertices);
	geometry->bounds = modelBounds;

	const UINT vbByteSize = static_cast<UINT>(mergedVertices.size() * sizeof(Vertex1));
	const UINT ibByteSizeLocal = static_cast<UINT>(mergedIndices.size() * sizeof(std::uint32_t));
	geometry->mVertexBufferByteSize = vbByteSize;
	geometry->mIndexBufferByteSize = ibByteSizeLocal;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geometry->mVertexBufferCPU));
	CopyMemory(geometry->mVertexBufferCPU->GetBufferPointer(), mergedVertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSizeLocal, &geometry->mIndexBufferCPU));
	CopyMemory(geometry->mIndexBufferCPU->GetBufferPointer(), mergedIndices.data(), ibByteSizeLocal);

	geometry->mVertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vbByteSize, mergedVertices.data(), geometry->mVertexBufferUploader);
	geometry->mIndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), ibByteSizeLocal, mergedIndices.data(), geometry->mIndexBufferUploader);

	geometries[geometryName] = std::move(geometry);
	if (std::find(mSceneModelGeometryNames.begin(), mSceneModelGeometryNames.end(), geometryName) == mSceneModelGeometryNames.end())
	{
		mSceneModelGeometryNames.push_back(geometryName);
	}

	return true;
}

void ShapesApp::BuildStaticSceneModels()
{
	mSceneModelGeometryNames.clear();

	const std::wstring sponzaCandidates[] =
	{
		L"D:/Computer Graphics/PathTracer/PathTracer-CPP/Model/sponza/sponza.obj",
		L"Models/Sponza/sponza.obj",
		L"Models/Sponza/Sponza.obj",
		L"Models/sponza/sponza.obj"
	};

	for (const auto& candidate : sponzaCandidates)
	{
		if (BuildObjModel(candidate, "sponzaGeo", "sponza"))
		{
			return;
		}
	}

	OutputDebugStringA("Sponza OBJ not found. Falling back to the procedural validation scene.\n");
}

void ShapesApp::BuildRenderItems()
{
	for (auto& layer : mRitemLayer)
	{
		layer.clear();
	}
	mAllItem.clear();
	mSceneObjects.clear();
	mSelectedSceneObjectIndex = -1;

	UINT nextObjCBIndex = 0;
	auto addSubmeshItem = [&](MeshGeometry* geo, const std::string& drawKey, const std::string& materialKey, CXMMATRIX worldMatrix) -> RenderItem*
	{
		const SubmeshGeometry* submesh = nullptr;
		if (geo != nullptr)
		{
			const auto drawIt = geo->mDrawArgs.find(drawKey);
			if (drawIt == geo->mDrawArgs.end())
			{
				return nullptr;
			}
			submesh = &drawIt->second;
		}
		else
		{
			const auto drawIt = mDrawArgs.find(drawKey);
			if (drawIt == mDrawArgs.end())
			{
				return nullptr;
			}
			submesh = &drawIt->second;
		}

		auto renderItem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&renderItem->world, worldMatrix);
		renderItem->localWorld = MathHelper::Identity4x4();
		renderItem->geo = geo;
		renderItem->mObjCBIndex = nextObjCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->indexCount = submesh->indexCount;
		renderItem->baseVertexLocation = submesh->baseVertexLocation;
		renderItem->startIndexLocation = submesh->startIndexLocation;
		auto materialIt = materials.find(materialKey);
		renderItem->mat = materialIt != materials.end() ? materialIt->second.get() : materials["pbrFloor"].get();
		renderItem->layer = RenderLayer::Opaque;
		RenderItem* result = renderItem.get();
		mAllItem.push_back(std::move(renderItem));
		return result;
	};

	auto addTestItem = [&](const std::string& drawKey, const std::string& materialKey, CXMMATRIX worldMatrix) -> RenderItem*
	{
		return addSubmeshItem(nullptr, drawKey, materialKey, worldMatrix);
	};

	auto addSkyItem = [&]()
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->world = MathHelper::Identity4x4();
		renderItem->mObjCBIndex = nextObjCBIndex++;
		renderItem->mPrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderItem->indexCount = mDrawArgs["sphere"].indexCount;
		renderItem->baseVertexLocation = mDrawArgs["sphere"].baseVertexLocation;
		renderItem->startIndexLocation = mDrawArgs["sphere"].startIndexLocation;
		renderItem->layer = RenderLayer::Sky;
		mAllItem.push_back(std::move(renderItem));
	};

	addSkyItem();

	Bounds importedBounds;
	bool addedImportedModel = false;
	for (const auto& geometryName : mSceneModelGeometryNames)
	{
		auto geometryIt = geometries.find(geometryName);
		if (geometryIt == geometries.end())
		{
			continue;
		}

		MeshGeometry* geometry = geometryIt->second.get();
		std::vector<RenderItem*> objectRenderItems;
		for (const auto& submeshKey : geometry->mSubmeshOrder)
		{
			const auto materialIt = geometry->mSubmeshMaterials.find(submeshKey);
			const std::string materialKey = materialIt != geometry->mSubmeshMaterials.end()
				? materialIt->second
				: "pbrFloor";
			if (RenderItem* renderItem = addSubmeshItem(geometry, submeshKey, materialKey, XMMatrixIdentity()))
			{
				objectRenderItems.push_back(renderItem);
				addedImportedModel = true;
			}
		}

		RegisterSceneObject(
			geometryName,
			objectRenderItems,
			XMFLOAT3(0.0f, 0.0f, 0.0f),
			XMFLOAT3(0.0f, 0.0f, 0.0f),
			XMFLOAT3(1.0f, 1.0f, 1.0f));

		if (geometry->bounds.valid)
		{
			ExpandBounds(importedBounds, geometry->bounds.min);
			ExpandBounds(importedBounds, geometry->bounds.max);
		}
	}

	if (addedImportedModel && importedBounds.valid)
	{
		const XMVECTOR minCorner = XMLoadFloat3(&importedBounds.min);
		const XMVECTOR maxCorner = XMLoadFloat3(&importedBounds.max);
		const float diagonal = XMVectorGetX(XMVector3Length(maxCorner - minCorner));
		mSceneBoundsRadius = std::max(1.0f, 0.5f * diagonal);

		if (std::find(mSceneModelGeometryNames.begin(), mSceneModelGeometryNames.end(), "sponzaGeo") != mSceneModelGeometryNames.end())
		{
			mSceneBoundsCenter = XMFLOAT3(80.0f, 320.0f, 80.0f);
			mTheta = 20.0f * XM_PI;
			mPhi = 5.0f * XM_PI;
			mRadius = 1440.0f;
		}
		else
		{
			mSceneBoundsCenter = XMFLOAT3(
				0.5f * (importedBounds.min.x + importedBounds.max.x),
				0.5f * (importedBounds.min.y + importedBounds.max.y),
				0.5f * (importedBounds.min.z + importedBounds.max.z));
			mRadius = std::max(5.0f, mSceneBoundsRadius * 1.55f);
		}
	}
	else
	{
		if (RenderItem* gridItem = addTestItem("grid", "pbrFloor", XMMatrixIdentity()))
		{
			RegisterSceneObject(
				"grid",
				{ gridItem },
				XMFLOAT3(0.0f, 0.0f, 0.0f),
				XMFLOAT3(0.0f, 0.0f, 0.0f),
				XMFLOAT3(1.0f, 1.0f, 1.0f));
		}
		if (RenderItem* sphereItem = addTestItem("sphere", "pbrMetalSphere", XMMatrixIdentity()))
		{
			RegisterSceneObject(
				"sphere",
				{ sphereItem },
				XMFLOAT3(0.0f, 1.0f, 0.0f),
				XMFLOAT3(0.0f, 0.0f, 0.0f),
				XMFLOAT3(2.0f, 2.0f, 2.0f));
		}

		mSceneBoundsCenter = XMFLOAT3(0.0f, 1.0f, 0.0f);
		mSceneBoundsRadius = 18.0f;
	}

	for (auto& renderItem : mAllItem)
	{
		mRitemLayer[(int)renderItem->layer].push_back(renderItem.get());
	}

	if (!mSceneObjects.empty())
	{
		mSelectedSceneObjectIndex = 0;
	}

	ResetCameraToSceneView();
	UpdateProjectionMatrix();
}

void ShapesApp::BuildFrameResource()
{
	mFrameResourcesArray.clear();
	for (UINT i = 0; i < mFrameResourcesCount; ++i)
	{
		mFrameResourcesArray.push_back(std::make_unique<FrameResources>(
			md3dDevice.Get(),
			static_cast<UINT>(mAllItem.size()),
			static_cast<UINT>(materials.size()),
			1));
	}

	mCurrentFrameResourcesIndex = 0;
	mCurrentFrameResources = mFrameResourcesArray.empty() ? nullptr : mFrameResourcesArray[0].get();
}

void ShapesApp::Update(GameTimer& gt)
{
	ProcessEditorCommands();

	mCurrentFrameResourcesIndex = (mCurrentFrameResourcesIndex + 1) % mFrameResourcesCount;
	mCurrentFrameResources = mFrameResourcesArray[mCurrentFrameResourcesIndex].get();

	if (mCurrentFrameResources->mFenceCPU != 0 && mFence->GetCompletedValue() < mCurrentFrameResources->mFenceCPU)
	{
		HANDLE eventHandle = CreateEvent(nullptr, false, false, L"FenceSetDone");
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFrameResources->mFenceCPU, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	OnKeyboardInput(gt);
	UpdateObjCBs();
	UpdateMatCBs();
	UpdatePassCBs(gt);
}

void ShapesApp::UpdateObjCBs()
{
	ObjectConstants objConstants = {};

	for (auto& renderItem : mAllItem)
	{
		if (renderItem->numFramesDirty <= 0)
		{
			continue;
		}

		const XMMATRIX world = XMLoadFloat4x4(&renderItem->world);
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

		XMVECTOR determinant = XMMatrixDeterminant(world);
		const XMMATRIX invWorld = XMMatrixInverse(&determinant, world);
		XMStoreFloat4x4(&objConstants.WorldInvTrans, XMMatrixTranspose(invWorld));

		mCurrentFrameResources->objCB->CopyData(renderItem->mObjCBIndex, objConstants);
		renderItem->numFramesDirty--;
	}
}

void ShapesApp::UpdatePassCBs(const GameTimer& gt)
{
	const XMVECTOR eyePos = XMLoadFloat3(&mCameraPosition);
	const XMVECTOR forward = GetCameraForwardVector();
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMMATRIX view = XMMatrixLookToLH(eyePos, forward, up);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX viewProj = view * proj;

	XMStoreFloat4x4(&mView, view);
	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat3(&passConstants.cameraPosW, eyePos);
	passConstants.totalTime = gt.TotalTime();
	passConstants.ambientLight = { 0.012f, 0.012f, 0.016f, 1.0f };
	passConstants.envMapMipCount = static_cast<float>(textures[mEnvironmentTextureName]->resource->GetDesc().MipLevels);
	passConstants.prefilteredEnvMapMipCount = static_cast<float>(PrefilteredEnvMapMipCount);
	passConstants.iblStrength = 0.6f;

	for (int i = 0; i < MAX_LIGHTS; ++i)
	{
		passConstants.lights[i] = Light{};
	}

	passConstants.lights[0].strength = { 2.8f, 2.7f, 2.55f };
	const XMVECTOR keyLightDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);
	XMStoreFloat3(&passConstants.lights[0].direction, keyLightDir);

	passConstants.lights[1].strength = { 0.55f, 0.52f, 0.48f };
	const XMVECTOR fillLightDir = XMVector3Normalize(XMVectorSet(0.35f, -0.75f, 0.45f, 0.0f));
	XMStoreFloat3(&passConstants.lights[1].direction, fillLightDir);

	UpdateShadowTransform();
	mCurrentFrameResources->passCB->CopyData(0, passConstants);
}

void ShapesApp::UpdateShadowTransform()
{
	const XMVECTOR sceneCenter = XMLoadFloat3(&mSceneBoundsCenter);
	const XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&passConstants.lights[0].direction));
	const XMVECTOR lightPos = sceneCenter - 2.0f * mSceneBoundsRadius * lightDir;
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, sceneCenter, up);

	XMFLOAT3 centerLightSpace;
	XMStoreFloat3(&centerLightSpace, XMVector3TransformCoord(sceneCenter, lightView));

	const float l = centerLightSpace.x - mSceneBoundsRadius;
	const float r = centerLightSpace.x + mSceneBoundsRadius;
	const float b = centerLightSpace.y - mSceneBoundsRadius;
	const float t = centerLightSpace.y + mSceneBoundsRadius;
	const float n = centerLightSpace.z - mSceneBoundsRadius;
	const float f = centerLightSpace.z + mSceneBoundsRadius;
	const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	const XMMATRIX textureTransform(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	const XMMATRIX lightViewProj = lightView * lightProj;
	const XMMATRIX shadowTransform = lightViewProj * textureTransform;
	XMStoreFloat4x4(&passConstants.LightViewProj, XMMatrixTranspose(lightViewProj));
	XMStoreFloat4x4(&passConstants.ShadowTransform, XMMatrixTranspose(shadowTransform));
}

void ShapesApp::UpdateMatCBs()
{
	for (auto& materialPair : materials)
	{
		Material* material = materialPair.second.get();
		if (material->numFramesDirty <= 0)
		{
			continue;
		}

		MatConstants matConstants = {};
		matConstants.baseColorFactor = material->baseColorFactor;
		matConstants.fresnelR0 = material->fresnelR0;
		matConstants.roughnessFactor = material->roughnessFactor;
		matConstants.metallicFactor = material->metallicFactor;
		matConstants.normalScale = material->normalScale;
		matConstants.normalMapFlipY = material->normalMapFlipY;
		matConstants.alphaCutoff = material->alphaCutoff;

		mCurrentFrameResources->matCB->CopyData(material->matCBIndex, matConstants);
		material->numFramesDirty--;
	}
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState(VK_LEFT) & 0x8000) sunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000) sunTheta += 1.0f * dt;
	if (GetAsyncKeyState(VK_UP) & 0x8000) sunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(VK_DOWN) & 0x8000) sunPhi += 1.0f * dt;
	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);
}

std::array<CD3DX12_STATIC_SAMPLER_DESC, 8> ShapesApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicMirror(
		6,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		7,
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp, anisotropicMirror, shadow };
}

D3D12_VERTEX_BUFFER_VIEW ShapesApp::GetVBV() const
{
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
	view.StrideInBytes = sizeof(Vertex1);
	view.SizeInBytes = totalByteSize;
	return view;
}

D3D12_INDEX_BUFFER_VIEW ShapesApp::GetIBV() const
{
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
	view.Format = DXGI_FORMAT_R16_UINT;
	view.SizeInBytes = ibByteSize;
	return view;
}
