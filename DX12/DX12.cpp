#include <Windows.h>
#include "ShapesApp.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
    void FindRuntimeDirectory()
    {
        wchar_t executable[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        const std::filesystem::path starts[] = {
            length > 0 && length < MAX_PATH ? std::filesystem::path(executable).parent_path() : std::filesystem::current_path(),
            std::filesystem::current_path()
        };
        for (auto directory : starts)
        {
            while (!directory.empty())
            {
                for (const auto& candidate : { directory, directory / "DX12" })
                {
                    if (std::filesystem::is_regular_file(candidate / "Shaders/Color.hlsl") &&
                        std::filesystem::is_regular_file(candidate / "Textures/white1x1.dds"))
                    {
                        std::filesystem::current_path(candidate);
                        return;
                    }
                }
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
        }
        throw std::runtime_error("Required Shaders/ and Textures/ not found beside the executable or in its repository ancestors.");
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR cmdLine, int nShowCmd)
{
    const bool smoke = std::string(cmdLine) == "--smoke";
    try
    {
        if (*cmdLine && !smoke) throw std::runtime_error("Only --smoke is supported. Optional resources use DX12_ASSET_ROOT.");
        FindRuntimeDirectory();
        ShapesApp shapesApp(hInstance);
#if defined(DEBUG) || defined(_DEBUG)
        _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
        if (!shapesApp.InitRnederItems(hInstance, smoke ? SW_HIDE : nShowCmd, L"")) return 1;
        return shapesApp.Run(smoke ? 10 : 0);
    }
    catch (DxException& error)
    {
        OutputDebugStringW(error.ToString().c_str());
        if (!smoke) MessageBoxW(nullptr, error.ToString().c_str(), L"DX12 Renderer", MB_OK | MB_ICONERROR);
    }
    catch (const std::exception& error)
    {
        OutputDebugStringA(error.what());
        if (!smoke) MessageBoxA(nullptr, error.what(), "DX12 Renderer", MB_OK | MB_ICONERROR);
    }
    return 1;
}
