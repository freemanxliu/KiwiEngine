cmake_minimum_required(VERSION 3.20)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
    # Keep SDKROOT as the generic "macosx" name. An absolute MacOSX14.2.sdk path
    # makes Xcode fail Info.plist generation with "SDK lookup failed: macosx14.2".
    set(CMAKE_OSX_SYSROOT "macosx" CACHE PATH "macOS SDK" FORCE)
endif()
project(KiwiEngine VERSION 0.2.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

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
)
if(WIN32)
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    )
elseif(APPLE)
    list(APPEND IMGUI_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
    )
endif()

# ---- glad (OpenGL loader) ----
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/third_party/glad)
set(GLAD_SOURCES ${GLAD_DIR}/src/gl.c)

# ---- 源文件 ----
set(SOURCES
    src/main.cpp
    src/Renderer.cpp
    src/Core/Application.cpp
    src/Core/EngineConfig.cpp
    src/Core/Platform.cpp
    src/RHI/RHIFactory.cpp
    src/Scene/Mesh.cpp
    src/Scene/Scene.cpp
    src/Scene/GPUScene.cpp
    src/Scene/TextureManager.cpp
    src/Scene/Material.cpp
    src/Scene/stb_image_impl.cpp
    src/Debug/RenderDocIntegration.cpp
)
if(WIN32)
    list(APPEND SOURCES
        src/Core/Window.cpp
        src/RHI/DX11Device.cpp
        src/RHI/DX12Device.cpp
        src/RHI/DXCCompiler.cpp
        src/RHI/GLDevice.cpp
        src/RHI/VulkanDevice.cpp
    )
elseif(APPLE)
    list(APPEND SOURCES
        src/Core/Window.mm
        src/RHI/MetalDevice.mm
    )
endif()

set(HEADERS
    include/RHI/RHI.h
    include/RHI/RHITypes.h
    include/RHI/DXCCompiler.h
    include/RHI/DX11/DX11Headers.h
    include/RHI/DX11/DX11Utils.h
    include/RHI/DX11/DX11Resources.h
    include/RHI/DX11/DX11Device.h
    include/RHI/DX12/DX12Headers.h
    include/RHI/DX12/DX12Device.h
    include/RHI/DX12/DX12Resources.h
    include/RHI/GL/GLHeaders.h
    include/RHI/GL/GLResources.h
    include/RHI/GL/GLDevice.h
    include/RHI/Vulkan/VulkanHeaders.h
    include/RHI/Vulkan/VulkanDevice.h
    include/Core/Window.h
    include/Core/Application.h
    include/Core/EngineConfig.h
    include/Math/Math.h
    include/Scene/Mesh.h
    include/Scene/Shaders.h
    include/Scene/ShaderLibrary.h
    include/Scene/Component.h
    include/Scene/MeshComponent.h
    include/Scene/CameraComponent.h
    include/Scene/LightComponent.h
    include/Scene/SceneObject.h
    include/Scene/Scene.h
    include/Scene/PostProcessComponent.h
    include/Scene/PostProcessShaders.h
    include/Scene/PostProcessShaderLibrary.h
    include/Scene/TextureManager.h
    include/Scene/Material.h
    include/Debug/RenderDocIntegration.h
)

# ---- 可执行文件 ----
if(WIN32)
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES} ${GLAD_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})
endif()
if(APPLE)
    set_source_files_properties(src/Core/Window.mm src/RHI/MetalDevice.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    # A command-line tool on the macOS 14 SDK cannot be signed with an empty identity.
    # Build a normal app bundle and ad-hoc sign it, so Xcode does not require a certificate.
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "KiwiEngine"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.kiwi.engine"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.kiwi.engine"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        "XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY[sdk=macosx*]" "-"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/apple/KiwiEngine.entitlements"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "NO"
    )
endif()

# ---- Include 路径 ----
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/renderdoc
    ${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/include
    ${GLAD_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        d3d12
        d3dcompiler
        dxgi
        dxguid
        dwmapi
        opengl32
        ${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/lib/vulkan-1.lib
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
        "-framework AppKit"
        "-framework GameController"
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG;KIWI_DEBUG>
    $<$<CONFIG:Release>:NDEBUG;KIWI_RELEASE>
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

# App bundles must keep loose assets in Contents/Resources. Files placed next to
# the executable are treated as nested code and fail the code signature.
if(APPLE)
    set(KIWI_ASSET_DIR "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources")
else()
    set(KIWI_ASSET_DIR "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
endif()

# ---- Copy Shaders/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Shaders
        ${KIWI_ASSET_DIR}/Shaders
    COMMENT "Copying Shaders/ to output directory"
)

# ---- Copy GLShaders/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/GLShaders
        ${KIWI_ASSET_DIR}/GLShaders
    COMMENT "Copying GLShaders/ to output directory"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/MetalShaders
        ${KIWI_ASSET_DIR}/MetalShaders
    COMMENT "Copying MetalShaders/ to output directory"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/MetalPostProcess
        ${KIWI_ASSET_DIR}/MetalPostProcess
    COMMENT "Copying MetalPostProcess/ to output directory"
)

# ---- Copy PostProcessShaders/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/PostProcessShaders
        ${KIWI_ASSET_DIR}/PostProcessShaders
    COMMENT "Copying PostProcessShaders/ to output directory"
)

# ---- Copy Config/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Config
        ${KIWI_ASSET_DIR}/Config
    COMMENT "Copying Config/ to output directory"
)

# ---- Copy Textures/ folder to output directory ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Textures
        ${KIWI_ASSET_DIR}/Textures
    COMMENT "Copying Textures/ to output directory"
)

if(EXISTS ${CMAKE_SOURCE_DIR}/Materials)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/Materials
            ${KIWI_ASSET_DIR}/Materials
        COMMENT "Copying Materials/ to output directory"
    )
endif()

# ---- Copy DXC runtime DLLs to output directory (for runtime shader compilation) ----
# Find Windows SDK bin directory for x64
set(WIN_SDK_BIN "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
if(EXISTS "${WIN_SDK_BIN}/dxcompiler.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WIN_SDK_BIN}/dxcompiler.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/dxcompiler.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WIN_SDK_BIN}/dxil.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/dxil.dll
        COMMENT "Copying DXC runtime (dxcompiler.dll + dxil.dll)"
    )
endif()
