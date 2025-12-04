#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>

#include "Core/Entity.h"
#include "Core/World.h"
#include "Core/Scene.h"
#include "Core/Script.h"
#include "Rendering/Camera.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Editor/ViewportPicker.h"

namespace Alice
{
    struct ID3D11RenderDevice;

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
                          SceneManager* sceneManager,
                          float deltaTime,
                          float fps,
                          bool& isPlaying,
                          int& shadingMode,
                          bool& useFillLight,
                          EntityId& selectedEntity,
                          ViewportPicker& picker);

        /// 프로젝트 뷰에서 사용할 간단한 디렉터리 트리 그리기 함수입니다.
        void DrawDirectoryNode(World& world,
                               EntityId& selectedEntity,
                               const std::filesystem::path& path);

    private:
        bool m_initialized = false;
    };
}



