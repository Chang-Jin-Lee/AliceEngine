cmake_minimum_required(VERSION 3.20)

project(AliceRenderer LANGUAGES CXX)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉터리 설정 (빌드 폴더 내부 bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 메인 실행 파일 타겟 정의
add_executable(AliceRenderer
    # 샌드박스(엔트리 포인트)
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp

    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.cpp

    # 코어(ECS 스타일)
    ${ALICE_SRC_DIR}/Core/World.cpp

    # 렌더링 계층
    ${ALICE_SRC_DIR}/Rendering/Camera.cpp
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.cpp
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.cpp
)

target_include_directories(AliceRenderer
    PRIVATE
        ${ALICE_SRC_DIR}
)

if(MSVC)
    # 경고 레벨 및 비교적 엄격한 컴파일 옵션
    target_compile_options(AliceRenderer PRIVATE /W4 /permissive-)
endif()

# Win32 하위 시스템으로 빌드 (콘솔 창 숨김)
set_target_properties(AliceRenderer PROPERTIES
    WIN32_EXECUTABLE YES
)

# DirectX 11 / DXGI 라이브러리 링크
target_link_libraries(AliceRenderer
    PRIVATE
        d3d11
        dxgi
        dxguid
        d3dcompiler
)


