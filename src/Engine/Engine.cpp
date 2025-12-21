#include "Engine/Engine.h"

#include "Rendering/D3D11/D3D11RenderDevice.h"
#include "Rendering/DebugDrawSystem.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder API 사용
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Win32 메시지 헬퍼 (GET_X/Y_LPARAM)
#include <Windowsx.h>

// 표준 라이브러리
#include <filesystem>
#include <cfloat>      // FLT_MAX
#include <algorithm>   // std::max
#include <memory>
#include <fstream>
#include <sstream>
#include "json/json.hpp"

// Core
#include "Core/World.h"
#include "Core/InputSystem.h"
#include "Core/TimeSystem.h"
#include "Core/ResourceManager.h"
#include "Core/Scene.h"
#include "Core/Script.h"
#include "Core/Delegate.h"
#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Editor/ViewportPicker.h"
#include "Editor/EditorCore.h"
#include "Game/SkinnedMeshSystem.h"
#include "Game/SkinnedAnimationSystem.h"

// 문자열 변환 / ImGui 래퍼
#include "Core/StringUtils.h"
#include "Core/ImGuiEx.h"
#include "Core/ScriptHotReload.h"
#include "Core/SceneFile.h"
#include "Core/Logger.h"
#include "Game/FbxImporter.h"
#include "Game/FbxAsset.h"
#include <dxgi1_3.h>
#include <unordered_set>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Alice
{
	struct Engine::Impl
	{
		enum class ShadingMode
		{
			Lambert = 0,
			Phong = 1,
			BlinnPhong = 2,
			Toon = 3,
			PBR = 4
		};

		HINSTANCE m_hInstance = nullptr;
		HWND      m_hWnd = nullptr;

		std::uint32_t m_width = 1600;
		std::uint32_t m_height = 900;

		bool m_isRunning = false;            // 엔진 자체가 실행중인지 판단
		bool m_isPlaying = false;            // 재생 / 일시정지 상태 (에디터 모드에서만 사용)
		bool m_editorMode = true;             // true: 에디터, false: 게임 전용
		EntityId m_selectedEntity{ InvalidEntityId }; // 현재 선택된 엔티티 (하이러키)

		World          m_world;
		Camera         m_camera;
		InputSystem    m_inputSystem;
		GameTimer      m_timer;
		ResourceManager m_resourceManager;
		std::unique_ptr<SceneManager> m_sceneManager;

		ScriptSystem   m_scriptSystem;

		ViewportPicker m_viewportPicker;
		EditorCore     m_editorCore;

		ShadingMode m_shadingMode{ ShadingMode::PBR };
		bool        m_useFillLight{ true };

		// 카메라 이동/회전을 위한 내부 상태 값들
		DirectX::XMFLOAT3 m_cameraPosition{ 0.0f, 2.0f, -5.0f };
		float             m_cameraYawRadians = 0.0f;  // Yaw (좌우 회전)
		float             m_cameraPitchRadians = 0.0f;  // Pitch (상하 회전)

		float             m_cameraMoveSpeed = 8.0f;     // 초당 이동 속도
		float             m_cameraMouseSensitivity = 0.0025f; // 마우스 감도 (라디안/픽셀)

		std::unique_ptr<ID3D11RenderDevice>  m_renderDevice;
		std::unique_ptr<ForwardRenderSystem> m_forwardRenderSystem;
		std::unique_ptr<class DebugDrawSystem> m_debugDrawSystem;

		// Skinned FBX 메시 렌더링용 레지스트리/시스템
		SkinnedMeshRegistry m_skinnedMeshRegistry;
		SkinnedMeshSystem   m_skinnedMeshSystem{ m_skinnedMeshRegistry };
		SkinnedAnimationSystem m_skinnedAnimSystem{ m_skinnedMeshRegistry };
		std::vector<ForwardRenderSystem::SkinnedDrawCommand> m_skinnedDrawCommands;
	};
	namespace
	{
		// 윈도우 클래스 이름은 전역 상수로 관리합니다.
		constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";

		// BuildSettings.txt 에서 시작 씬(.scene 파일)을 읽어와 World 에 로드합니다.
		// - scenes 섹션은 "index: path" 형식으로 저장되어 있다고 가정합니다.
		bool LoadStartupSceneFromBuildSettings(World& world, const ResourceManager& resources, const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;

			// 경로 설정 (상수 없이 바로 대입)
			fs::path cfg = exeDir / "BuildSettings.json";
			if (!fs::exists(cfg)) // 빌드 경로 없으면 프로젝트 루트 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "Build/BuildSettings.json";

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return false;

			nlohmann::json j;
			try { ifs >> j; }
			catch (...) { return false; }

			std::string target = j.value("default", std::string{});
			std::vector<std::string> scenes;
			if (j.contains("scenes") && j["scenes"].is_array())
			{
				for (const auto& v : j["scenes"])
					if (v.is_string()) scenes.push_back(v.get<std::string>());
			}

			// 씬 결정 및 경로 보정
			if (target.empty() && !scenes.empty()) target = scenes[0];
			if (target.empty()) return false;

			const fs::path logicalScene = fs::path(target);
			ALICE_LOG_INFO("Loading Startup Scene: %s", logicalScene.string().c_str());

			// gameMode에서는 Assets/... 가 Metas/Chunks 로 패킹되어 있으므로 LoadAuto를 사용합니다.
			if (!SceneFile::LoadAuto(world, resources, logicalScene))
			{
				ALICE_LOG_ERRORF("Scene Load Failed: %s", logicalScene.string().c_str());
				return false;
			}

			return true;
		}
	}

	Engine::Engine(bool editorMode) : pImpl(std::make_unique<Impl>())
	{
		pImpl->m_editorMode = editorMode;
		pImpl->m_scriptSystem.SetEditorMode(editorMode);
	}

	Engine::~Engine()
	{
		pImpl->m_editorCore.Shutdown();
	}

	bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
	{
		LinkComponentRegistry();
		ALICE_LOG_INFO("Engine::Initialize: begin (editorMode=%d)", pImpl->m_editorMode ? 1 : 0);

		// 1) 인스턴스 핸들 보관
		pImpl->m_hInstance = hInstance;

		// ResourceManager: 경로 해석 기준을 "모드"로 단순하게 고정합니다.
		// - editorMode(true)  : 프로젝트 루트 기준(= exeDir/../../..) Assets/Resource/Cooked
		// - gameMode(false)   : exeDir 기준 Assets/Resource/Cooked
		{
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			const std::filesystem::path exeDir = std::filesystem::path(exePathW).parent_path();
			pImpl->m_resourceManager.Configure(/*gameMode=*/!pImpl->m_editorMode, exeDir);
		}

		// 2) 윈도우 생성
		if (!CreateMainWindow(nCmdShow))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: CreateMainWindow failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: CreateMainWindow succeeded.");

		// 3) 입력 시스템 초기화 (DirectXTK Keyboard/Mouse)
		pImpl->m_inputSystem.Initialize(pImpl->m_hWnd);
		ALICE_LOG_INFO("Engine::Initialize: InputSystem initialized.");

		// 4) 렌더 디바이스 생성(D3D11 구현체 사용)
		pImpl->m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!pImpl->m_renderDevice->Initialize(pImpl->m_hWnd, pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: D3D11RenderDevice::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: D3D11RenderDevice initialized.");

		// 5) ImGui / Editor 코어 초기화 (에디터 모드에서만)
		if (pImpl->m_editorMode)
		{
			// EditorCore::Initialize 단계에서도 폰트/아이콘 등 리소스 경로가 필요하므로,
			// 리소스 포인터는 Initialize 이전에 주입합니다.
			pImpl->m_editorCore.SetResourceManager(&pImpl->m_resourceManager);
			pImpl->m_editorCore.SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);
			pImpl->m_editorCore.SetInputSystem(&pImpl->m_inputSystem);

			if (!pImpl->m_editorCore.Initialize(pImpl->m_hWnd, *pImpl->m_renderDevice))
			{
				ALICE_LOG_ERRORF("Engine::Initialize: EditorCore::Initialize failed.");
				return false;
			}
			ALICE_LOG_INFO("Engine::Initialize: EditorCore initialized.");
		}

		// 6) Forward 렌더 시스템 초기화
		pImpl->m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*pImpl->m_renderDevice);
		// 리소스 매니저를 렌더 시스템에 주입합니다 (텍스처 쿠킹/로딩 등에 사용).
		pImpl->m_forwardRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		// 스키닝 메시 레지스트리를 렌더 시스템에 주입 (서브셋/스켈레톤 메타데이터 조회용)
		pImpl->m_forwardRenderSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);
		if (!pImpl->m_forwardRenderSystem->Initialize(pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: ForwardRenderSystem::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: ForwardRenderSystem initialized.");

		// 7) DebugDraw 시스템 초기화 (옵션 기능)
		pImpl->m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_debugDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("Engine::Initialize: DebugDrawSystem::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: DebugDrawSystem initialized.");

		// 8) 카메라 설정
		const float aspect = static_cast<float>(pImpl->m_width) / static_cast<float>(pImpl->m_height);
		pImpl->m_cameraPosition = DirectX::XMFLOAT3(0.0f, 2.0f, -5.0f);
		DirectX::XMFLOAT3 target(0.0f, 0.0f, 0.0f);
		pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, target, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
		pImpl->m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 5000.0f);

		// 9) 스크립트 DLL (라이브 코딩용) 로드 시도
		ScriptHotReload_Load();
		ALICE_LOG_INFO("Engine::Initialize: ScriptHotReload_Load called.");

		// 10) 씬 매니저 생성 및 기본 씬/씬 파일 로드
		pImpl->m_resourceManager.Clear();
		pImpl->m_sceneManager = std::make_unique<SceneManager>(pImpl->m_world, pImpl->m_resourceManager);
		ALICE_LOG_INFO("Engine::Initialize: SceneManager created.");

		// 에디터 모드: 코드 기반 SampleScene 을 기본으로 사용
		if (pImpl->m_editorMode)
		{
			pImpl->m_sceneManager->SwitchTo("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: editor mode, switched to SampleScene.");
		}
		else
		{
			// 게임 모드: BuildSettings.txt 에 정의된 0번 인덱스 씬(.scene)을 우선 로드
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			std::filesystem::path exePath = exePathW;
			std::filesystem::path exeDir = exePath.parent_path();

			if (!LoadStartupSceneFromBuildSettings(pImpl->m_world, pImpl->m_resourceManager, exeDir))
			{
				// 실패 시 최후의 수단으로 SampleScene 을 사용
				pImpl->m_sceneManager->SwitchTo("SampleScene");
				ALICE_LOG_WARN("Engine::Initialize: failed to load startup scene from BuildSettings, fallback to SampleScene.");
			}
			else
			{
				ALICE_LOG_INFO("Engine::Initialize: startup scene loaded from BuildSettings.");
			}
		}

		// 월드 안의 SkinnedMeshComponent 들에 대응하는 GPU 메시들이
		// SkinnedMeshRegistry 에 모두 등록되어 있는지 확인합니다.
		EnsureSkinnedMeshesRegisteredForWorld();

		// ScriptSystem 에 서비스 연결 (입력/씬/리소스/스키닝 레지스트리)
		pImpl->m_scriptSystem.SetServices(&pImpl->m_inputSystem, pImpl->m_sceneManager.get(), &pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);
		pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::EnsureSkinnedMeshesRegisteredForWorld);
		//pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::UpdateIblForScene);
		pImpl->m_scriptSystem.onTrimVideoMemory.BindObject(this, &Engine::TrimVideoMemory);

		const auto& transforms = pImpl->m_world.GetTransforms();
		const auto& skinnedMeshes = pImpl->m_world.GetSkinnedMeshes();
		const auto& scripts = pImpl->m_world.GetAllScripts();
		const auto& materials = pImpl->m_world.GetMaterials();
		ALICE_LOG_INFO("Engine::Initialize: world summary: transforms=%zu, skinnedMeshes=%zu, scripts=%zu, materials=%zu",
			transforms.size(), skinnedMeshes.size(), scripts.size(), materials.size());

		ALICE_LOG_INFO("Engine::Initialize: success.");
		return true;
	}

	int Engine::Run()
	{
		pImpl->m_isRunning = true;

		MSG msg = {};

		// 고해상도 타이머 초기화
		pImpl->m_timer.Reset();
		pImpl->m_timer.Start();

		// 기본 게임 루프
		while (pImpl->m_isRunning)
		{
			// 1) 윈도우 메시지 처리
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					pImpl->m_isRunning = false;
					break;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (!pImpl->m_isRunning) break;

			Update();
			Render();
		}

		// 종료 라이프사이클
		pImpl->m_scriptSystem.OnApplicationQuit(pImpl->m_world);
		return static_cast<int>(msg.wParam);
	}

	void Engine::Update()
	{
		pImpl->m_timer.Tick();
		pImpl->m_inputSystem.Update(pImpl->m_timer.DeltaTime());

		using namespace DirectX;

		// 에디터 모드:
		// - Play 전  : 기존 프리 카메라 조작(뷰포트 편집용)
		// - Play 중  : 게임 모드처럼 씬의 CameraComponent(메인 카메라)로 갱신
		// 게임 모드  : 항상 씬의 CameraComponent 기반
		if (pImpl->m_editorMode && !pImpl->m_isPlaying)
		{
			const bool canControlCamera = pImpl->m_inputSystem.IsRightButtonDown();
			XMVECTOR moveDir = XMVectorZero();

			if (canControlCamera)
			{
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::W)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::S)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::D)) moveDir = XMVectorAdd(moveDir, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::A)) moveDir = XMVectorAdd(moveDir, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::E)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
				if (pImpl->m_inputSystem.IsKeyDown(Keyboard::Q)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));

				if (!XMVector3Equal(moveDir, XMVectorZero()))
				{
					XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0.0f);
					XMVECTOR worldMoveDir = XMVector3TransformNormal(moveDir, rotMatrix);
					worldMoveDir = XMVector3Normalize(worldMoveDir);

					XMVECTOR pos = XMLoadFloat3(&pImpl->m_cameraPosition);
					pos = XMVectorAdd(pos, XMVectorScale(worldMoveDir, pImpl->m_cameraMoveSpeed * pImpl->m_timer.DeltaTime()));
					XMStoreFloat3(&pImpl->m_cameraPosition, pos);
				}

				POINT mouseDelta = pImpl->m_inputSystem.GetMouseDelta();
				pImpl->m_cameraYawRadians += static_cast<float>(mouseDelta.x) * pImpl->m_cameraMouseSensitivity;
				pImpl->m_cameraPitchRadians += static_cast<float>(mouseDelta.y) * pImpl->m_cameraMouseSensitivity;
			}

			const float pitchLimit = XMConvertToRadians(89.0f);
			if (pImpl->m_cameraPitchRadians > pitchLimit)  pImpl->m_cameraPitchRadians = pitchLimit;
			if (pImpl->m_cameraPitchRadians < -pitchLimit) pImpl->m_cameraPitchRadians = -pitchLimit;

			XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0.0f);
			XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);

			XMVECTOR pos = XMLoadFloat3(&pImpl->m_cameraPosition);
			XMVECTOR target = XMVectorAdd(pos, forward);

			XMFLOAT3 targetFloat3;
			XMStoreFloat3(&targetFloat3, target);

			pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, targetFloat3, XMFLOAT3(0.0f, 1.0f, 0.0f));
		}
		else
		{
			EntityId camEntity = InvalidEntityId;
			for (const auto& [id, cam] : pImpl->m_world.GetCameras())
			{
				if (cam.primary) { camEntity = id; break; }
				if (camEntity == InvalidEntityId) camEntity = id;
			}

			if (camEntity != InvalidEntityId)
			{
				const auto* t = pImpl->m_world.GetTransform(camEntity);
				const auto* c = pImpl->m_world.GetCamera(camEntity);
				if (t && c)
				{
					pImpl->m_cameraPosition = t->position;

					// Transform.rotation(라디안)을 yaw/pitch로 사용 (y=Yaw, x=Pitch)
					pImpl->m_cameraYawRadians = t->rotation.y;
					pImpl->m_cameraPitchRadians = t->rotation.x;

					const float pitchLimit = XMConvertToRadians(89.0f);
					if (pImpl->m_cameraPitchRadians > pitchLimit)  pImpl->m_cameraPitchRadians = pitchLimit;
					if (pImpl->m_cameraPitchRadians < -pitchLimit) pImpl->m_cameraPitchRadians = -pitchLimit;

					const float aspect = static_cast<float>(pImpl->m_width) / static_cast<float>(pImpl->m_height);
					pImpl->m_camera.SetPerspective(c->fovYRad, aspect, c->nearPlane, c->farPlane);

					XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0.0f);
					XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);
					XMVECTOR pos = XMLoadFloat3(&pImpl->m_cameraPosition);
					XMVECTOR target = XMVectorAdd(pos, forward);
					XMFLOAT3 targetFloat3;
					XMStoreFloat3(&targetFloat3, target);

					pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, targetFloat3, XMFLOAT3(0.0f, 1.0f, 0.0f));
				}
			}
		}

		// 4) 현재 씬 및 스크립트 업데이트
		//    - 에디터 모드: Play 버튼이 눌렸을 때만 진행
		//    - 게임 전용 모드: 항상 진행
		const bool play = pImpl->m_editorMode ? pImpl->m_isPlaying : true;
		if (play)
		{
			if (pImpl->m_sceneManager)
			{
				pImpl->m_sceneManager->Update(pImpl->m_timer.DeltaTime());
			}

			// Unity 스타일 스크립트 라이프사이클 수행
			pImpl->m_scriptSystem.Tick(pImpl->m_world, pImpl->m_timer.DeltaTime());
		}
	}

	void Engine::Render()
	{
		if (!pImpl->m_renderDevice || !pImpl->m_forwardRenderSystem)
			return;

		// 화면 클리어 색상 (짙은 파란색 계열)
		const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

		pImpl->m_renderDevice->BeginFrame(clearColor);

		// 에디터 모드에서만 ImGui/도킹 UI + 디버그 축을 그립니다.
		if (pImpl->m_editorMode)
		{
			// ImGui 프레임 시작 (EditorCore 에 위임)
			pImpl->m_editorCore.BeginFrame();

			const float dt = pImpl->m_timer.DeltaTime();
			const float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
			int shadingModeValue = static_cast<int>(pImpl->m_shadingMode);
			pImpl->m_editorCore.DrawEditorUI(
				pImpl->m_world,
				pImpl->m_camera,
				*pImpl->m_forwardRenderSystem,
				pImpl->m_sceneManager.get(),
				dt,
				fps,
				pImpl->m_isPlaying,
				shadingModeValue,
				pImpl->m_useFillLight,
				pImpl->m_selectedEntity,
				pImpl->m_viewportPicker,
				pImpl->m_cameraMoveSpeed);
			pImpl->m_shadingMode = static_cast<Alice::Engine::Impl::ShadingMode>(shadingModeValue);

			// DebugDraw 라인 초기화 및 예제 축(axis) 추가
			if (pImpl->m_debugDrawSystem)
			{
				pImpl->m_debugDrawSystem->Clear();

				// 원점에서 XYZ 축을 그립니다.
				// X: 빨강, Y: 초록, Z: 파랑
				pImpl->m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
				pImpl->m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f),
					DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f));
				pImpl->m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f),
					DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f));
			}
		}

		// 스키닝 애니메이션(본 팔레트)을 먼저 갱신합니다.
		// - 에디터 모드에서도 Animation 탭에서 스크럽/재생이 즉시 반영되도록 Render 단계에서 갱신합니다.
		// - dt=0 이어도(일시정지) 사용자가 시간을 바꾸면 팔레트가 갱신됩니다.
		pImpl->m_skinnedAnimSystem.Update(pImpl->m_world, (double)pImpl->m_timer.DeltaTime());

		// 스키닝 메시 드로우 리스트를 먼저 구성합니다.
		pImpl->m_skinnedMeshSystem.BuildDrawList(pImpl->m_world, pImpl->m_skinnedDrawCommands);

		// 간단한 Forward 렌더링 (큐브 + 스키닝 메시)
		EntityId renderEntity = InvalidEntityId;
		if (pImpl->m_sceneManager) renderEntity = pImpl->m_sceneManager->GetPrimaryRenderableEntity();

		// 게임 모드에서는 PBR을 고정으로 사용
		int shadingModeValue2 = static_cast<int>(pImpl->m_shadingMode);
		if (!pImpl->m_editorMode) shadingModeValue2 = static_cast<int>(Impl::ShadingMode::PBR); // 4

		auto cameras = pImpl->m_world.GetCameras();
		std::unordered_set<EntityId> cameraEntities;
		for (const auto& [id, _] : cameras) cameraEntities.insert(id);

		pImpl->m_forwardRenderSystem->Render(
			pImpl->m_world,
			pImpl->m_camera,
			renderEntity,
			cameraEntities,
			shadingModeValue2,
			pImpl->m_useFillLight,
			pImpl->m_skinnedDrawCommands);

		{
			auto* ctx = pImpl->m_renderDevice->GetImmediateContext();

			auto* backBufferRTV = pImpl->m_renderDevice->GetBackBufferRTV();

			// SRV/RTV 에서 리소스 꺼내기
			Microsoft::WRL::ComPtr<ID3D11Resource> src;
			Microsoft::WRL::ComPtr<ID3D11Resource> dst;

			// src: ForwardRenderSystem 의 컬러 텍스처
			auto* sceneSRV = pImpl->m_forwardRenderSystem->GetSceneSRV();
			sceneSRV->GetResource(src.GetAddressOf());

			// dst: 백버퍼 텍스처
			backBufferRTV->GetResource(dst.GetAddressOf());

			// 실제 복사
			ctx->CopyResource(dst.Get(), src.Get());
		}

		// DebugDraw 렌더링 (Forward 렌더 이후, 같은 카메라 기준)
		if (pImpl->m_debugDrawSystem)
		{
			pImpl->m_debugDrawSystem->Render(pImpl->m_camera);
		}

		// ImGui 렌더링 (에디터 모드에서만)
		if (pImpl->m_editorMode)
		{
			pImpl->m_editorCore.RenderDrawData();
		}

		pImpl->m_renderDevice->EndFrame();
	}

	void Engine::EnsureSkinnedMeshesRegisteredForWorld()
	{
		if (!pImpl->m_renderDevice) return;

		auto* device = pImpl->m_renderDevice->GetDevice();
		if (!device) return;

		const auto& skinnedMap = pImpl->m_world.GetSkinnedMeshes();
		if (skinnedMap.empty())
		{
			ALICE_LOG_INFO("Engine::EnsureSkinnedMeshesRegisteredForWorld: no SkinnedMeshComponents in world.");
			return;
		}

		for (const auto& [entityId, comp] : skinnedMap)
		{
			if (comp.meshAssetPath.empty()) continue;

			if (pImpl->m_skinnedMeshRegistry.Find(comp.meshAssetPath)) continue; // 이미 등록됨

			std::filesystem::path fbxAssetPath;
			if (!comp.instanceAssetPath.empty())
			{
				fbxAssetPath = comp.instanceAssetPath;
			}
			else
			{
				// 논리 경로(Assets/...)만 저장/사용하고, 실제 파일 경로는 ResourceManager 가 해석합니다.
				fbxAssetPath = std::filesystem::path("Assets/Fbx") / (comp.meshAssetPath + ".fbxasset");
			}

			Alice::FbxInstanceAsset instance{};
			// gameMode에서는 Assets/... 가 Metas/Chunks 로 패킹되어 있으므로 Auto 로더를 사용합니다.
			std::filesystem::path fbxAssetLogical = fbxAssetPath;
			if (fbxAssetLogical.is_absolute())
			{
				// 절대경로가 저장된 경우: 파일명만으로 Assets/Fbx 아래에서 찾도록 정규화
				fbxAssetLogical = std::filesystem::path("Assets/Fbx") / fbxAssetLogical.filename();
			}

			if (!Alice::LoadFbxInstanceAssetAuto(pImpl->m_resourceManager, fbxAssetLogical, instance))
			{
				ALICE_LOG_WARN("Engine::EnsureSkinnedMeshesRegisteredForWorld: failed to load .fbxasset \"%s\" for meshKey=\"%s\"",
					fbxAssetLogical.string().c_str(),
					comp.meshAssetPath.c_str());
				continue;
			}

			FbxImportOptions opt{};
			FbxImporter importer(pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);

			// 배포(gameMode)에서는 source_fbx(논리 "Resource/...")를 Resolve하면
			// Cooked/Chunks/.../c0000.alice(청크 물리경로)로 바뀌어 FbxModel::Load(파일로드)가 실패합니다.
			// 따라서:
			// - editorMode: 파일 기반 로드를 위해 Resolve 사용
			// - gameMode  : 논리 경로 그대로 넘기고, ResourceManager가 Cooked/Chunks에서 로드/복호화하도록 함
			std::filesystem::path srcFbxPath =
				pImpl->m_editorMode ? pImpl->m_resourceManager.Resolve(instance.sourceFbx)
				: std::filesystem::path(instance.sourceFbx);
			FbxImportResult result = importer.Import(device, srcFbxPath, opt);

			ALICE_LOG_INFO("Engine::EnsureSkinnedMeshesRegisteredForWorld: re-import FBX \"%s\" -> meshKey=\"%s\" result.mesh=\"%s\"",
				srcFbxPath.string().c_str(),
				comp.meshAssetPath.c_str(),
				result.meshAssetPath.c_str());
		}
	}

	void Engine::TrimVideoMemory()
	{
		pImpl->m_renderDevice->TrimVideoMemory();
	}

	void Engine::UpdateIblForScene()
	{
		if (!pImpl->m_forwardRenderSystem) return;

		// 씬 파일에서 IBL 세트 정보를 읽어올 수 있도록 확장 가능하지만,
		// 현재는 기본적으로 "Bridge" IBL 세트를 사용합니다.
		// 향후 씬 파일에 IBL 세트 정보를 추가하면 여기서 읽어올 수 있습니다.
		pImpl->m_forwardRenderSystem->SetIblSet();
	}

	bool Engine::CreateMainWindow(int nCmdShow)
	{
		// 1) 윈도우 클래스 등록
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(WNDCLASSEXW);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = &Engine::WindowProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = pImpl->m_hInstance;
		// 엔진 전용 아이콘을 로드합니다. (실패하면 기본 아이콘을 사용)
		const std::filesystem::path iconAbs = pImpl->m_resourceManager.Resolve("Resource/Icon/Alice.ico");
		HICON hIconBig = static_cast<HICON>(LoadImageW(
			nullptr,
			iconAbs.wstring().c_str(),
			IMAGE_ICON,
			32,
			32,
			LR_LOADFROMFILE));
		if (!hIconBig) hIconBig = LoadIcon(nullptr, IDI_APPLICATION);
		HICON hIconSmall = static_cast<HICON>(LoadImageW(
			nullptr,
			iconAbs.wstring().c_str(),
			IMAGE_ICON,
			16,
			16,
			LR_LOADFROMFILE));
		if (!hIconSmall) hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

		wc.hIcon = hIconBig;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = kWindowClassName;
		wc.hIconSm = hIconSmall;

		if (!RegisterClassExW(&wc)) return false;

		// 2) 윈도우 크기를 클라이언트 기준으로 맞추기 위해 조정
		RECT windowRect = { 0, 0, static_cast<LONG>(pImpl->m_width), static_cast<LONG>(pImpl->m_height) };
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		const int windowWidth = windowRect.right - windowRect.left;
		const int windowHeight = windowRect.bottom - windowRect.top;

		// 3) 윈도우 생성 (this 포인터를 lpParam으로 전달)
		pImpl->m_hWnd = CreateWindowExW(
			0,
			kWindowClassName,
			L"AliceRenderer",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			pImpl->m_hInstance,
			this
		);

		if (!pImpl->m_hWnd) return false;

		ShowWindow(pImpl->m_hWnd, nCmdShow);
		UpdateWindow(pImpl->m_hWnd);

		return true;
	}

	void Engine::OnResize(std::uint32_t width, std::uint32_t height)
	{
		pImpl->m_width = width;
		pImpl->m_height = height;

		if (pImpl->m_renderDevice)
		{
			pImpl->m_renderDevice->Resize(width, height);

			const float aspect = (height != 0)
				? static_cast<float>(width) / static_cast<float>(height)
				: 1.0f;
			pImpl->m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
		}

		if (pImpl->m_forwardRenderSystem)
		{
			pImpl->m_forwardRenderSystem->Resize(width, height);
		}
	}

	LRESULT Engine::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_SIZE:
		{
			const auto newWidth = static_cast<std::uint32_t>(LOWORD(lParam));
			const auto newHeight = static_cast<std::uint32_t>(HIWORD(lParam));
			OnResize(newWidth, newHeight);
			return 0;
		}
		case WM_DESTROY:
			pImpl->m_isRunning = false;
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// ImGui가 먼저 Win32 메시지를 처리할 수 있도록 전달합니다.
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;

		// DirectXTK Keyboard / Mouse 에 Win32 메시지 전달 (GameApp::WndProc 패턴)
		switch (message)
		{
		case WM_ACTIVATEAPP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_INPUT:
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_MOUSEHOVER:
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			break;

		default:
			break;
		}

		// 1) WM_NCCREATE 단계에서 Engine 인스턴스 포인터를 HWND에 저장
		if (message == WM_NCCREATE)
		{
			auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
			auto engine = static_cast<Engine*>(createStruct->lpCreateParams);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(engine));
		}

		// 2) 저장된 Engine 포인터를 가져와서 멤버 함수로 위임
		auto engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		if (engine) return engine->HandleMessage(hWnd, message, wParam, lParam);

		// 3) 엔진 포인터가 없으면 기본 처리
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
}