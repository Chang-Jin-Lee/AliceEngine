cmake_minimum_required(VERSION 3.20)

# 프로젝트 이름은 전체 솔루션 이름
project(AliceRenderer LANGUAGES CXX)


# /utf-8 옵션 추가 경고 안뜨게 하기 위함
if(MSVC)
    add_compile_options(/utf-8)
endif()

# =========================================================================
# [설정] VCPKG 경로 (맨 위에서 관리)
# 1. "AUTO" : 환경변수 -> D드라이브 -> C드라이브 순서로 자동 탐색
# 2. 직접 지정 : "E:/MyLibs/vcpkg" 처럼 경로 입력
# =========================================================================
set(ALICE_VCPKG_PATH_OPTION "AUTO")

# --- VCPKG 경로 자동 감지 로직 ---
if(ALICE_VCPKG_PATH_OPTION STREQUAL "AUTO")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DETECTED $ENV{VCPKG_ROOT})
        message(STATUS "[Alice] VCPKG: 환경변수에서 감지됨 -> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "D:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "D:/vcpkg")
        message(STATUS "[Alice] VCPKG: D드라이브에서 감지됨 -> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "C:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "C:/vcpkg")
        message(STATUS "[Alice] VCPKG: C드라이브에서 감지됨 -> ${VCPKG_ROOT_DETECTED}")
    else()
        message(FATAL_ERROR "[Alice] vcpkg를 찾을 수 없습니다! 배치 파일을 먼저 실행하거나 경로를 지정해주세요.")
    endif()
else()
    set(VCPKG_ROOT_DETECTED ${ALICE_VCPKG_PATH_OPTION})
    message(STATUS "[Alice] VCPKG: 사용자 지정 경로 사용 -> ${VCPKG_ROOT_DETECTED}")
endif()

# 변수는 여기서 확정됨 (캐시에 저장하여 GUI에서도 보이게 함)
set(VCPKG_ROOT "${VCPKG_ROOT_DETECTED}" CACHE PATH "Path to vcpkg root" FORCE)
set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
set(VCPKG_TRIPLET_DYNAMIC "x64-windows" CACHE STRING "vcpkg dynamic triplet")


# ==========================================
# [설정] 기본 컴파일 옵션
# ==========================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 디렉터리 설정 (빌드 폴더 내부 bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ImGui")
set(IMGUIZMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ImGuizmo")
set(THIRDPARTY_3DMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/3DModel")

# ==========================================
# [설정] RTTR 라이브러리 (Engine에 통합)
# ==========================================
# Boost/Doxygen 의존성을 제거하고 가볍게 빌드하기 위한 옵션들
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# RTTR은 shared로 빌드해야 Launch.exe, AliceScripts.dll 사이에서
# 등록된 프로퍼티가 같은 registry로 합쳐집니다.
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)

# 런타임 라이브러리 설정 (vcpkg 및 엔진과 맞추기 위해 Static Runtime 끔 -> MD/MDd 사용)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)

# RTTR 서브모듈 추가 (경로: ThirdParty/rttr)
add_subdirectory(ThirdParty/rttr)

# window, std 둘의 min, max 충돌삭제
add_compile_definitions(NOMINMAX)

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
    ${ALICE_SRC_DIR}/Core/ThreadSafety.cpp
    ${ALICE_SRC_DIR}/Core/IScript.cpp
    ${ALICE_SRC_DIR}/Core/ScriptFactory.cpp
    ${ALICE_SRC_DIR}/Core/ScriptSystem.cpp
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.cpp
    ${ALICE_SRC_DIR}/Core/Prefab.cpp
    ${ALICE_SRC_DIR}/Core/Material.cpp
    ${ALICE_SRC_DIR}/Core/SceneFile.cpp
    ${ALICE_SRC_DIR}/Core/ComponentRegistry.cpp
    ${ALICE_SRC_DIR}/Core/CameraSystem.cpp
    ${ALICE_SRC_DIR}/Core/ResourceManager.cpp
    ${ALICE_SRC_DIR}/Core/Scene.cpp
    ${ALICE_SRC_DIR}/Core/TimeSystem.cpp
    ${ALICE_SRC_DIR}/Core/InputSystem.cpp
    ${ALICE_SRC_DIR}/Core/Logger.cpp
    ${ALICE_SRC_DIR}/Core/Singleton.cpp
    ${ALICE_SRC_DIR}/Core/Helper.cpp
    ${ALICE_SRC_DIR}/Core/Vertex.cpp
    ${ALICE_SRC_DIR}/Core/Input.cpp

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
    ${ALICE_SRC_DIR}/Rendering/DeferredRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.cpp
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.h
    
    #PhysX
    ${ALICE_SRC_DIR}/PhysX/PhysXContext.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Actors.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Core.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Joints.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Queries.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysicsSystem.cpp
    #Physx Module
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsDebug.cpp
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsModule.cpp

    #UI
     ${ALICE_SRC_DIR}/UI/UIWorldManager.cpp
     ${ALICE_SRC_DIR}/UI/UISceneManager.cpp

     #UI Component
    ${ALICE_SRC_DIR}/UI/IUIComponent.cpp
	${ALICE_SRC_DIR}/UI/UITransform.cpp
	${ALICE_SRC_DIR}/UI/UI_ImageComponent.cpp
	
	${ALICE_SRC_DIR}/UI/UI_ScriptComponent.cpp
	${ALICE_SRC_DIR}/UI/IUIScript.cpp
	${ALICE_SRC_DIR}/UI/UI_InputComponent.cpp
	
     #UI Object
     ${ALICE_SRC_DIR}/UI/UIBase.cpp
	 ${ALICE_SRC_DIR}/UI/UIImage.cpp
	 ${ALICE_SRC_DIR}/UI/UIButton.cpp
	
	#UI IUIScripts
	${ALICE_SRC_DIR}/UI/UIScriptSystem.cpp
	${ALICE_SRC_DIR}/UI/UIScriptFactory.cpp

    # 쉐이더 코드 묶음
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/CommonShaderCode.cpp
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/DeferredShader.cpp
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/ForwardShader.cpp
	${ALICE_SRC_DIR}/Rendering/EffectSystem.cpp
	${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.cpp

)

set(ENGINE_HEADERS
    # 엔진 계층
    ${ALICE_SRC_DIR}/Engine/Engine.h
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.h
    ${ALICE_SRC_DIR}/Editor/EditorCore.h

    # 코어
    ${ALICE_SRC_DIR}/Core/World.h
    ${ALICE_SRC_DIR}/Core/ThreadSafety.h
    ${ALICE_SRC_DIR}/Core/GameObject.h
    ${ALICE_SRC_DIR}/Core/Delegate.h
    ${ALICE_SRC_DIR}/Core/IScript.h
    ${ALICE_SRC_DIR}/Core/ScriptFactory.h
    ${ALICE_SRC_DIR}/Core/ScriptSystem.h
    ${ALICE_SRC_DIR}/Core/ScriptAPI.h
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.h
    ${ALICE_SRC_DIR}/Core/Prefab.h
    ${ALICE_SRC_DIR}/Core/Material.h
    ${ALICE_SRC_DIR}/Core/Logger.h
    ${ALICE_SRC_DIR}/Core/CameraSystem.h
    ${ALICE_SRC_DIR}/Core/SceneFile.h
    ${ALICE_SRC_DIR}/Core/SceneFileHelper.h
    ${ALICE_SRC_DIR}/Core/ReflectionSerializer.h
    ${ALICE_SRC_DIR}/Core/ReflectionUI.h
    ${ALICE_SRC_DIR}/Core/ComponentRegistry.h
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
    ${ALICE_SRC_DIR}/Core/JsonRttr.h
    ${ALICE_SRC_DIR}/Core/InputTypes.h

    
    #컴포넌트들
    ${ALICE_SRC_DIR}/Components/ComponentStorage.h
    ${ALICE_SRC_DIR}/Components/CameraBlendComponent.h
    ${ALICE_SRC_DIR}/Components/CameraComponent.h
    ${ALICE_SRC_DIR}/Components/CameraFollowComponent.h
    ${ALICE_SRC_DIR}/Components/CameraInputComponent.h
    ${ALICE_SRC_DIR}/Components/CameraLookAtComponent.h
    ${ALICE_SRC_DIR}/Components/CameraShakeComponent.h
    ${ALICE_SRC_DIR}/Components/CameraSpringArmComponent.h
    ${ALICE_SRC_DIR}/Components/MaterialComponent.h
    ${ALICE_SRC_DIR}/Components/PointLightComponent.h
    ${ALICE_SRC_DIR}/Components/RectLightComponent.h
    ${ALICE_SRC_DIR}/Components/ScriptComponent.h
    ${ALICE_SRC_DIR}/Components/SkinnedAnimationComponent.h
    ${ALICE_SRC_DIR}/Components/SkinnedMeshComponent.h
    ${ALICE_SRC_DIR}/Components/SpotLightComponent.h
    ${ALICE_SRC_DIR}/Components/TransformComponent.h
    ${ALICE_SRC_DIR}/Components/TrailEffectComponent.h
    ${ALICE_SRC_DIR}/Components/EffectComponent.h

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
    ${ALICE_SRC_DIR}/Rendering/DeferredRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.h
    ${ALICE_SRC_DIR}/Rendering/IRenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/ID3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.h
    ${ALICE_SRC_DIR}/Rendering/RenderTypes.h
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.h
    
    # 쉐이더 코드 묶음
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/CommonShaderCode.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/DeferredShader.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/ForwardShader.h
    ${ALICE_SRC_DIR}/Rendering/EffectSystem.h
	  ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    
    #PhysX 물리
    ${ALICE_SRC_DIR}/PhysX/IPhysicsWorld.h
    ${ALICE_SRC_DIR}/PhysX/PhysicsMath.h
    ${ALICE_SRC_DIR}/PhysX/PhysXContext.h
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld.h
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Internal.h
	${ALICE_SRC_DIR}/PhysX/PhysicsSystem.h

    #Module
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsDebug.h
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsEcsBridge.h
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsModule.h
    #Components
	${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_SettingsComponent.h
	${ALICE_SRC_DIR}/PhysX/Components/Phy_ColliderComponent.h
	${ALICE_SRC_DIR}/PhysX/Components/Phy_RigidBodyComponent.h	
	${ALICE_SRC_DIR}/PhysX/Components/Phy_TerrainHeightFieldComponent.h
	${ALICE_SRC_DIR}/PhysX/Components/Phy_MeshColliderComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_JointComponent.h


     #UI
    ${ALICE_SRC_DIR}/UI/UIWorldManager.h
    ${ALICE_SRC_DIR}/UI/UISceneManager.h
    ${ALICE_SRC_DIR}/UI/UIRenderStruct.h
	${ALICE_SRC_DIR}/UI/UITransform.h
	${ALICE_SRC_DIR}/UI/UICompDelegate.h
	${ALICE_SRC_DIR}/UI/IUIScript.h
	${ALICE_SRC_DIR}/UI/UIScriptSystem.h

     #UI Component
    ${ALICE_SRC_DIR}/UI/IUIComponent.h

     #UI Object
    ${ALICE_SRC_DIR}/UI/UIBase.h

    # json
    ${ALICE_SRC_DIR}/json/json.hpp
    ${ALICE_SRC_DIR}/json/json_fwd.hpp

	${ALICE_SRC_DIR}/Rendering/EffectSystem.h
	${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
	${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
	${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
	${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
	${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
	${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h


	${ALICE_SRC_DIR}/UI/UI_ImageComponent.h
	${ALICE_SRC_DIR}/UI/UIImage.h
	${ALICE_SRC_DIR}/UI/UI_ScriptComponent.h
	${ALICE_SRC_DIR}/UI/UI_InputComponent.h
	${ALICE_SRC_DIR}/UI/UIButton.h

)

# [변경] AliceEngine -> Engine
add_library(Engine STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

# [설정] Engine에 RTTR 연결 (PUBLIC: 엔진을 쓰는 프로젝트도 RTTR 사용 가능)
target_link_libraries(Engine PUBLIC RTTR::Core)

source_group(TREE ${ALICE_SRC_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# ==========================================
# [Target] Launch & AlicePlayer
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

# 1. Launch (에디터/툴 모드) 엔트리 포인트
set(LAUNCH_MAIN
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp
)

# 2. AlicePlayer (순수 게임 모드) 엔트리 포인트
set(PLAYER_MAIN
    ${ALICE_SRC_DIR}/Sandbox/GameMain.cpp
)

# [Target 1] Launch (기존 AliceGame/AliceRenderer)
add_executable(Launch
    ${LAUNCH_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

# [Target 2] AlicePlayer (최종 빌드용 플레이어)
add_executable(AlicePlayer
    ${PLAYER_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${LAUNCH_MAIN} ${PLAYER_MAIN} ${APP_COMMON_SOURCES} ${APP_HEADERS})

# ==========================================
# [ThirdParty] ImGui
# ==========================================

# Engine이 ImGui 헤더를 찾을 수 있도록 설정
target_include_directories(Engine
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rttr/src  # RTTR 헤더 경로
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

# Launch와 AlicePlayer가 ImGui 헤더를 찾을 수 있도록 설정
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${ALICE_SRC_DIR}
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
    )
endforeach()

# ==========================================
# [ThirdParty] ImGuizmo
# ==========================================

# Engine이 ImGuizmo 헤더를 찾을 수 있도록 설정
target_include_directories(Engine
    PRIVATE
        ${IMGUIZMO_DIR}
)

# ImGuizmo 정적 라이브러리
add_library(imguizmo STATIC
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.h
)

target_include_directories(imguizmo
    PUBLIC
        ${IMGUIZMO_DIR}
)

# ImGuizmo를 DirectX 모드로 컴파일 (LH 좌표계, depth 0~1)
target_compile_definitions(imguizmo
    PUBLIC
        IMGUIZMO_USE_DIRECTX
)

# Engine도 동일한 정의 사용
target_compile_definitions(Engine
    PRIVATE
        IMGUIZMO_USE_DIRECTX
)

# ImGuizmo는 ImGui에 의존
target_link_libraries(imguizmo
    PUBLIC
        imgui
)

# 실행 파일들이 ImGuizmo 헤더를 찾을 수 있도록 설정
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${IMGUIZMO_DIR}
    )
endforeach()

# ==========================================
# [설정] 컴파일 옵션 및 vcpkg 적용
# ==========================================
if(MSVC)
    # 경고 레벨
    target_compile_options(Engine      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(Launch      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(AlicePlayer PRIVATE /W4 /permissive- /bigobj)

    # Include 경로 추가 (상단에서 설정한 VCPKG_ROOT 사용)
    target_include_directories(Engine
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # Launch와 AlicePlayer에 VCPKG Include/Lib 경로 설정
    foreach(target Launch AlicePlayer)
        target_include_directories(${target}
            PRIVATE
                "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
                "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
        )

        target_link_directories(${target}
            PRIVATE
                "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
                "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
                "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
                "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
        )
    endforeach()
endif()

# Win32 하위 시스템 (콘솔 숨김)
set_target_properties(Launch PROPERTIES WIN32_EXECUTABLE YES)
set_target_properties(AlicePlayer PROPERTIES WIN32_EXECUTABLE YES)

# ==========================================
# [Link] 라이브러리 연결
# ==========================================
# 두 타겟 모두 동일한 라이브러리 의존성을 가짐
foreach(target Launch AlicePlayer)
    target_link_libraries(${target}
        PRIVATE
            Engine          # 핵심 엔진
            imgui
            imguizmo
            d3d11
            dxgi
            dxguid
            d3dcompiler
            directxtk
            directxtex
            $<$<CONFIG:Debug>:assimp-vc143-mtd>
            $<$<CONFIG:Release>:assimp-vc143-mt>
    )
endforeach()

# ==========================================
# [Post-Build] DLL 복사
# ==========================================

# [변경] Launch와 AlicePlayer 타겟 모두에 DLL 복사 수행
foreach(target Launch AlicePlayer)
    add_custom_command(TARGET ${target} POST_BUILD
        # RTTR shared 스크립트 DLL과 registry 공유
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rttr_core>" "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:rttr_core>"

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
            "${THIRDPARTY_3DMODEL_DIR}/kubazip.dll"      "$<TARGET_FILE_DIR:${target}>/kubazip.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/minizip.dll"      "$<TARGET_FILE_DIR:${target}>/minizip.dll"

        # vcpkg Assimp DLL 복사
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin/assimp-vc143-mtd.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mtd.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin/assimp-vc143-mt.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mt.dll"
    )
endforeach()

# ==========================================
# PhysX
# ==========================================
if(MSVC)

  foreach(t Launch AlicePlayer)
    target_link_options(${t} PRIVATE
      "$<$<CONFIG:Debug>:/NODEFAULTLIB:PhysXExtensions_static_64.lib>"
    )
  endforeach()
  
    # vcpkg toolchain을 안 쓰는 구성이라면, find_package가 찾을 수 있게 prefix만 추가
    list(PREPEND CMAKE_PREFIX_PATH
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}"
    )

    # vcpkg physx 포트가 내보내는 패키지/타겟
    find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

    # 엔진에 PhysX 연결 (Debug/Release 올바른 lib 조합은 타겟이 알아서 처리)
    target_link_libraries(Engine PUBLIC unofficial::omniverse-physx-sdk::sdk)

    # include 보강
    target_include_directories(Engine PRIVATE
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include/physx"
    )

    # PhysX 런타임 DLL 복사 (필요한 것만 최소로)
    set(PHYSX_BINDIR_DEBUG "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin")
    set(PHYSX_BINDIR_REL   "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin")

    foreach(target Launch AlicePlayer)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXFoundation_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXFoundation_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXFoundation_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXCommon_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXCommon_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXCommon_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysX_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysX_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysX_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXCooking_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXCooking_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXCooking_64.dll"
        )
    endforeach()
endif()