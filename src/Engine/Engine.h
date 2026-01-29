#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <memory>
#include <filesystem>
#include <Core/ComponentRegistry.h>

namespace Alice
{
    /// 엔진 전체를 관리하는 가장 상위 레벨 클래스입니다.
    /// - 윈도우 생성 및 메시지 루프 관리
    /// - World 및 시스템 업데이트
    /// - 렌더 디바이스에게 렌더링을 요청
    class Engine
    {
    public:
        /// \param editorMode true 이면 에디터(도킹 UI) 모드, false 이면 게임 전용 모드
        Engine(bool editorMode = true);
        ~Engine();
        
        /// 엔진 종료 (명시적 종료 호출)
        void Shutdown();

        /// 엔진과 윈도우, 렌더 디바이스를 초기화합니다.
        bool Initialize(HINSTANCE hInstance, int nCmdShow);

        /// 메인 루프를 실행합니다.
        int Run();

        /// 윈도우 메시지를 처리하는 멤버 함수입니다.
        LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        // Win32 전역 윈도우 프로시저 → Engine 인스턴스로 위임
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        /// 한 프레임의 업데이트(게임 로직)를 수행합니다.
        void Update();

        /// 한 프레임의 렌더링을 수행합니다.
        void Render();

        /// 기본 윈도우를 생성합니다.
        bool CreateMainWindow(int nCmdShow);

        /// 윈도우 크기 변경 시 호출됩니다.
        void OnResize(std::uint32_t width, std::uint32_t height);

        /// 월드 안의 SkinnedMeshComponent 들에 대해,
        /// SkinnedMeshRegistry 에 GPU 메시가 등록되어 있는지 확인하고,
        /// 필요 시 FBX 를 다시 임포트해서 등록합니다.
        void EnsureSkinnedMeshesRegisteredForWorld();

        /// 씬 전환 시 IBL 세트를 업데이트합니다.
        /// - onAfterSceneLoaded 델리게이트에 연결되어 자동으로 호출됩니다.
        void UpdateIblForScene();

        void TrimVideoMemory();

        /// 렌더링 모드를 설정합니다 (true: Forward, false: Deferred)
        void SetUseForwardRendering(bool useForward);
        bool GetUseForwardRendering() const;
        //===========================================
        //물리
        void RefreshPhysicsForCurrentWorld();
        void TickPhysics(float dt);
        void ProcessPhysicsEvents();
        void ProcessCombatHits();
        /// 월드와 물리 시스템을 함께 정리하는 안전한 진입점
        /// World::Clear()와 PhysicsSystem 정리를 함께 처리하여 누락을 방지
        void ClearWorldAndPhysics();
        //===========================================

        // =========================
        // Initialize helpers
        void InitializeMainThreadAndRegistry();
        std::filesystem::path InitializeResolveExeDir();
        bool InitializeConfigureResourceManagers(const std::filesystem::path& exeDir);
        bool InitializeValidateGameDataIfNeeded();
        void InitializeLoadPvdSettings(const std::filesystem::path& exeDir);
        bool InitializePhysicsContext();
        bool InitializeWindowAndInput(int nCmdShow);
        bool InitializeRenderDevice();
        bool InitializeEditorCoreIfNeeded();
        void InitializeAudio();
        bool InitializeRenderSystems();
        bool InitializeUI();
        bool InitializeComputeEffectSystem();
        void InitializeCameraAndScriptHotReload();
        bool InitializeScene(const std::filesystem::path& exeDir);
        bool InitializePhysicsSystemAndWorldCallbacks();
        void InitializePostLoadBindings();
        void InitializeEnsureUIResourcesAfterSceneLoad();

        // =========================
        // Update helpers
        void UpdateTimerAndInput(float& outDt);
        bool UpdateShouldUpdateFromScene() const;
        void UpdateSceneAndScript(float dt);
        bool UpdateCommitPendingSceneChanges(float dt); // true면 "sceneChangedThisFrame"
        void UpdateAttackDriver();
        void UpdateEnsurePhysicsWorldIfNeeded();
        void UpdatePhysicsBridge(float dt);
        void UpdatePhysicsSim(float dt);
        void UpdateAnimationAndSockets(float dt);
        void UpdateCombat(float dt);
        void UpdateCameraSystems(float dt);
        void UpdateSyncPrimaryCameraFromWorld();
        void UpdateEditorFreeCam(float dt);
        void UpdateApplyFinalCameraLookAt();
        void UpdateUI(float dt);

        // =========================
        // Render helpers
        void RenderUpdateWorldTransformCache();
        void RenderHandlePendingRenderSystemChange();
        bool RenderValidateRenderSystems() const;
        void RenderBeginFrame();
        void RenderEditorUI();
        void RenderEditorDebugBuild();
        void RenderEnsureAnimationIfNotUpdated();
        void RenderBuildSkinnedDrawList();
        void RenderOnDemandSkinnedMeshLoading();
        void RenderAudioUpdate();
        void RenderMainPass();
        void RenderUnbindDepthOnly();
        void RenderComputeEffects();
        void RenderParticleOverlayComposite();
        void RenderDebugOverlayComposite();
        void RenderGameModeToneMappingAndUI();
        void RenderOverlayEffects();
        void RenderEditorDraw();
        void RenderEndFrame();

    private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
    };
}

