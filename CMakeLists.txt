cmake_minimum_required(VERSION 3.20)

# ?로?트 ?름? ?체 ?루???름
project(AliceRenderer LANGUAGES CXX)


# /utf-8 ?션 추? 경고 ?뜨??기 ?함
if(MSVC)
    add_compile_options(/utf-8)
endif()

# =========================================================================
# [?정] VCPKG 경로 (??에??관?
# 1. "AUTO" : ?경변??-> D?라?브 -> C?라?브 ?서??동 ?색
# 2. 직접 지??: "E:/MyLibs/vcpkg" 처럼 경로 ?력
# =========================================================================
set(ALICE_VCPKG_PATH_OPTION "AUTO")

# --- VCPKG 경로 ?동 감? 로직 ---
if(ALICE_VCPKG_PATH_OPTION STREQUAL "AUTO")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DETECTED $ENV{VCPKG_ROOT})
        message(STATUS "[Alice] VCPKG: ?경변?에??감???-> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "D:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "D:/vcpkg")
        message(STATUS "[Alice] VCPKG: D?라?브?서 감???-> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "C:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "C:/vcpkg")
        message(STATUS "[Alice] VCPKG: C?라?브?서 감???-> ${VCPKG_ROOT_DETECTED}")
    else()
        message(FATAL_ERROR "[Alice] vcpkg?찾을 ???습?다! 배치 ?일??먼? ?행?거??경로?지?해주세??")
    endif()
else()
    set(VCPKG_ROOT_DETECTED ${ALICE_VCPKG_PATH_OPTION})
    message(STATUS "[Alice] VCPKG: ?용??지??경로 ?용 -> ${VCPKG_ROOT_DETECTED}")
endif()

# 변?는 ?기???정??(캐시????하??GUI?서??보이???
set(VCPKG_ROOT "${VCPKG_ROOT_DETECTED}" CACHE PATH "Path to vcpkg root" FORCE)
set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
set(VCPKG_TRIPLET_DYNAMIC "x64-windows" CACHE STRING "vcpkg dynamic triplet")


# ==========================================
# [?정] 기본 컴파???션
# ==========================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 출력 ?렉?리 ?정 (빌드 ?더 ?? bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/src")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/ImGui")
set(IMGUIZMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/ImGuizmo")
#set(IMGUI_NODE_EDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/imgui-node-editor")
set(THIRDPARTY_3DMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/3DModel")
set(FMOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/FMOD")

# ==========================================
# [?정] RTTR ?이브러?(Engine???합)
# ==========================================
# Boost/Doxygen ?존?을 ?거?고 가볍게 빌드?기 ?한 ?션??
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# RTTR? shared?빌드?야 Launch.exe, AliceScripts.dll ?이?서
# ?록???로?티가 같? registry??쳐집니??
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)

# ?????이브러??정 (vcpkg ??진?맞추??해 Static Runtime ??-> MD/MDd ?용)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)

# RTTR  ߰ (: ThirdParty/rttr)
add_subdirectory(Engine/ThirdParty/rttr)

# window, std ?의 min, max 충돌??
add_compile_definitions(NOMINMAX)

# ==========================================
# [Target] Engine (기존 AliceEngine)
# ==========================================
set(ENGINE_SOURCES
    # ?진 계층
    ${ALICE_SRC_DIR}/Runtime/Engine/Engine.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/EngineInitialize.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/EngineUpdate.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/EngineRender.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/EnginePhysics.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/EngineWindow.cpp
    ${ALICE_SRC_DIR}/Editor/Core/ViewportPicker.cpp
    ${ALICE_SRC_DIR}/Editor/Core/EditorCore.cpp
    ${ALICE_SRC_DIR}/Editor/Tools/Blueprint/AnimBlueprintEditor.cpp

    # 코어(ECS ????
    ${ALICE_SRC_DIR}/Runtime/ECS/World.cpp
    ${ALICE_SRC_DIR}/Runtime/Foundation/ThreadSafety.cpp
    ${ALICE_SRC_DIR}/Runtime/Scripting/IScript.cpp
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptFactory.cpp
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptHotReload.cpp
    ${ALICE_SRC_DIR}/Runtime/Resources/Prefab.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/Data/Material.cpp
    ${ALICE_SRC_DIR}/Runtime/Resources/SceneFile.cpp
    ${ALICE_SRC_DIR}/Runtime/ECS/ComponentRegistry.cpp
    ${ALICE_SRC_DIR}/Runtime/ECS/EditorComponentRegistry.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/CameraSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Resources/ResourceManager.cpp
    ${ALICE_SRC_DIR}/Runtime/Resources/Scene.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/TimeSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Input/InputSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Foundation/Logger.cpp
    ${ALICE_SRC_DIR}/Runtime/Foundation/Singleton.cpp
    ${ALICE_SRC_DIR}/Runtime/Foundation/Helper.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/Data/Vertex.cpp
    ${ALICE_SRC_DIR}/Runtime/Input/Input.cpp
    ${ALICE_SRC_DIR}/Runtime/Engine/AdvancedAnimSystem.cpp

    # Audio
    ${ALICE_SRC_DIR}/Runtime/Audio/AudioSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Audio/SoundManager.cpp

    # FBX/3D 모델 로더
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxMaterial.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxModel.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxSkeleton.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxGeometry.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxAnimation.cpp

    # AnimBlueprint
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Animation/AnimBlueprintAsset.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketAttachmentSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketWorldUpdateSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/HurtboxFactory.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/WeaponTraceSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/CombatSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/AttackDriverSystem.cpp

    # ?더?계층
    ${ALICE_SRC_DIR}/Runtime/Rendering/Camera.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/D3D11/D3D11RenderDevice.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/ForwardRenderSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/DeferredRenderSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/SkinnedMeshRegistry.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/DebugDrawSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/TrailEffectRenderSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/TrailEffectRenderSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/PostProcessVolumeSystem.cpp
    
    #PhysX
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXContext.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld_Actors.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld_Core.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld_Joints.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld_Queries.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysicsSystem.cpp
    #Physx Module
    ${ALICE_SRC_DIR}/Runtime/Physics/Module/PhysicsDebug.cpp
    ${ALICE_SRC_DIR}/Runtime/Physics/Module/PhysicsModule.cpp


    
    
    

    # ?이??코드 묶음
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/CommonShaderCode.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/DeferredShader.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/ForwardShader.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/EffectSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/ComputeEffectSystem.cpp
    ${ALICE_SRC_DIR}/Runtime/Rendering/DebugDrawComponentSystem.cpp


    # AliceUI (Shader  UI)
    ${ALICE_SRC_DIR}/Runtime/UI/UIFont.cpp
    ${ALICE_SRC_DIR}/Runtime/UI/UIRenderer.cpp
    ${ALICE_SRC_DIR}/Runtime/UI/UIShaderCode.cpp
    ${ALICE_SRC_DIR}/Runtime/UI/UICurveAsset.cpp
)

set(ENGINE_HEADERS
    # ?진 계층
    ${ALICE_SRC_DIR}/Runtime/Engine/Engine.h
    ${ALICE_SRC_DIR}/Runtime/Engine/EngineImpl.h
    ${ALICE_SRC_DIR}/Editor/Core/ViewportPicker.h
    ${ALICE_SRC_DIR}/Editor/Core/EditorCore.h
    ${ALICE_SRC_DIR}/Editor/Tools/Blueprint/AnimBlueprintEditor.h

    # 코어
    ${ALICE_SRC_DIR}/Runtime/ECS/World.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/ThreadSafety.h
    ${ALICE_SRC_DIR}/Runtime/ECS/GameObject.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/Delegate.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/IScript.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptFactory.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptSystem.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptAPI.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/ScriptHotReload.h
    ${ALICE_SRC_DIR}/Runtime/Resources/Prefab.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Data/Material.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/Logger.h
    ${ALICE_SRC_DIR}/Runtime/Engine/CameraSystem.h
    ${ALICE_SRC_DIR}/Runtime/Resources/SceneFile.h
    ${ALICE_SRC_DIR}/Runtime/Resources/SceneFileHelper.h
    ${ALICE_SRC_DIR}/Runtime/Resources/Serialization/ReflectionSerializer.h
    ${ALICE_SRC_DIR}/Editor/Core/ReflectionUI.h
    ${ALICE_SRC_DIR}/Runtime/ECS/ComponentRegistry.h
    ${ALICE_SRC_DIR}/Runtime/ECS/EditorComponentRegistry.h
    ${ALICE_SRC_DIR}/Runtime/ECS/EditorComponentRegistry.inl
    ${ALICE_SRC_DIR}/Runtime/ECS/Component.h
    ${ALICE_SRC_DIR}/Runtime/ECS/Entity.h
    ${ALICE_SRC_DIR}/Runtime/ECS/System.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/StringUtils.h
    ${ALICE_SRC_DIR}/Runtime/Resources/ResourceManager.h
    ${ALICE_SRC_DIR}/Runtime/Resources/Scene.h
    ${ALICE_SRC_DIR}/Runtime/Engine/TimeSystem.h
    ${ALICE_SRC_DIR}/Runtime/Input/InputSystem.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/ImGuiEx.h
    ${ALICE_SRC_DIR}/Runtime/Input/Input.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/Singleton.h
    ${ALICE_SRC_DIR}/Runtime/Foundation/Helper.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Data/Vertex.h
    ${ALICE_SRC_DIR}/Runtime/Engine/AdvancedAnimSystem.h
    ${ALICE_SRC_DIR}/Runtime/Resources/Serialization/JsonRttr.h
    ${ALICE_SRC_DIR}/Runtime/Input/InputTypes.h

    # Audio
    ${ALICE_SRC_DIR}/Runtime/Audio/AudioSystem.h
    ${ALICE_SRC_DIR}/Runtime/Audio/SoundManager.h

    
    # Ʈ
    ${ALICE_SRC_DIR}/Runtime/ECS/Components/ComponentStorage.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraBlendComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraFollowComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraInputComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraLookAtComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraShakeComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/CameraSpringArmComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Animation/AdvancedAnimationComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/DebugDrawBoxComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Animation/AdvancedAnimator.h
    ${ALICE_SRC_DIR}/Runtime/Audio/Components/SoundBoxComponent.h
    ${ALICE_SRC_DIR}/Runtime/Audio/Components/AudioListenerComponent.h
    ${ALICE_SRC_DIR}/Runtime/Audio/Components/AudioSourceComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/MaterialComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/PointLightComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/RectLightComponent.h
    ${ALICE_SRC_DIR}/Runtime/Scripting/Components/ScriptComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/SkinnedAnimationComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/SkinnedMeshComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/SpotLightComponent.h
    ${ALICE_SRC_DIR}/Runtime/ECS/Components/TransformComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/TrailEffectComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/EffectComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketAttachmentComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketPoseOutputComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/HurtboxComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/WeaponTraceComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/HealthComponent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/AttackDriverComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/PostProcessVolumeComponent.h

    # FBX/3D 모델 로더 ?더
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxMaterial.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxModel.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxSkeleton.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxTypes.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxGeometry.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxAnimation.h

    # ?더?계층
    ${ALICE_SRC_DIR}/Runtime/Rendering/Camera.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ForwardRenderSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/DeferredRenderSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/SkinnedMeshRegistry.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/IRenderDevice.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/D3D11/D3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/D3D11/ID3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/DebugDrawSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/DebugDrawComponentSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/RenderTypes.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/TrailEffectRenderSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/PostProcessSettings.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/PostProcessVolumeSystem.h
    
    # ?이??코드 묶음
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/CommonShaderCode.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/DeferredShader.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/ForwardShader.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/EffectSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Animation/AdvancedAnimSystem.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketAttachmentSystem.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Sockets/SocketWorldUpdateSystem.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/HurtboxFactory.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/WeaponTraceSystem.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/CombatPhysicsLayers.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/CombatHitEvent.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/CombatSystem.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Combat/AttackDriverSystem.h
    #PhysX 물리
    ${ALICE_SRC_DIR}/Runtime/Physics/IPhysicsWorld.h
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysicsMath.h
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXContext.h
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld.h
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysXWorld_Internal.h
    ${ALICE_SRC_DIR}/Runtime/Physics/PhysicsSystem.h

    #Module
    ${ALICE_SRC_DIR}/Runtime/Physics/Module/PhysicsDebug.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Module/PhysicsEcsBridge.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Module/PhysicsModule.h
    #Components
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_SettingsComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_ColliderComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_RigidBodyComponent.h    
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_MeshColliderComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_JointComponent.h





    # AliceUI (Shader  UI)
    ${ALICE_SRC_DIR}/Runtime/UI/UICommon.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIWidgetComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UITransformComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIImageComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UITextComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIButtonComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIGaugeComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIEffectComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIAnimationComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIShakeComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIHover3DComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIVitalComponent.h
    ${ALICE_SRC_DIR}/Runtime/UI/UICurveAsset.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIFont.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIRenderer.h
    ${ALICE_SRC_DIR}/Runtime/UI/UIShaderCode.h
    ${ALICE_SRC_DIR}/Runtime/UI/BindWidget.h

    # json
    ${ALICE_SRC_DIR}/ThirdParty/json/json.hpp
    ${ALICE_SRC_DIR}/ThirdParty/json/json_fwd.hpp

    ${ALICE_SRC_DIR}/Runtime/Rendering/EffectSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Physics/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/Components/ComputeEffectComponent.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ComputeEffectSystem.h
    ${ALICE_SRC_DIR}/Runtime/Rendering/ShaderCode/ComputeEffectShader.h
    ${ALICE_SRC_DIR}/Runtime/Resources/Serialization/SocketSerialization.h



)

add_library(Engine STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

# [?정] Engine??RTTR ?결 (PUBLIC: ?진???는 ?로?트??RTTR ?용 가??
target_link_libraries(Engine PUBLIC RTTR::Core)

if(MSVC)
    # ComponentRegistry.cpp??RTTR ?록??커져??obj가 커짐
    target_compile_options(Engine PRIVATE /bigobj)
endif()

source_group(TREE ${ALICE_SRC_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# ==========================================
# [ThirdParty] FMOD
# ==========================================
# FMOD 경로 변???정 (기존 FMOD_DIR ?용)
set(FMOD_INC_DIR "${FMOD_DIR}/inc")
set(FMOD_LIB_DIR "${FMOD_DIR}/64x")

# Engine??FMOD ?더 ?함 (PUBLIC: ?진???는 ?로?트??FMOD ?더 ?용 가??
if(EXISTS "${FMOD_INC_DIR}")
    target_include_directories(Engine PUBLIC "${FMOD_INC_DIR}")
endif()

# Engine??FMOD ?이브러?링크 (Debug??fmodL_vc.lib, Release??fmod_vc.lib)
if(EXISTS "${FMOD_LIB_DIR}/fmod_vc.lib" AND EXISTS "${FMOD_LIB_DIR}/fmodL_vc.lib")
    target_link_libraries(Engine PUBLIC
        debug     "${FMOD_LIB_DIR}/fmodL_vc.lib"
        optimized "${FMOD_LIB_DIR}/fmod_vc.lib"
    )
endif()

# ==========================================
# [Target] Launch & AlicePlayer
# ==========================================
# 공통 게임 코드
set(APP_COMMON_SOURCES
    ${ALICE_SRC_DIR}/Samples/Scenes/SampleScene.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxImporter.cpp
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxAsset.cpp
)

set(APP_HEADERS
    ${ALICE_SRC_DIR}/Samples/Scenes/SampleScene.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxImporter.h
    ${ALICE_SRC_DIR}/Runtime/Importing/FbxAsset.h
    ${ALICE_SRC_DIR}/Runtime/Gameplay/Animation/SkinnedMeshSystem.h
    ${ALICE_SRC_DIR}/Runtime/Engine/AdvancedAnimSystem.h
)

# 1. Launch (?디????모드) ?트??인??
set(LAUNCH_MAIN
    ${ALICE_SRC_DIR}/Samples/Sandbox/Main.cpp
)

# 2. AlicePlayer (?수 게임 모드) ?트??인??
set(PLAYER_MAIN
    ${ALICE_SRC_DIR}/Samples/Sandbox/GameMain.cpp
)

# [Target 1] Launch (기존 AliceGame/AliceRenderer)
add_executable(Launch
    ${LAUNCH_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

# [Target 2] AlicePlayer (최종 빌드???레?어)
add_executable(AlicePlayer
    ${PLAYER_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${LAUNCH_MAIN} ${PLAYER_MAIN} ${APP_COMMON_SOURCES} ${APP_HEADERS})

# ==========================================
# [ThirdParty] ImGui
# ==========================================

# Engine??ImGui ?더?찾을 ???도??정
target_include_directories(Engine
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUI_NODE_EDITOR_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/rttr/src  # RTTR  
)


# ImGui ?적 ?이브러?
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

# Launch? AlicePlayer가 ImGui ?더?찾을 ???도??정
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${ALICE_SRC_DIR}
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
    )
    if(EXISTS "${FMOD_DIR}/inc")
        target_include_directories(${target} PRIVATE "${FMOD_DIR}/inc")
        target_link_directories(${target} PRIVATE "${FMOD_DIR}/64x")
    endif()
endforeach()

# ==========================================
# [ThirdParty] ImGuizmo
# ==========================================

# Engine??ImGuizmo ?더?찾을 ???도??정
target_include_directories(Engine
    PRIVATE
        ${IMGUIZMO_DIR}
)

# ImGuizmo ?적 ?이브러?
add_library(imguizmo STATIC
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.h
)

target_include_directories(imguizmo
    PUBLIC
        ${IMGUIZMO_DIR}
)

# ImGuizmo?DirectX 모드?컴파??(LH 좌표? depth 0~1)
target_compile_definitions(imguizmo
    PUBLIC
        IMGUIZMO_USE_DIRECTX
)

# Engine???일???의 ?용
target_compile_definitions(Engine
    PRIVATE
        IMGUIZMO_USE_DIRECTX
)

# ImGuizmo??ImGui???존
target_link_libraries(imguizmo
    PUBLIC
        imgui
)

# ?행 ?일?이 ImGuizmo ?더?찾을 ???도??정
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${IMGUIZMO_DIR}
    )
endforeach()

# ==========================================
# [ThirdParty] ImGui Node Editor
# ==========================================

# 경로 반영
#set(IMGUI_NODE_EDITOR_INCLUDE "${IMGUI_NODE_EDITOR_DIR}")
#set(IMGUI_NODE_EDITOR_SOURCE  "${IMGUI_NODE_EDITOR_DIR}")
#
#if(NOT EXISTS "${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor.cpp")
#    message(FATAL_ERROR
#        "?류: imgui-node-editor ?스?찾을 ???습?다!\n"
#        "경로 ?인: ${IMGUI_NODE_EDITOR_SOURCE}\n"
#        "cpp/h ?일?이 ?위 ?더 ?이 ?당 ?치??바로 ?는지 ?인?주?요."
#    )
#endif()
#
#add_library(imgui_node_editor STATIC
#    ${IMGUI_NODE_EDITOR_SOURCE}/crude_json.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_canvas.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor_api.cpp
#)
#
#target_include_directories(imgui_node_editor PUBLIC
#    ${IMGUI_NODE_EDITOR_INCLUDE}
#)
#
#target_link_libraries(imgui_node_editor PUBLIC
#    imgui
#)
#
#target_compile_definitions(imgui_node_editor PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# Engine?서 ?는?보통?니 Engine?만 물림(?행?일?는 중복 링크 X)
#target_link_libraries(Engine PUBLIC imgui_node_editor)

# ==========================================
# [?정] 컴파???션 ?vcpkg ?용
# ==========================================
if(MSVC)
    # 경고 ?벨
    target_compile_options(Engine      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(Launch      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(AlicePlayer PRIVATE /W4 /permissive- /bigobj)

    # Include 경로 추? (?단?서 ?정??VCPKG_ROOT ?용)
    target_include_directories(Engine
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # Launch? AlicePlayer??VCPKG Include/Lib 경로 ?정
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

# Win32 ?위 ?스??(콘솔 ??)
set_target_properties(Launch PROPERTIES WIN32_EXECUTABLE YES)
set_target_properties(AlicePlayer PROPERTIES WIN32_EXECUTABLE YES)

# ==========================================
# [Link] ?이브러??결
# ==========================================
# ????모두 ?일???이브러??존?을 가?
foreach(target Launch AlicePlayer)
    target_link_libraries(${target}
        PRIVATE
            Engine          # ?심 ?진
            imgui
            imguizmo
            #imgui_node_editor
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

# Launch? AlicePlayer ??모두??DLL 복사 ?행
foreach(target Launch AlicePlayer)
    add_custom_command(TARGET ${target} POST_BUILD
        # RTTR shared ?크립트 DLL?registry 공유
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
   
    # vcpkg toolchain   ̶, find_package ã  ְ prefix ߰
    list(PREPEND CMAKE_PREFIX_PATH
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}"
    )

    # vcpkg physx ?트가 ?보?는 ?키지/??
    find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

    # ?진??PhysX ?결 (Debug/Release ?바?lib 조합? ?겟이 ?아??처리)
    target_link_libraries(Engine PUBLIC unofficial::omniverse-physx-sdk::sdk)

    # include 보강
    target_include_directories(Engine PRIVATE
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include/physx"
    )

    # PhysX ????DLL 복사 (?요??것만 최소?
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



# ==========================================
# [Post-Build] FMOD DLL 복사
# ==========================================
if(WIN32)
    if(NOT EXISTS "${FMOD_LIB_DIR}/fmod.dll")
        message(FATAL_ERROR "[FMOD] not found: ${FMOD_LIB_DIR}/fmod.dll")
    endif()
    if(NOT EXISTS "${FMOD_LIB_DIR}/fmodL.dll")
        message(FATAL_ERROR "[FMOD] not found: ${FMOD_LIB_DIR}/fmodL.dll")
    endif()

    # Launch? AlicePlayer ?겟에 DLL 복사
    foreach(t Launch AlicePlayer)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FMOD_LIB_DIR}/fmod.dll"
                "$<TARGET_FILE_DIR:${t}>/fmod.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FMOD_LIB_DIR}/fmodL.dll"
                "$<TARGET_FILE_DIR:${t}>/fmodL.dll"
            VERBATIM
        )
    endforeach()
endif()
