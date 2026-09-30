cmake_minimum_required(VERSION 3.21)
project(DX12Renderer LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This DirectX 12 / Win32 application requires Windows with MSVC.")
endif()

add_executable(DX12Renderer WIN32
    DX12/D3DApp.cpp DX12/DDSTextureLoader.cpp DX12/DX12.cpp
    DX12/DXMath/DXMath.cpp DX12/DXMath/MathHelper.cpp
    DX12/FrameResources.cpp DX12/ProceduralGeometry.cpp DX12/GameTimer.cpp
    DX12/ShapesApp.cpp DX12/ToolFunc.cpp DX12/WindowsProject1.rc
    DX12/third_party/imgui/imgui.cpp DX12/third_party/imgui/imgui_draw.cpp
    DX12/third_party/imgui/imgui_tables.cpp DX12/third_party/imgui/imgui_widgets.cpp
    DX12/third_party/imgui/backends/imgui_impl_dx12.cpp
    DX12/third_party/imgui/backends/imgui_impl_win32.cpp
    DX12/third_party/ImGuizmo/ImGuizmo.cpp
)
target_compile_features(DX12Renderer PRIVATE cxx_std_23)
target_compile_definitions(DX12Renderer PRIVATE UNICODE _UNICODE IMGUI_DEFINE_MATH_OPERATORS)
# The legacy Win32/Timer sources and headers contain CP936 comments.
target_compile_options(DX12Renderer PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/source-charset:.936>" "$<$<COMPILE_LANGUAGE:CXX>:/W3>")
target_include_directories(DX12Renderer PRIVATE
    DX12 DX12/DXMath DX12/third_party/imgui
    DX12/third_party/imgui/backends DX12/third_party/ImGuizmo
)
target_link_libraries(DX12Renderer PRIVATE d3d12 dxgi dxguid d3dcompiler user32 gdi32 comdlg32 ole32 windowscodecs)
set_property(TARGET DX12Renderer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/DX12")
foreach(directory Shaders Textures Models)
    add_custom_command(TARGET DX12Renderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/DX12/${directory}" "$<TARGET_FILE_DIR:DX12Renderer>/${directory}"
    )
endforeach()
