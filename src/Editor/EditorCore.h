#pragma once

// Windows.h의 min/max 매크로 충돌 방지 (RTTR 헤더와의 충돌 방지)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>

#include "Core/Entity.h"
#include "Core/World.h"
#include "Core/Scene.h"
#include "Core/IScript.h"
#include "Rendering/Camera.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Editor/ViewportPicker.h"
#include "Core/InputSystem.h"

namespace Alice
{
    struct ID3D11RenderDevice;
    class ResourceManager;
    class SkinnedMeshRegistry;
    class DeferredRenderSystem;

    /// ImGui 컨텍스트 수명과 기본 에디터 유틸(도킹, 디렉터리 뷰, 에디터 패널 등)을 관리하는
    /// 간단한 코어 클래스입니다.
    class EditorCore
    {
    public:
        EditorCore() = default;
        ~EditorCore();

        /// ImGui 컨텍스트와 백엔드(Win32 + DX11)를 초기화합니다.
        bool Initialize(HWND hwnd, ID3D11RenderDevice& renderDevice);

        /// ImGui 리소스를 정리합니다.
        void Shutdown();

        /// 새 ImGui 프레임을 시작합니다.
        void BeginFrame();

        /// ImGui 드로우 데이터를 렌더링합니다.
        void RenderDrawData();

        /// 에디터 전체 UI(Hierarchy, Inspector, Game, Project 등)를 그립니다.
        /// - 상태값(재생 여부, 셰이딩 모드, 선택된 엔티티 등)은 참조로 받아 직접 갱신합니다.
        void DrawEditorUI(World& world,
                          Camera& camera,
                          ForwardRenderSystem& forward,
                          DeferredRenderSystem& deferred,
                          SceneManager* sceneManager,
                          float deltaTime,
                          float fps,
                          bool& isPlaying,
                          int& shadingMode,
                          bool& useFillLight,
                          EntityId& selectedEntity,
                          ViewportPicker& picker,
                          float& cameraMoveSpeed,
                          bool& useForwardRendering);

        void DrawInspectorTransform(World& world, const EntityId& _selectedEntity);
        void DrawInspectorScripts(World& world, const EntityId& _selectedEntity);
        void DrawInspectorMaterial(World& world, const EntityId& _selectedEntity);
        void DrawInspectorPointLight(World& world, const EntityId& _selectedEntity);
        void DrawInspectorSpotLight(World& world, const EntityId& _selectedEntity);
        void DrawInspectorRectLight(World& world, const EntityId& _selectedEntity);

        /// 프로젝트 뷰에서 사용할 간단한 디렉터리 트리 그리기 함수입니다.
        void DrawDirectoryNode(World& world,
                               EntityId& selectedEntity,
                               const std::filesystem::path& path);

    public:
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_skinnedRegistry = registry; }
        void SetInputSystem(InputSystem* inputSystem) { m_inputSystem = inputSystem; }

    private:
        /// 씬을 로드한 뒤, World 에 존재하는 SkinnedMeshComponent 들이
        /// SkinnedMeshRegistry 에도 등록되어 있는지 확인하고,
        /// 누락된 경우 .fbxasset / FBX 원본을 통해 간단히 재-임포트합니다.
        void EnsureSkinnedMeshesRegistered(World& world);
        void SaveScene(World& );
        void LoadScene(World& );

    private:
        bool               m_initialized = false;
        HWND               m_hwnd        = nullptr;
        ID3D11RenderDevice* m_renderDevice = nullptr;
        ResourceManager*    m_resources    = nullptr;
        SkinnedMeshRegistry* m_skinnedRegistry = nullptr;
        InputSystem*        m_inputSystem = nullptr;

        bool               m_scriptBuilded = false;
    };
}



