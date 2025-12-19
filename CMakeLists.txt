cmake_minimum_required(VERSION 3.20)

# 프로젝트 이름은 전체 솔루션 이름
project(AliceRenderer LANGUAGES CXX)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉터리 설정 (빌드 폴더 내부 bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ==========================================
# [설정] RTTR 라이브러리 (Engine에 통합)
# ==========================================
# Boost/Doxygen 의존성을 제거하고 가볍게 빌드하기 위한 옵션들
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# 정적 라이브러리(.lib)로 빌드하여 DLL 복사 과정 생략
set(BUILD_STATIC ON CACHE BOOL "" FORCE)

# 런타임 라이브러리 설정 (vcpkg 및 엔진과 맞추기 위해 Static Runtime 끔 -> MD/MDd 사용)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)

# RTTR 서브모듈 추가 (경로: ThirdParty/rttr)
add_subdirectory(ThirdParty/rttr)

# ==========================================
# [Target] Engine (기존 AliceEngine)
# ==========================================
set(ENGINE_SOURCES
    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.cpp
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.cpp
    ${ALICE_SRC_DIR}/Editor/EditorCore.cpp

    # 코어(ECS 스타일)
    ${ALICE_SRC_DIR}/Core/World.cpp
    ${ALICE_SRC_DIR}/Core/Script.cpp
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.cpp
    ${ALICE_SRC_DIR}/Core/Prefab.cpp
    ${ALICE_SRC_DIR}/Core/Material.cpp
    ${ALICE_SRC_DIR}/Core/SceneFile.cpp
    ${ALICE_SRC_DIR}/Core/ResourceManager.cpp
    ${ALICE_SRC_DIR}/Core/Scene.cpp
    ${ALICE_SRC_DIR}/Core/TimeSystem.cpp
    ${ALICE_SRC_DIR}/Core/InputSystem.cpp
    ${ALICE_SRC_DIR}/Core/Logger.cpp
    ${ALICE_SRC_DIR}/Core/Singleton.cpp
    ${ALICE_SRC_DIR}/Core/Helper.cpp
    ${ALICE_SRC_DIR}/Core/Vertex.cpp

    # FBX/3D 모델 로더
    ${ALICE_SRC_DIR}/3Dmodel/FbxMaterial.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxModel.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxSkeleton.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxGeometry.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxAnimation.cpp

    # 렌더링 계층
    ${ALICE_SRC_DIR}/Rendering/Camera.cpp
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.cpp
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.h
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.cpp
)

set(ENGINE_HEADERS
    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.h
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.h
    ${ALICE_SRC_DIR}/Editor/EditorCore.h

    # 코어
    ${ALICE_SRC_DIR}/Core/World.h
    ${ALICE_SRC_DIR}/Core/Delegate.h
    ${ALICE_SRC_DIR}/Core/Script.h
    ${ALICE_SRC_DIR}/Core/ScriptAPI.h
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.h
    ${ALICE_SRC_DIR}/Core/Prefab.h
    ${ALICE_SRC_DIR}/Core/Material.h
    ${ALICE_SRC_DIR}/Core/Logger.h
    ${ALICE_SRC_DIR}/Core/SceneFile.h
    ${ALICE_SRC_DIR}/Core/Component.h
    ${ALICE_SRC_DIR}/Core/Entity.h
    ${ALICE_SRC_DIR}/Core/System.h
    ${ALICE_SRC_DIR}/Core/StringUtils.h
    ${ALICE_SRC_DIR}/Core/ResourceManager.h
    ${ALICE_SRC_DIR}/Core/Scene.h
    ${ALICE_SRC_DIR}/Core/TimeSystem.h
    ${ALICE_SRC_DIR}/Core/InputSystem.h
    ${ALICE_SRC_DIR}/Core/ImGuiEx.h
    ${ALICE_SRC_DIR}/Core/Input.h
    ${ALICE_SRC_DIR}/Core/Singleton.h
    ${ALICE_SRC_DIR}/Core/Helper.h
    ${ALICE_SRC_DIR}/Core/Vertex.h

    # FBX/3D 모델 로더 헤더
    ${ALICE_SRC_DIR}/3Dmodel/FbxMaterial.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxModel.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxSkeleton.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxTypes.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxGeometry.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxAnimation.h

    # 렌더링 계층
    ${ALICE_SRC_DIR}/Rendering/Camera.h
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.h
    ${ALICE_SRC_DIR}/Rendering/IRenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/ID3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.h
)

# [변경] AliceEngine -> Engine
add_library(Engine STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

# [설정] Engine에 RTTR 연결 (PUBLIC: 엔진을 쓰는 프로젝트도 RTTR 사용 가능)
target_link_libraries(Engine PUBLIC RTTR::Core_Lib)

source_group(TREE ${ALICE_SRC_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# ==========================================
# [Target] Launch (기존 AliceGame, 실행 파일)
# ==========================================
# 공통 게임 코드
set(APP_COMMON_SOURCES
    ${ALICE_SRC_DIR}/Scenes/SampleScene.cpp
    ${ALICE_SRC_DIR}/Game/FbxImporter.cpp
    ${ALICE_SRC_DIR}/Game/FbxAsset.cpp
)

set(APP_HEADERS
    ${ALICE_SRC_DIR}/Scenes/SampleScene.h
    ${ALICE_SRC_DIR}/Game/FbxImporter.h
    ${ALICE_SRC_DIR}/Game/FbxAsset.h
    ${ALICE_SRC_DIR}/Game/SkinnedMeshSystem.h
    ${ALICE_SRC_DIR}/Game/SkinnedAnimationSystem.h
)

# 엔트리 포인트 (기존 AliceGame이 사용하던 Main.cpp 사용)
set(LAUNCH_MAIN
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp
)

# [변경] AliceGame -> Launch
add_executable(Launch
    ${LAUNCH_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${LAUNCH_MAIN} ${APP_COMMON_SOURCES} ${APP_HEADERS})

# ==========================================
# [ThirdParty] ImGui
# ==========================================
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ImGui")

# Engine이 ImGui 헤더를 찾을 수 있도록 설정
target_include_directories(Engine
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# ImGui 정적 라이브러리
add_library(imgui STATIC
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.h
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.h
)

target_include_directories(imgui
    PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# Launch가 ImGui 헤더를 찾을 수 있도록 설정
target_include_directories(Launch
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# ==========================================
# [설정] 컴파일 옵션 및 vcpkg
# ==========================================
if(MSVC)
    # 경고 레벨
    target_compile_options(Engine      PRIVATE /W4 /permissive-)
    target_compile_options(Launch PRIVATE /W4 /permissive-)

    # VCPKG 설정
    set(VCPKG_ROOT_DEFAULT "D:/vcpkg")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DEFAULT "$ENV{VCPKG_ROOT}")
    endif()
    set(VCPKG_ROOT "${VCPKG_ROOT_DEFAULT}" CACHE PATH "Path to vcpkg root")

    set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
    set(VCPKG_TRIPLET_DYNAMIC "x64-windows" CACHE STRING "vcpkg dynamic triplet")

    # Include 경로 추가
    target_include_directories(Engine
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    target_include_directories(Launch
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # Lib 경로 추가
    target_link_directories(Launch
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )
endif()

# Win32 하위 시스템 (콘솔 숨김)
set_target_properties(Launch PROPERTIES
    WIN32_EXECUTABLE YES
)

# ==========================================
# [Link] 라이브러리 연결
# ==========================================
target_link_libraries(Launch
    PRIVATE
        Engine          # [변경] AliceEngine 대신 Engine 링크
        imgui
        d3d11
        dxgi
        dxguid
        d3dcompiler
        directxtk
        directxtex
        $<$<CONFIG:Debug>:assimp-vc143-mtd>
        $<$<CONFIG:Release>:assimp-vc143-mt>
)

# ==========================================
# [Post-Build] DLL 복사
# ==========================================
set(THIRDPARTY_3DMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/3DModel")

# [변경] Launch 타겟에만 복사 수행
foreach(target Launch)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/zlib1.dll"       "$<TARGET_FILE_DIR:${target}>/zlib1.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/zlibd1.dll"      "$<TARGET_FILE_DIR:${target}>/zlibd1.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/quazip.dll"      "$<TARGET_FILE_DIR:${target}>/quazip.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/pugixml.dll"     "$<TARGET_FILE_DIR:${target}>/pugixml.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/poly2tri.dll"    "$<TARGET_FILE_DIR:${target}>/poly2tri.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/kubazip.dll"     "$<TARGET_FILE_DIR:${target}>/kubazip.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/minizip.dll"     "$<TARGET_FILE_DIR:${target}>/minizip.dll"

        # vcpkg Assimp DLL 복사
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin/assimp-vc143-mtd.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mtd.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin/assimp-vc143-mt.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mt.dll"
    )
endforeach()