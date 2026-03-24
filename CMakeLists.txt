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
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# ---- 源文件 ----
set(SOURCES
    src/main.cpp
    src/Core/Window.cpp
    src/Core/Application.cpp
    src/RHI/DX11Device.cpp
    src/Scene/Mesh.cpp
    src/Scene/Scene.cpp
)

set(HEADERS
    include/RHI/RHI.h
    include/RHI/RHITypes.h
    include/RHI/DX11/DX11Headers.h
    include/RHI/DX11/DX11Utils.h
    include/RHI/DX11/DX11Resources.h
    include/RHI/DX11/DX11Device.h
    include/Core/Window.h
    include/Core/Application.h
    include/Math/Math.h
    include/Scene/Mesh.h
    include/Scene/Shaders.h
    include/Scene/SceneObject.h
    include/Scene/Scene.h
)

# ---- 可执行文件 ----
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

# ---- Include 路径 ----
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ---- 链接 Windows 系统库 ----
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d11
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
