cmake_minimum_required(VERSION 3.20)

project(AliceRenderer LANGUAGES CXX)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉터리 설정 (빌드 폴더 내부 bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ===== 엔진 라이브러리 (Engine/Core/Rendering) =====
set(ALICE_ENGINE_SOURCES
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

    # FBX/3D 모델 로더 (D3D11-AliceTutorial Common/Mesh 기반)
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

set(ALICE_ENGINE_HEADERS
    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.h
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.h
    ${ALICE_SRC_DIR}/Editor/EditorCore.h

    # 코어(ECS 스타일)
    ${ALICE_SRC_DIR}/Core/World.h
    ${ALICE_SRC_DIR}/Core/Script.h
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

add_library(AliceEngine STATIC
    ${ALICE_ENGINE_SOURCES}
    ${ALICE_ENGINE_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${ALICE_ENGINE_SOURCES} ${ALICE_ENGINE_HEADERS})

# ===== 콘텐츠/게임 실행 파일 (Sandbox + Scenes) =====
# 공통 게임 코드(엔트리 포인트 제외)
set(ALICE_APP_COMMON_SOURCES
    # 샘플 씬 (콘텐츠 계층)
    ${ALICE_SRC_DIR}/Scenes/SampleScene.cpp

    # 게임 레벨 유틸리티 (FBX 임포터, 스키닝 시스템 등)
    ${ALICE_SRC_DIR}/Game/FbxImporter.cpp
    ${ALICE_SRC_DIR}/Game/FbxAsset.cpp
)

set(ALICE_APP_HEADERS
    ${ALICE_SRC_DIR}/Scenes/SampleScene.h

    ${ALICE_SRC_DIR}/Game/FbxImporter.h
    ${ALICE_SRC_DIR}/Game/FbxAsset.h
    ${ALICE_SRC_DIR}/Game/SkinnedMeshSystem.h
)

set(ALICE_EDITOR_MAIN
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp
)

set(ALICE_PLAYER_MAIN
    ${ALICE_SRC_DIR}/Sandbox/GameMain.cpp
)

# === 엔진 데모/기본 실행 파일 (엔진 자체 테스트용, 에디터 모드) ===
add_executable(AliceRenderer
    ${ALICE_EDITOR_MAIN}
    ${ALICE_APP_COMMON_SOURCES}
    ${ALICE_APP_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${ALICE_EDITOR_MAIN} ${ALICE_PLAYER_MAIN} ${ALICE_APP_COMMON_SOURCES} ${ALICE_APP_HEADERS})

# === 콘텐츠 개발용 실행 파일 (Assets 폴더의 스크립트 포함) ===
# - Unity 의 게임 프로젝트에 해당하는 대상
file(GLOB_RECURSE ALICE_ASSET_SCRIPTS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.cpp"
)

# 동적 스크립트 DLL (라이브 코딩용)
file(GLOB_RECURSE ALICE_SCRIPT_DLL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*.cpp"
    "${ALICE_SRC_DIR}/Core/Script.cpp"
)

add_library(AliceScripts SHARED
    ${ALICE_SCRIPT_DLL_SOURCES}
)

target_include_directories(AliceScripts
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(AliceScripts
    PRIVATE
        AliceEngine
)

add_executable(AliceGame
    ${ALICE_EDITOR_MAIN}
    ${ALICE_APP_COMMON_SOURCES}
    ${ALICE_APP_HEADERS}
)

add_executable(AlicePlayer
    ${ALICE_PLAYER_MAIN}
    ${ALICE_APP_COMMON_SOURCES}
    ${ALICE_APP_HEADERS}
)

target_include_directories(AlicePlayer
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
)

target_link_directories(AlicePlayer
    PRIVATE
        "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
        "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
        "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
)

source_group(TREE ${ALICE_SRC_DIR}   FILES ${ALICE_EDITOR_MAIN} ${ALICE_PLAYER_MAIN} ${ALICE_APP_COMMON_SOURCES} ${ALICE_APP_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ALICE_ASSET_SCRIPTS})

# ThirdParty ImGui (docking 브랜치) 경로
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ImGui")

target_include_directories(AliceEngine
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_include_directories(AliceRenderer
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# ImGui 정적 라이브러리 (Win32 + DX11 백엔드 포함)
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

target_include_directories(AliceGame
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

if(MSVC)
    # 경고 레벨 및 비교적 엄격한 컴파일 옵션
    target_compile_options(AliceEngine   PRIVATE /W4 /permissive-)
    target_compile_options(AliceRenderer PRIVATE /W4 /permissive-)
    target_compile_options(AliceGame     PRIVATE /W4 /permissive-)

    # ==== vcpkg + DirectXTK/DirectXTex 연동 ====
    # D3D11-AliceTutorial 의 Directory.Build.Props 와 동일한 개념을 CMake로 옮긴 것.
    #
    # VCPKG_ROOT 기본값: 환경변수 VCPKG_ROOT가 있으면 그 값을, 아니면 D:/vcpkg 사용.
    set(VCPKG_ROOT_DEFAULT "D:/vcpkg")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DEFAULT "$ENV{VCPKG_ROOT}")
    endif()
    set(VCPKG_ROOT "${VCPKG_ROOT_DEFAULT}" CACHE PATH "Path to vcpkg root (ex: D:/vcpkg)")

    # 정적/동적 트리플릿 (Props 의 TripletStatic / TripletDynamic 과 대응)
    set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
    set(VCPKG_TRIPLET_DYNAMIC "x64-windows" CACHE STRING "vcpkg dynamic triplet")

    # vcpkg include 경로 추가 (DirectXTK / DirectXTex / ImGui / Assimp 등)
    target_include_directories(AliceEngine
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )
    target_include_directories(AliceRenderer
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    target_include_directories(AliceGame
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    target_include_directories(AlicePlayer
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # vcpkg lib 경로 추가 (Debug/Release 를 분리해서 Props 와 동일하게 설정)
    target_link_directories(AliceRenderer
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )

    target_link_directories(AliceGame
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )
    target_link_directories(AlicePlayer
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )
endif()

# Win32 하위 시스템으로 빌드 (콘솔 창 숨김)
set_target_properties(AliceRenderer PROPERTIES
    WIN32_EXECUTABLE YES
)
set_target_properties(AliceGame PROPERTIES
    WIN32_EXECUTABLE YES
)
set_target_properties(AlicePlayer PROPERTIES
    WIN32_EXECUTABLE YES
)

# DirectX 11 / DXGI / DirectXTK / DirectXTex / ImGui / Assimp 링크
target_link_libraries(AliceRenderer
    PRIVATE
        AliceEngine
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

target_link_libraries(AliceGame
    PRIVATE
        AliceEngine
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

target_link_libraries(AlicePlayer
    PRIVATE
        AliceEngine
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

# === 3D Model 관련 DLL 들을 실행 파일 출력 폴더로 복사 ===
set(THIRDPARTY_3DMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/3DModel")

foreach(target AliceRenderer AliceGame AlicePlayer)
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

        # vcpkg 에서 설치된 Assimp DLL 도 함께 복사
        # (Debug/Release 둘 다 존재한다고 가정하고 단순히 복사만 수행)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin/assimp-vc143-mtd.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mtd.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin/assimp-vc143-mt.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mt.dll"
    )
endforeach()


