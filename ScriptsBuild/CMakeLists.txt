cmake_minimum_required(VERSION 3.20)

# ============================================================================
# AliceRenderer - User Script 전용 CMake 프로젝트
# - Assets/Scripts/*.cpp 를 빌드해서 AliceScripts.dll 을 생성합니다.
# - 엔진(AliceEngine.lib)은 이미 상위 프로젝트에서 빌드되어 있다고 가정합니다.
# - Editor 의 "Reload Scripts" 버튼이 이 프로젝트를 cmake --build 로 호출합니다.
# ============================================================================
project(AliceUserScripts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# AliceRenderer 루트 경로 (ScriptsBuild 기준 상대 경로)
set(ALICE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# 유저 스크립트 소스 (에디터에서 생성되는 C++ 스크립트들)
file(GLOB_RECURSE USER_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${ALICE_ROOT}/Assets/Scripts/*.cpp"
)

if(NOT USER_SCRIPT_SOURCES)
    message(STATUS "AliceUserScripts: no user script sources found under Assets/Scripts")
endif()

# AliceScripts.dll (동적 스크립트 DLL)
add_library(AliceScripts SHARED
    ${USER_SCRIPT_SOURCES}
)

target_include_directories(AliceScripts
    PRIVATE
        "${ALICE_ROOT}/src"
        "${ALICE_ROOT}/ThirdParty/ImGui"
        "${ALICE_ROOT}/ThirdParty/ImGui/backends"
)

# vcpkg / 외부 라이브러리(DirectXTK, DirectXTex, Assimp) 설정
if(MSVC)
    set(VCPKG_ROOT_DEFAULT "D:/vcpkg")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DEFAULT "$ENV{VCPKG_ROOT}")
    endif()
    set(VCPKG_ROOT "${VCPKG_ROOT_DEFAULT}" CACHE PATH "Path to vcpkg root (ex: D:/vcpkg)")

    set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
    set(VCPKG_TRIPLET_DYNAMIC "x64-windows"          CACHE STRING "vcpkg dynamic triplet")

    target_include_directories(AliceScripts
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    target_link_directories(AliceScripts
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )
endif()

# 상위 빌드 디렉터리에서 이미 빌드된 AliceEngine.lib 에 링크합니다.
# - 기본 경로: <AliceRoot>/build/<Config>/AliceEngine.lib
target_link_libraries(AliceScripts
    PRIVATE
        "${ALICE_ROOT}/build/$<CONFIG>/AliceEngine.lib"
)

# AliceEngine(정적)에 포함된 코드가 사용하는 외부 라이브러리들을
# AliceScripts.dll 링크 단계에서도 반드시 같이 링크해야 합니다.
target_link_libraries(AliceScripts
    PRIVATE
        d3d11
        dxgi
        dxguid
        d3dcompiler
        directxtk
        directxtex
        $<$<CONFIG:Debug>:assimp-vc143-mtd>
        $<$<CONFIG:Release>:assimp-vc143-mt>
)

set_target_properties(AliceScripts PROPERTIES
    OUTPUT_NAME "AliceScripts"
)

# Visual Studio 디버그 세션에서 PDB 파일 잠금(LNK1201)을 피하기 위해
# ScriptsBuild 전용 DLL 에 대해서는 디버그 정보(.pdb)를 생성하지 않습니다.
if(MSVC)
    target_link_options(AliceScripts PRIVATE "/DEBUG:NONE")
endif()


