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
add_library(AliceEngine STATIC
    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.cpp

    # 코어(ECS 스타일)
    ${ALICE_SRC_DIR}/Core/World.cpp
    ${ALICE_SRC_DIR}/Core/StringUtils.h
    ${ALICE_SRC_DIR}/Core/ResourceManager.cpp
    ${ALICE_SRC_DIR}/Core/Scene.cpp
    ${ALICE_SRC_DIR}/Core/TimeSystem.cpp
    ${ALICE_SRC_DIR}/Core/InputSystem.cpp

    # 렌더링 계층
    ${ALICE_SRC_DIR}/Rendering/Camera.cpp
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.cpp
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.cpp
)

# ===== 콘텐츠/게임 실행 파일 (Sandbox + Scenes) =====
add_executable(AliceRenderer
    # 샌드박스(엔트리 포인트)
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp

    # 샘플 씬 (콘텐츠 계층)
    ${ALICE_SRC_DIR}/Scenes/SampleScene.cpp
)

target_include_directories(AliceEngine
    PRIVATE
        ${ALICE_SRC_DIR}
)

target_include_directories(AliceRenderer
    PRIVATE
        ${ALICE_SRC_DIR}
)

if(MSVC)
    # 경고 레벨 및 비교적 엄격한 컴파일 옵션
    target_compile_options(AliceEngine   PRIVATE /W4 /permissive-)
    target_compile_options(AliceRenderer PRIVATE /W4 /permissive-)

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

    # vcpkg lib 경로 추가 (Debug/Release 를 분리해서 Props 와 동일하게 설정)
    target_link_directories(AliceRenderer
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

# DirectX 11 / DXGI / DirectXTK / DirectXTex / ImGui 링크 (vcpkg)
target_link_libraries(AliceRenderer
    PRIVATE
        AliceEngine
        d3d11
        dxgi
        dxguid
        d3dcompiler
        directxtk
        directxtex
        $<$<CONFIG:Debug>:imguid>
        $<$<NOT:$<CONFIG:Debug>>:imgui>
)


