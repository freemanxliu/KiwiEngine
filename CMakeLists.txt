cmake_minimum_required(VERSION 3.20)
project(KiwiEngine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出二进制到项目根目录的 bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

# ---- ImGui ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# ---- 源文件 ----
set(SOURCES
    src/main.cpp
    src/Core/Window.cpp
    src/Core/Application.cpp
    src/Core/EngineConfig.cpp
    src/RHI/DX11Device.cpp
    src/RHI/DX12Device.cpp
    src/Scene/Mesh.cpp
    src/Scene/Scene.cpp
    src/Debug/RenderDocIntegration.cpp
)

set(HEADERS
    include/RHI/RHI.h
    include/RHI/RHITypes.h
    include/RHI/DX11/DX11Headers.h
    include/RHI/DX11/DX11Utils.h
    include/RHI/DX11/DX11Resources.h
    include/RHI/DX11/DX11Device.h
    include/RHI/DX12/DX12Headers.h
    include/RHI/DX12/DX12Device.h
    include/Core/Window.h
    include/Core/Application.h
    include/Core/EngineConfig.h
    include/Math/Math.h
    include/Scene/Mesh.h
    include/Scene/Shaders.h
    include/Scene/ShaderLibrary.h
    include/Scene/SceneObject.h
    include/Scene/Scene.h
    include/Debug/RenderDocIntegration.h
)

# ---- 可执行文件 ----
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

# ---- Include 路径 ----
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/renderdoc
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ---- 链接 Windows 系统库 ----
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
    d3d12
    d3dcompiler
    dxgi
    dxguid
    dwmapi
)

# ---- 定义 ----
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG;KIWI_DEBUG>
    $<$<CONFIG:Release>:NDEBUG;KIWI_RELEASE>
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# ---- 编译选项 ----
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /WX-
        /permissive-
        $<$<CONFIG:Debug>:/Zi /Od>
        $<$<CONFIG:Release>:/O2 /Ob2>
    )
endif()

# ---- Copy Shaders/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Shaders
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/Shaders
    COMMENT "Copying Shaders/ to output directory"
)

# ---- Copy Config/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Config
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/Config
    COMMENT "Copying Config/ to output directory"
)
