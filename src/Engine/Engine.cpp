#include "Engine/Engine.h"

#include "Rendering/D3D11/D3D11RenderDevice.h"
#include "Rendering/DebugDrawSystem.h"
#include "Rendering/DebugDrawComponentSystem.h"
#include "Rendering/EffectSystem.h"
#include "Rendering/TrailEffectRenderSystem.h"

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
#include <cmath>       // std::fabsf
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
#include "Core/ScriptSystem.h"
#include "Core/Delegate.h"

#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/DeferredRenderSystem.h"

#include "AliceUI/UIRenderer.h"
#include "Rendering/ComputeEffectSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Editor/ViewportPicker.h"
#include "Editor/EditorCore.h"
#include "Game/SkinnedMeshSystem.h"
#include "Core/AdvancedAnimSystem.h"
#include "Game/SkinnedAnimationSystem.h"
#include "Game/SocketWorldUpdateSystem.h"
#include "Game/SocketAttachmentSystem.h"
#include "Game/WeaponTraceSystem.h"
#include "Game/CombatHitEvent.h"
#include "Game/CombatSystem.h"
#include "Game/AttackDriverSystem.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundManager.h"

#include "PhysX/Module/PhysicsModule.h" // 물리 모듈
#include "PhysX/PhysicsSystem.h" // 물리 시스템

//UI
#include "UI/UIWorldManager.h"

// 문자열 변환 / ImGui 래퍼
#include "Core/StringUtils.h"
#include "Core/ImGuiEx.h"
#include "Core/ScriptHotReload.h"
#include "Core/SceneFile.h"
#include "Core/ThreadSafety.h"
#include "Core/CameraSystem.h"
#include "Core/Logger.h"
#include "Game/FbxImporter.h"
#include "Game/FbxAsset.h"
#include <dxgi1_3.h>
#include <unordered_set>

#include "3DModel/FbxModel.h"

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
			PBR = 4,
			ToonPBR = 5,
			ToonPBREditable = 7
		};

		HINSTANCE m_hInstance = nullptr;
		HWND      m_hWnd = nullptr;

		std::uint32_t m_width = 1600;
		std::uint32_t m_height = 900;

		bool m_isRunning = false;            // 엔진 자체가 실행중인지 판단
		bool m_isPlaying = false;            // 재생 / 일시정지 상태 (에디터 모드에서만 사용)
		bool m_editorMode = true;             // true: 에디터, false: 게임 전용
		bool m_debugDraw = true;
		EntityId m_selectedEntity{ InvalidEntityId }; // 현재 선택된 엔티티 (하이러키)

		World          m_world;
		UIWorldManager m_uiWorld;
		UIRenderer     m_aliceUIRenderer;
		Camera         m_camera;
		InputSystem    m_inputSystem;
		GameTimer      m_timer;
		ResourceManager m_resourceManager;
		std::unique_ptr<SceneManager> m_sceneManager;

		//===============================		
		PhysicsModule m_physics; // 물리 모듈
		std::unique_ptr<PhysicsSystem> m_physicsSystem; // 물리 시스템 (ECS 브릿지)

		float m_physAccum = 0.0f;
		float m_physFixedDt = 1.0f / 60.0f;
		int   m_physMaxSubsteps = 4;

		// 물리 이벤트 큐 (한 프레임 안전하게 처리하기 위함)
		std::vector<PhysicsEvent> m_physicsEventQueue;
		std::vector<CombatHitEvent> m_combatHitQueue;

		// PVD (PhysX Visual Debugger) 설정
		bool m_pvdEnabled = false;
		std::string m_pvdHost = "127.0.0.1";
		int m_pvdPort = 5425;
		//===============================

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
		std::unique_ptr<DeferredRenderSystem> m_deferredRenderSystem;
		std::unique_ptr<class DebugDrawSystem> m_debugDrawSystem;
		std::unique_ptr<class DebugDrawSystem> m_gizmoDrawSystem;
		DebugDrawComponentSystem m_debugDrawComponentSystem;
		std::unique_ptr<class EffectSystem> m_effectSystem;
		std::unique_ptr<class TrailEffectRenderSystem> m_trailRenderSystem;
		std::unique_ptr<ComputeEffectSystem> m_computeEffectSystem;

		// 렌더링 모드 전환 (true: Forward, false: Deferred)
		bool m_useForwardRendering = false;

		// 렌더링 시스템 전환 지연 처리 (안전한 전환을 위해)
		bool m_pendingRenderSystemChange = false;
		bool m_pendingUseForwardRendering = true;

		CameraSystem m_cameraSystem;

		// Skinned FBX 메시 렌더링용 레지스트리/시스템
		SkinnedMeshRegistry m_skinnedMeshRegistry;
		SkinnedMeshSystem   m_skinnedMeshSystem{ m_skinnedMeshRegistry };
		AdvancedAnimSystem  m_advancedAnimSystem{ m_skinnedMeshRegistry };
		SkinnedAnimationSystem m_skinnedAnimSystem{ m_skinnedMeshRegistry };
		SocketWorldUpdateSystem m_socketWorldUpdateSystem{ m_skinnedMeshRegistry };
		SocketAttachmentSystem m_socketAttachmentSystem;
		WeaponTraceSystem m_weaponTraceSystem;
		CombatSystem m_combatSystem;
		AttackDriverSystem m_attackDriverSystem;
		AudioSystem m_audioSystem;
		std::vector<SkinnedDrawCommand> m_skinnedDrawCommands;

		bool m_animUpdatedThisFrame = false;
	};
	namespace
	{
		// 윈도우 클래스 이름은 전역 상수로 관리합니다.
		constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";

		// PVD 설정 저장/로드 함수
		std::filesystem::path GetEngineSettingsPath(const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;
			// 에디터 모드: 프로젝트 루트 / EngineSettings.json
			// 게임 모드: 실행 파일 위치 / EngineSettings.json
			fs::path cfg = exeDir / "EngineSettings.json";
			if (!fs::exists(cfg))
			{
				// 빌드 경로에도 확인
				cfg = exeDir.parent_path().parent_path().parent_path() / "EngineSettings.json";
			}
			return cfg;
		}

		void LoadPvdSettings(const std::filesystem::path& exeDir, bool& enabled, std::string& host, int& port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			if (!fs::exists(cfg))
			{
				// 파일이 없으면 기본값 유지
				return;
			}

			std::ifstream ifs(cfg);
			if (!ifs.is_open()) return;

			nlohmann::json j;
			try
			{
				ifs >> j;
			}
			catch (...)
			{
				ALICE_LOG_WARN("EngineSettings.json parse error. Using defaults.");
				return;
			}

			if (j.contains("pvd"))
			{
				const auto& pvd = j["pvd"];
				if (pvd.contains("enabled") && pvd["enabled"].is_boolean())
					enabled = pvd["enabled"].get<bool>();
				if (pvd.contains("host") && pvd["host"].is_string())
					host = pvd["host"].get<std::string>();
				if (pvd.contains("port") && pvd["port"].is_number_integer())
					port = pvd["port"].get<int>();
			}
		}

		void SavePvdSettings(const std::filesystem::path& exeDir, bool enabled, const std::string& host, int port)
		{
			namespace fs = std::filesystem;
			fs::path cfg = GetEngineSettingsPath(exeDir);

			// 디렉토리 생성 (없으면)
			fs::create_directories(cfg.parent_path());

			nlohmann::json j;

			// 기존 파일이 있으면 읽어서 병합
			if (fs::exists(cfg))
			{
				std::ifstream ifs(cfg);
				if (ifs.is_open())
				{
					try
					{
						ifs >> j;
					}
					catch (...)
					{
						// 파싱 실패해도 계속 진행 (새 파일로 덮어쓰기)
					}
				}
			}

			// PVD 설정 업데이트
			j["pvd"] = nlohmann::json::object();
			j["pvd"]["enabled"] = enabled;
			j["pvd"]["host"] = host;
			j["pvd"]["port"] = port;

			// 저장
			std::ofstream ofs(cfg);
			if (!ofs.is_open())
			{
				ALICE_LOG_ERRORF("Failed to save EngineSettings.json");
				return;
			}

			ofs << j.dump(4); // 들여쓰기 4칸으로 포맷
			ALICE_LOG_INFO("PVD settings saved to EngineSettings.json");
		}

		// BuildSettings.txt 에서 시작 씬(.scene 파일)을 읽어와 World 에 로드합니다.
		// - scenes 섹션은 "index: path" 형식으로 저장되어 있다고 가정합니다.
		bool LoadStartupSceneFromBuildSettings(World& world, const ResourceManager& resources, const std::filesystem::path& exeDir, UIWorldManager* uiWorldManager = nullptr)
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
		ALICE_LOG_INFO("Loading Startup Scene: %s (uiWorldManager=%p)", logicalScene.string().c_str(), uiWorldManager);

		// gameMode에서는 Assets/... 가 Metas/Chunks 로 패킹되어 있으므로 LoadAuto를 사용합니다.
		if (!SceneFile::LoadAuto(world, resources, logicalScene, uiWorldManager))
		{
			ALICE_LOG_ERRORF("Scene Load Failed: %s", logicalScene.string().c_str());
			return false;
		}

		ALICE_LOG_INFO("Startup Scene loaded successfully: %s", logicalScene.string().c_str());
		return true;
		}
	}

	namespace
	{
		inline Alice::EntityId DecodeEntityId(void* p)
		{
			return static_cast<Alice::EntityId>(reinterpret_cast<std::uintptr_t>(p));
		}

		// 축 맞는지 확인해야함, 아니면 조율해줘야함
		inline DirectX::XMFLOAT3 QuatToEulerXYZ(const Quat& qIn)
		{
			// normalize
			Quat q = qIn;
			const float len2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
			if (len2 > 0.0f)
			{
				const float inv = 1.0f / std::sqrt(len2);
				q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
			}

			// Tait–Bryan angles (X=pitch, Y=yaw, Z=roll) 근사
			const float sinp = 2.0f * (q.w * q.x + q.y * q.z);
			const float cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
			const float pitch = std::atan2(sinp, cosp);

			float siny = 2.0f * (q.w * q.y - q.z * q.x);
			siny = std::clamp(siny, -1.0f, 1.0f);
			const float yaw = std::asin(siny);

			const float sinr = 2.0f * (q.w * q.z + q.x * q.y);
			const float cosr = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
			const float roll = std::atan2(sinr, cosr);

			return { pitch, yaw, roll };
		}
	}



	Engine::Engine(bool editorMode) : pImpl(std::make_unique<Impl>())
	{
		pImpl->m_editorMode = editorMode;
		pImpl->m_scriptSystem.SetEditorMode(editorMode);
	}

	Engine::~Engine()
	{
		Shutdown();
	}

	void Engine::Shutdown()
	{
		static bool s_isShutdown = false;
		if (s_isShutdown) return;
		s_isShutdown = true;

		// PVD 설정 저장 (엔진 종료 시)
		wchar_t pathBuf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		const std::filesystem::path exeDir = std::filesystem::path(pathBuf).parent_path();
		SavePvdSettings(exeDir, pImpl->m_pvdEnabled, pImpl->m_pvdHost, pImpl->m_pvdPort);

		// 1) 게임 루프/시스템이 물리 월드 참조 못 하게 먼저 끊기
		if (pImpl->m_physicsSystem)
		{
			pImpl->m_physicsSystem->SetPhysicsWorld(nullptr);
			pImpl->m_physicsSystem.reset();
		}

		// 2) World가 잡고 있는 physics world(shared_ptr) 해제
		pImpl->m_world.SetPhysicsWorld(nullptr);

		// 3) Editor/기타가 물리를 참조하면 여기서 먼저 정리
		pImpl->m_editorCore.Shutdown();
		pImpl->m_aliceUIRenderer.Shutdown();

		// 4) 마지막에 PhysX 컨텍스트 종료
		pImpl->m_physics.ShutdownContext();
		Sound::Shutdown();
	}

	bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
	{
		pImpl->m_hInstance = hInstance;

		InitializeMainThreadAndRegistry();

		const std::filesystem::path exeDir = InitializeResolveExeDir();

		if (!InitializeConfigureResourceManagers(exeDir)) return false;
		if (!InitializeValidateGameDataIfNeeded()) return false;

		InitializeLoadPvdSettings(exeDir);
		if (!InitializePhysicsContext()) return false;

		if (!InitializeWindowAndInput(nCmdShow)) return false;
		if (!InitializeRenderDevice()) return false;

		if (!InitializeEditorCoreIfNeeded()) return false;

		InitializeAudio();

		if (!InitializeRenderSystems()) return false;
		if (!InitializeUI()) return false;
		if (!InitializeComputeEffectSystem()) return false;

		InitializeCameraAndScriptHotReload();

		if (!InitializeScene(exeDir)) return false;
		if (!InitializePhysicsSystemAndWorldCallbacks()) return false;

		InitializePostLoadBindings();
		InitializeEnsureUIResourcesAfterSceneLoad();

		ALICE_LOG_INFO("Engine::Initialize: Success (Entities: %zu)",
			pImpl->m_world.GetComponents<TransformComponent>().size());

		return true;
	}

	// =========================
	// Initialize helpers
	void Engine::InitializeMainThreadAndRegistry()
	{
		ThreadSafety::SetMainThreadId(std::this_thread::get_id());
		LinkComponentRegistry();
		ALICE_LOG_INFO("Engine::Initialize: Begin (EditorMode=%d)", pImpl->m_editorMode);
	}

	std::filesystem::path Engine::InitializeResolveExeDir()
	{
		wchar_t pathBuf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		return std::filesystem::path(pathBuf).parent_path();
	}

	bool Engine::InitializeConfigureResourceManagers(const std::filesystem::path& exeDir)
	{
		pImpl->m_resourceManager.Configure(!pImpl->m_editorMode, exeDir);

		if (pImpl->m_editorMode)
			ResourceManager::Get().Configure(false, exeDir);

		return true;
	}

	bool Engine::InitializeValidateGameDataIfNeeded()
	{
		if (pImpl->m_editorMode) return true;

		if (!pImpl->m_resourceManager.ValidateGameData())
		{
			MessageBoxW(nullptr,
				L"Critical Error: Game Data is corrupted or missing.\nPlease reinstall the game.",
				L"Integrity Check Failed",
				MB_OK | MB_ICONERROR);
			ALICE_LOG_ERRORF("[Engine] Initialize FAILED: Data integrity check failed.");
			return false;
		}
		return true;
	}

	void Engine::InitializeLoadPvdSettings(const std::filesystem::path& exeDir)
	{
		LoadPvdSettings(exeDir, pImpl->m_pvdEnabled, pImpl->m_pvdHost, pImpl->m_pvdPort);
		if (pImpl->m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD settings loaded from EngineSettings.json: %s:%d",
				pImpl->m_pvdHost.c_str(), pImpl->m_pvdPort);
		}
	}

	bool Engine::InitializePhysicsContext()
	{
		PhysicsModule::ContextInitDesc ctx{};
		ctx.enablePvd = pImpl->m_pvdEnabled;
		ctx.pvdHost = pImpl->m_pvdHost.c_str();
		ctx.pvdPort = pImpl->m_pvdPort;
		ctx.pvdTimeoutMs = 1000;

		if (!pImpl->m_physics.InitializeContext(ctx))
		{
			const std::string& error = pImpl->m_physics.GetLastError();
			ALICE_LOG_ERRORF("PhysicsModule::InitializeContext failed: %s", error.c_str());
			return false;
		}

		if (pImpl->m_pvdEnabled)
		{
			ALICE_LOG_INFO("PVD enabled: %s:%d (connection may fail silently if PVD server is not running)",
				pImpl->m_pvdHost.c_str(), pImpl->m_pvdPort);
		}

		return true;
	}

	bool Engine::InitializeWindowAndInput(int nCmdShow)
	{
		if (!CreateMainWindow(nCmdShow)) return false;
		pImpl->m_inputSystem.Initialize(pImpl->m_hWnd);
		return true;
	}

	bool Engine::InitializeRenderDevice()
	{
		pImpl->m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!pImpl->m_renderDevice->Initialize(pImpl->m_hWnd, pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: RenderDevice failed.");
			return false;
		}
		return true;
	}

	bool Engine::InitializeEditorCoreIfNeeded()
	{
		if (!pImpl->m_editorMode) return true;

		pImpl->m_editorCore.SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);
		pImpl->m_editorCore.SetInputSystem(&pImpl->m_inputSystem);

		if (!pImpl->m_editorCore.Initialize(pImpl->m_hWnd, *pImpl->m_renderDevice))
			return false;

		return true;
	}

	void Engine::InitializeAudio()
	{
		pImpl->m_audioSystem.SetResourceManager(&pImpl->m_resourceManager);
		Sound::Initialize();
	}

	bool Engine::InitializeRenderSystems()
	{
		pImpl->m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*pImpl->m_renderDevice);
		pImpl->m_forwardRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		pImpl->m_forwardRenderSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		pImpl->m_attackDriverSystem.SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		if (!pImpl->m_forwardRenderSystem->Initialize(pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("pImpl->m_forwardRenderSystem->Initialize: fail...");
			return false;
		}

		pImpl->m_deferredRenderSystem = std::make_unique<DeferredRenderSystem>(*pImpl->m_renderDevice);
		pImpl->m_deferredRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		pImpl->m_deferredRenderSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		if (!pImpl->m_deferredRenderSystem->Initialize(pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("pImpl->m_deferredRenderSystem->Initialize: fail...");
			return false;
		}

		pImpl->m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_debugDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("pImpl->m_debugDrawSystem->Initialize(): fail...");
			return false;
		}

		pImpl->m_gizmoDrawSystem = std::make_unique<DebugDrawSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_gizmoDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("pImpl->m_gizmoDrawSystem->Initialize(): fail...");
			return false;
		}

		pImpl->m_effectSystem = std::make_unique<EffectSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_effectSystem->Initialize())
		{
			ALICE_LOG_ERRORF("pImpl->m_effectSystem->Initialize(): fail...");
			return false;
		}

		pImpl->m_trailRenderSystem = std::make_unique<TrailEffectRenderSystem>(*pImpl->m_renderDevice);
		pImpl->m_trailRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		if (!pImpl->m_trailRenderSystem->Initialize()) return false;

		if (pImpl->m_deferredRenderSystem && pImpl->m_trailRenderSystem)
			pImpl->m_deferredRenderSystem->SetSwordRenderSystem(pImpl->m_trailRenderSystem.get());

		return true;
	}

	bool Engine::InitializeUI()
	{
		auto* device = pImpl->m_renderDevice->GetDevice();
		auto* context = pImpl->m_renderDevice->GetImmediateContext();
		if (!device || !context)
		{
			ALICE_LOG_ERRORF("[Debug] Device or Context is NULL inside UI Block!");
			return false;
		}

		pImpl->m_uiWorld.Initalize(device, context, pImpl->m_width, pImpl->m_height, pImpl->m_inputSystem);

		if (!pImpl->m_aliceUIRenderer.Initialize(device, context, &pImpl->m_resourceManager))
			ALICE_LOG_ERRORF("[AliceUI] UIRenderer Initialize failed.");

		if (pImpl->m_forwardRenderSystem)  pImpl->m_forwardRenderSystem->SetUIRenderer(&pImpl->m_aliceUIRenderer);
		if (pImpl->m_deferredRenderSystem) pImpl->m_deferredRenderSystem->SetUIRenderer(&pImpl->m_aliceUIRenderer);
		if (pImpl->m_editorMode)          pImpl->m_editorCore.SetAliceUIRenderer(&pImpl->m_aliceUIRenderer);

		return true;
	}

	bool Engine::InitializeComputeEffectSystem()
	{
		pImpl->m_computeEffectSystem = std::make_unique<ComputeEffectSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_computeEffectSystem->Initialize(pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("[Debug] ComputeEffectSystem Init Failed!");
			return false;
		}
		return true;
	}

	void Engine::InitializeCameraAndScriptHotReload()
	{
		pImpl->m_cameraPosition = { 0.0f, 2.0f, -5.0f };
		pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		pImpl->m_camera.SetPerspective(DirectX::XM_PIDIV4,
			static_cast<float>(pImpl->m_width) / pImpl->m_height, 0.1f, 5000.0f);

		ScriptHotReload_Load();
	}

	bool Engine::InitializeScene(const std::filesystem::path& exeDir)
	{
		pImpl->m_resourceManager.Clear();
		pImpl->m_sceneManager = std::make_unique<SceneManager>(pImpl->m_world, pImpl->m_resourceManager);

		bool isSceneLoaded = false;

		if (!pImpl->m_editorMode)
		{
			isSceneLoaded = LoadStartupSceneFromBuildSettings(
				pImpl->m_world, pImpl->m_resourceManager, exeDir, &pImpl->m_uiWorld);
		}

		if (!isSceneLoaded)
		{
			pImpl->m_sceneManager->SwitchToImmediate("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: Loaded SampleScene (Fallback or Editor).");
		}

		return true;
	}

	bool Engine::InitializePhysicsSystemAndWorldCallbacks()
	{
		pImpl->m_physicsSystem = std::make_unique<PhysicsSystem>(pImpl->m_world);
		pImpl->m_physicsSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		pImpl->m_world.SetOnBeforeClearCallback([this]() {
			if (pImpl->m_physicsSystem)
			{
				if (auto pwShared = pImpl->m_world.GetPhysicsWorldShared())
					pwShared->Flush();

				pImpl->m_physicsSystem->SetPhysicsWorld(nullptr);
			}

			pImpl->m_physAccum = 0.0f;
			pImpl->m_physicsEventQueue.clear();
		});

		RefreshPhysicsForCurrentWorld();
		return true;
	}

	void Engine::InitializePostLoadBindings()
	{
		EnsureSkinnedMeshesRegisteredForWorld();

		pImpl->m_scriptSystem.SetServices(&pImpl->m_inputSystem, pImpl->m_sceneManager.get(),
			&pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);

		pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::EnsureSkinnedMeshesRegisteredForWorld);
		pImpl->m_scriptSystem.onTrimVideoMemory.BindObject(this, &Engine::TrimVideoMemory);
		pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::RefreshPhysicsForCurrentWorld);
	}

	void Engine::InitializeEnsureUIResourcesAfterSceneLoad()
	{
		pImpl->m_uiWorld.EnsureAllUIResources();
	}

	int Engine::Run()
	{
		// 타이머 초기화
		pImpl->m_isRunning = true;
		pImpl->m_timer.Reset();
		pImpl->m_timer.Start();

		MSG msg = {};

		// 기본 게임 루프
		while (pImpl->m_isRunning)
		{
			// 윈도우 메시지는 바로바로 처리함
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

		// 종료할때 정리
		pImpl->m_scriptSystem.OnApplicationQuit(pImpl->m_world);
		return static_cast<int>(msg.wParam);
	}

	void Engine::Update()
	{
		float dt = 0.0f;
		UpdateTimerAndInput(dt);

		const bool updateFromScene = UpdateShouldUpdateFromScene();
		bool sceneChangedThisFrame = false;

		if (updateFromScene)
		{
			UpdateSceneAndScript(dt);
			sceneChangedThisFrame = UpdateCommitPendingSceneChanges(dt);

			if (!sceneChangedThisFrame)
			{
				UpdateAttackDriver();

				UpdateEnsurePhysicsWorldIfNeeded();
				UpdatePhysicsBridge(dt);
				UpdatePhysicsSim(dt);

				UpdateAnimationAndSockets(dt);
				UpdateCombat(dt);

				UpdateCameraSystems(dt);
				UpdateSyncPrimaryCameraFromWorld();
			}
		}
		else
		{
			UpdateEditorFreeCam(dt);
		}

		UpdateApplyFinalCameraLookAt();
		UpdateUI(dt);
	}

	// =========================
	// Update helpers
	void Engine::UpdateTimerAndInput(float& outDt)
	{
		pImpl->m_timer.Tick();
		outDt = pImpl->m_timer.DeltaTime();
		pImpl->m_inputSystem.Update(outDt);
		pImpl->m_animUpdatedThisFrame = false;
	}

	bool Engine::UpdateShouldUpdateFromScene() const
	{
		return (!pImpl->m_editorMode || pImpl->m_isPlaying);
	}

	void Engine::UpdateSceneAndScript(float dt)
	{
		if (pImpl->m_sceneManager) pImpl->m_sceneManager->Update(dt);
		pImpl->m_scriptSystem.Tick(pImpl->m_world, dt);
	}

	bool Engine::UpdateCommitPendingSceneChanges(float /*dt*/)
	{
		bool sceneChangedThisFrame = false;

		if (pImpl->m_scriptSystem.HasPendingSceneRequests() ||
			(pImpl->m_sceneManager && pImpl->m_sceneManager->HasPendingSceneChange()))
		{
			if (pImpl->m_physicsSystem)
				pImpl->m_physicsSystem->SetPhysicsWorld(nullptr);

			if (auto pwShared = pImpl->m_world.GetPhysicsWorldShared())
				pwShared->Flush();

			pImpl->m_physAccum = 0.0f;
			pImpl->m_physicsEventQueue.clear();

			if (pImpl->m_scriptSystem.HasPendingSceneRequests())
				pImpl->m_scriptSystem.CommitSceneRequests(pImpl->m_world, &pImpl->m_uiWorld);

			if (pImpl->m_sceneManager && pImpl->m_sceneManager->HasPendingSceneChange())
				pImpl->m_sceneManager->CommitPendingSceneChange(pImpl->m_world, &pImpl->m_uiWorld);

			sceneChangedThisFrame = true;
		}

		return sceneChangedThisFrame;
	}

	void Engine::UpdateAttackDriver()
	{
		pImpl->m_attackDriverSystem.Update(pImpl->m_world);
	}

	void Engine::UpdateEnsurePhysicsWorldIfNeeded()
	{
		if (pImpl->m_physicsSystem && !pImpl->m_world.GetPhysicsWorld())
		{
			const auto& settingsMap = pImpl->m_world.GetComponents<Phy_SettingsComponent>();
			if (!settingsMap.empty())
			{
				const auto& settings = settingsMap.begin()->second;
				if (settings.enablePhysics)
					RefreshPhysicsForCurrentWorld();
			}
		}
	}

	void Engine::UpdatePhysicsBridge(float dt)
	{
		if (pImpl->m_physicsSystem)
			pImpl->m_physicsSystem->Update(dt);
	}

	void Engine::UpdatePhysicsSim(float dt)
	{
		TickPhysics(dt);
	}

	void Engine::UpdateAnimationAndSockets(float dt)
	{
		pImpl->m_advancedAnimSystem.Update(pImpl->m_world, static_cast<double>(dt));
		pImpl->m_skinnedAnimSystem.Update(pImpl->m_world, static_cast<double>(dt));
		pImpl->m_socketWorldUpdateSystem.Update(pImpl->m_world);
		pImpl->m_socketAttachmentSystem.Update(pImpl->m_world);
		pImpl->m_animUpdatedThisFrame = true;

		pImpl->m_weaponTraceSystem.Update(pImpl->m_world, dt, &pImpl->m_combatHitQueue);
	}

	void Engine::UpdateCombat(float dt)
	{
		ProcessPhysicsEvents();
		ProcessCombatHits();
		pImpl->m_combatSystem.Update(pImpl->m_world, dt);
	}

	void Engine::UpdateCameraSystems(float dt)
	{
		pImpl->m_cameraSystem.Update(pImpl->m_world, pImpl->m_inputSystem, dt);
	}

	void Engine::UpdateSyncPrimaryCameraFromWorld()
	{
		EntityId camId = InvalidEntityId;
		for (const auto& [id, cam] : pImpl->m_world.GetComponents<CameraComponent>())
		{
			if (cam.GetPrimary()) { camId = id; break; }
			if (camId == InvalidEntityId) camId = id;
		}

		if (camId == InvalidEntityId) return;

		auto* camComp = pImpl->m_world.GetComponent<CameraComponent>(camId);
		if (!camComp) return;

		const Camera& sourceCamera = camComp->GetCamera();

		const float defaultAspect = static_cast<float>(pImpl->m_width) / pImpl->m_height;
		const float aspect = (camComp->useAspectOverride && camComp->aspectOverride > 0.0f)
			? camComp->aspectOverride : defaultAspect;

		pImpl->m_camera.SetPerspective(
			sourceCamera.GetFovYRadians(), aspect,
			sourceCamera.GetNearPlane(), sourceCamera.GetFarPlane());

		pImpl->m_camera.SetPosition(sourceCamera.GetPosition());
		pImpl->m_camera.SetRotation(sourceCamera.GetRotationQuat());
		pImpl->m_camera.SetScale(sourceCamera.GetScale());

		pImpl->m_cameraPosition = sourceCamera.GetPosition();
		const DirectX::XMFLOAT3 rot = sourceCamera.GetRotation();
		pImpl->m_cameraYawRadians = rot.y;
		pImpl->m_cameraPitchRadians = rot.x;
	}

	void Engine::UpdateEditorFreeCam(float dt)
	{
		using namespace DirectX;

		if (!pImpl->m_inputSystem.IsRightButtonDown())
			return;

		XMVECTOR moveDir = XMVectorZero();
		auto& input = pImpl->m_inputSystem;

		if (input.IsKeyDown(Keyboard::W)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, 1, 0));
		if (input.IsKeyDown(Keyboard::S)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 0, -1, 0));
		if (input.IsKeyDown(Keyboard::D)) moveDir = XMVectorAdd(moveDir, XMVectorSet(1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::A)) moveDir = XMVectorAdd(moveDir, XMVectorSet(-1, 0, 0, 0));
		if (input.IsKeyDown(Keyboard::E)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, 1, 0, 0));
		if (input.IsKeyDown(Keyboard::Q)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0, -1, 0, 0));

		if (!XMVector3Equal(moveDir, XMVectorZero()))
		{
			const XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0);
			const XMVECTOR worldDir = XMVector3Normalize(XMVector3TransformNormal(moveDir, rotMat));
			const XMVECTOR currentPos = XMLoadFloat3(&pImpl->m_cameraPosition);

			XMStoreFloat3(&pImpl->m_cameraPosition,
				XMVectorAdd(currentPos, XMVectorScale(worldDir, pImpl->m_cameraMoveSpeed * dt)));
		}

		const POINT mouseDelta = input.GetMouseDelta();
		pImpl->m_cameraYawRadians += mouseDelta.x * pImpl->m_cameraMouseSensitivity;
		pImpl->m_cameraPitchRadians += mouseDelta.y * pImpl->m_cameraMouseSensitivity;
	}

	void Engine::UpdateApplyFinalCameraLookAt()
	{
		using namespace DirectX;

		const float pitchLimit = XMConvertToRadians(89.0f);
		pImpl->m_cameraPitchRadians = std::clamp(pImpl->m_cameraPitchRadians, -pitchLimit, pitchLimit);

		const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0);
		const XMVECTOR camForward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), camRot);
		const XMVECTOR camPos = XMLoadFloat3(&pImpl->m_cameraPosition);

		XMFLOAT3 targetPos;
		XMStoreFloat3(&targetPos, XMVectorAdd(camPos, camForward));

		pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, targetPos, XMFLOAT3(0, 1, 0));
	}

	void Engine::UpdateUI(float /*dt*/)
	{
		pImpl->m_uiWorld.Update(pImpl->m_width, pImpl->m_height);
		pImpl->m_aliceUIRenderer.Update(
			pImpl->m_world, pImpl->m_inputSystem, pImpl->m_camera,
			static_cast<float>(pImpl->m_width), static_cast<float>(pImpl->m_height));
	}

	//=========================================================
	// 물리 시스템
	void Alice::Engine::ClearWorldAndPhysics()
	{
		// 월드와 물리 시스템을 함께 정리하는 안전한 진입점
		// World::Clear()가 호출되면 OnBeforeClear 콜백이 자동으로 PhysicsSystem을 정리하므로,
		// 이 함수는 단순히 World::Clear()를 호출하면 됨
		pImpl->m_world.Clear();

		// World::Clear()에서 이미 물리 월드를 reset했지만, 명시적으로도 해제
		pImpl->m_world.SetPhysicsWorld(nullptr);
	}

	void Alice::Engine::RefreshPhysicsForCurrentWorld()
	{
		ThreadSafety::AssertMainThread();
		// 현재 씬의 물리 월드 설정을 갱신
		// Phy_SettingsComponent를 기반으로 물리 월드를 생성/재사용
		// settings가 없으면, 물리월드 제거(비물리 씬)
		const auto& settingsMap = pImpl->m_world.GetComponents<Phy_SettingsComponent>();

		if (settingsMap.empty())
		{
			// 물리월드 끄기 직전
			if (auto pwShared = pImpl->m_world.GetPhysicsWorldShared())
			{
				pwShared->Flush();            // pending add/remove/release 처리
			}

			pImpl->m_physAccum = 0.0f;
			pImpl->m_physicsEventQueue.clear();

			// 안전한 파괴 순서: PhysicsSystem 먼저 정리 (액터 Destroy) → 월드 해제
			if (pImpl->m_physicsSystem) { pImpl->m_physicsSystem->SetPhysicsWorld(nullptr); }
			pImpl->m_world.SetPhysicsWorld(nullptr);
			return;
		}

		// 여러 settings 컴포넌트가 있을 경우 첫 번째 것만 사용 (명확하게)
		const auto& settings = settingsMap.begin()->second;
		if (!settings.enablePhysics)
		{
			// 물리월드 끄기 직전
			if (auto pwShared = pImpl->m_world.GetPhysicsWorldShared())
			{
				pwShared->Flush();            // pending add/remove/release 처리
			}

			pImpl->m_physAccum = 0.0f;
			pImpl->m_physicsEventQueue.clear();

			// 안전한 파괴 순서: PhysicsSystem 먼저 정리 (액터 Destroy) → 월드 해제
			if (pImpl->m_physicsSystem) { pImpl->m_physicsSystem->SetPhysicsWorld(nullptr); }
			pImpl->m_world.SetPhysicsWorld(nullptr);
			return;
		}

		IPhysicsWorld* existingWorld = pImpl->m_world.GetPhysicsWorld();
		Vec3 newGravity = Vec3(settings.gravity.x, settings.gravity.y, settings.gravity.z);

		// 기존 월드가 있고 설정이 변경되지 않았다면 그대로 사용
		if (existingWorld)
		{
			Vec3 currentGravity = existingWorld->GetGravity();
			// 중력이 변경되었으면 업데이트
			if (currentGravity.x != newGravity.x || currentGravity.y != newGravity.y || currentGravity.z != newGravity.z)
			{
				existingWorld->SetGravity(newGravity);
				ALICE_LOG_INFO("PhysicsWorld gravity updated: (%.2f, %.2f, %.2f)", newGravity.x, newGravity.y, newGravity.z);
			}

			// fixedDt/maxSubsteps는 매 프레임 업데이트 (에디터에서 변경 가능)
			pImpl->m_physFixedDt = settings.fixedDt;
			pImpl->m_physMaxSubsteps = settings.maxSubsteps;
			// accum은 유지 (프레임 드롭 방지)

			// PhysicsSystem에 물리 월드 설정 (이미 같은 월드면 재설정 생략 - 불필요한 전체 재초기화 방지)
			if (pImpl->m_physicsSystem && pImpl->m_physicsSystem->GetPhysicsWorld() != existingWorld)
			{
				pImpl->m_physicsSystem->SetPhysicsWorld(existingWorld);
			}

			// Phy_SettingsComponent의 layerCollideMatrix와 layerQueryMatrix 변경은
			// 런타임에 적용할 수 없으므로 (FilterShader는 씬 생성 시 설정됨),
			// 변경 시 물리 월드를 재생성해야 합니다.
			// 하지만 매 프레임 체크하는 것은 비효율적이므로, 에디터에서 변경 시 씬 재로드를 권장합니다.

			return;
		}

		// 새 월드 생성
		PhysicsModule::WorldDesc desc{};
		desc.gravity = newGravity;
		// 필요하면 여기서 CCD / 이벤트 옵션들 설정

		std::shared_ptr<IPhysicsWorld> world = pImpl->m_physics.CreateWorld(desc);
		if (!world)
		{
			ALICE_LOG_ERRORF("Failed to create PhysicsWorld");
			return;
		}

		ALICE_LOG_INFO("PhysicsWorld created: %p", world.get());
		pImpl->m_world.SetPhysicsWorld(world);

		// PhysicsSystem에 물리 월드 설정
		if (pImpl->m_physicsSystem)
		{
			pImpl->m_physicsSystem->SetPhysicsWorld(world.get());
		}

		// settings 반영
		pImpl->m_physFixedDt = settings.fixedDt;
		pImpl->m_physMaxSubsteps = settings.maxSubsteps;
		pImpl->m_physAccum = 0.0f;
	}

	void Engine::TickPhysics(float dt)
	{
		// 물리 시뮬레이션 수행 (고정 시간 스텝)
		// Physics → Game 동기화 및 이벤트 수집
		auto pwShared = pImpl->m_world.GetPhysicsWorldShared(); // 로컬로 수명을 고정시킴
		IPhysicsWorld* pw = pwShared.get();
		if (!pw) {
			if (pImpl->m_physicsSystem && pImpl->m_physicsSystem->GetPhysicsWorld() != nullptr)
				pImpl->m_physicsSystem->SetPhysicsWorld(nullptr);
			return;
		}

		dt = std::min(dt, 0.25f);

		pImpl->m_physAccum += dt;
		int steps = 0;

		std::vector<ActiveTransform> moved;

		std::vector<PhysicsEvent> events;

		while (pImpl->m_physAccum >= pImpl->m_physFixedDt && steps < pImpl->m_physMaxSubsteps)
		{
			pw->Step(pImpl->m_physFixedDt);

			moved.clear();
			pw->DrainActiveTransforms(moved);

			// Physics → Game 동기화 (PhysicsSystem을 통해 처리)
			if (pImpl->m_physicsSystem)
			{
				for (const auto& at : moved)
				{
					pImpl->m_physicsSystem->SyncPhysicsToGame(at);
				}
			}
			else
			{
				// Fallback: PhysicsSystem이 없을 때 직접 동기화
				for (const auto& at : moved)
				{
					if (!at.userData) continue;

					// worldEpoch 검증 포함하여 EntityId 추출 (이전 씬의 userData는 무시)
					const EntityId id = pImpl->m_world.ExtractEntityIdFromUserData(at.userData);
					if (id == InvalidEntityId) continue;

					auto* tr = pImpl->m_world.GetComponent<TransformComponent>(id);
					if (!tr) continue;

					tr->position = { at.position.x, at.position.y, at.position.z };
					// 회전도 동기화 (static 메서드이므로 PhysicsSystem 인스턴스 없이도 호출 가능)
					DirectX::XMFLOAT3 euler = PhysicsSystem::ToEulerRadians(at.rotation);
					tr->rotation = euler;
				}
			}

			// 이벤트 드레인 및 큐에 누적 (한 프레임 안전하게 처리)
			events.clear();
			pw->DrainEvents(events);

			// 이벤트를 큐에 추가 (다음 프레임 게임 로직에서 처리)
			pImpl->m_physicsEventQueue.insert(
				pImpl->m_physicsEventQueue.end(),
				events.begin(),
				events.end()
			);

			pImpl->m_physAccum -= pImpl->m_physFixedDt;
			++steps;
		}

		if (steps == pImpl->m_physMaxSubsteps)
			pImpl->m_physAccum = 0.0f;

		// 로그로 떨어지는지 확인 (1초에 1번만)
	}

	void Engine::ProcessPhysicsEvents()
	{
		// 물리 이벤트 큐 처리 (한 프레임 안전하게 처리)
		// 물리 시뮬레이션에서 발생한 충돌/트리거 이벤트를 게임 로직으로 전달
		for (const auto& e : pImpl->m_physicsEventQueue)
		{
			if (!e.userDataA || !e.userDataB) continue;

			// worldEpoch 검증 포함하여 EntityId 추출 (이전 씬의 userData는 무시)
			EntityId entityA = pImpl->m_world.ExtractEntityIdFromUserData(e.userDataA);
			EntityId entityB = pImpl->m_world.ExtractEntityIdFromUserData(e.userDataB);

			// 유효하지 않은 EntityId면 무시 (이전 씬의 이벤트)
			if (entityA == InvalidEntityId || entityB == InvalidEntityId) continue;

			// 현재 물리 시스템이 추적하는 엔티티만 처리 (씬 전환 중 stale userData 방지)
			if (pImpl->m_physicsSystem)
			{
				// PhysicsSystem의 IsTrackedEntity를 사용하여 현재 추적 중인 엔티티만 처리
				// 이는 씬 전환 중 파괴된 액터의 userData가 새 엔티티를 오염시키는 것을 방지
				if (!pImpl->m_physicsSystem->IsTrackedEntity(entityA) ||
					!pImpl->m_physicsSystem->IsTrackedEntity(entityB))
				{
					continue; // 둘 중 하나라도 추적 중이 아니면 이벤트 무시
				}
			}

			// 이벤트 타입에 따른 처리
			switch (e.type)
			{
			case PhysicsEventType::ContactBegin:
				// TODO: 게임 시스템으로 전달 (예: 스크립트 이벤트, 컴포넌트 갱신 등)
				// ALICE_LOG_INFO("ContactBegin: Entity %llu <-> %llu", 
				//     (unsigned long long)entityA, (unsigned long long)entityB);
				break;
			case PhysicsEventType::ContactEnd:
				// TODO: 게임 시스템으로 전달
				break;
			case PhysicsEventType::TriggerEnter:
				// TODO: 게임 시스템으로 전달
				// ALICE_LOG_INFO("TriggerEnter: Entity %llu <-> %llu", 
				//     (unsigned long long)entityA, (unsigned long long)entityB);
				break;
			case PhysicsEventType::TriggerExit:
				// TODO: 게임 시스템으로 전달
				break;
			case PhysicsEventType::JointBreak:
			{
				// jointUserData는 PhysicsSystem이 MakeUserData(epoch, entityId)로 넣었음
				// 조인트를 소유한 엔티티 (조인트 컴포넌트가 붙어있는 엔티티)
				EntityId jointOwner = InvalidEntityId;
				if (e.jointUserData)
				{
					jointOwner = pImpl->m_world.ExtractEntityIdFromUserData(e.jointUserData);
				}

				// 연결된 두 액터의 엔티티
				EntityId actorAEntity = InvalidEntityId;
				EntityId actorBEntity = InvalidEntityId;
				if (e.userDataA)
				{
					actorAEntity = pImpl->m_world.ExtractEntityIdFromUserData(e.userDataA);
				}
				if (e.userDataB)
				{
					actorBEntity = pImpl->m_world.ExtractEntityIdFromUserData(e.userDataB);
				}

				// 로그 출력 (필요하면 나중에 게임 시스템/스크립트 이벤트로 전달 가능)
				if (jointOwner != InvalidEntityId)
				{
					ALICE_LOG_INFO("[Physics] JointBreak: jointOwner=%llu, ActorA=%llu, ActorB=%llu",
						(unsigned long long)jointOwner,
						(unsigned long long)actorAEntity,
						(unsigned long long)actorBEntity);

					// PhysicsSystem에 조인트가 부러졌음을 알려서 컴포넌트의 jointHandle을 null로 설정
					// (다음 Update에서 감지하여 재생성하거나 정리 가능)
					if (pImpl->m_physicsSystem)
					{
						// PhysicsSystem에 조인트 정리 요청 (필요시 구현)
						// 현재는 로그만 남기고, 다음 Update에서 컴포넌트 변경 감지로 자동 정리됨
					}
				}
				break;
			}
			}
		}
		
		// 큐 비우기
		pImpl->m_physicsEventQueue.clear();
	}

	void Engine::ProcessCombatHits()
	{
		if (pImpl->m_combatHitQueue.empty())
			return;

		pImpl->m_combatSystem.ProcessHits(pImpl->m_world, pImpl->m_combatHitQueue);
		pImpl->m_combatHitQueue.clear();
	}

	//=========================================================


	void Engine::Render()
	{
		if (!pImpl->m_renderDevice) return;

		RenderUpdateWorldTransformCache();
		RenderHandlePendingRenderSystemChange();

		if (!RenderValidateRenderSystems()) return;

		RenderBeginFrame();

		if (pImpl->m_editorMode)
		{
			RenderEditorUI();
			RenderEditorDebugBuild();
		}

		RenderEnsureAnimationIfNotUpdated();
		RenderBuildSkinnedDrawList();
		RenderOnDemandSkinnedMeshLoading();

		// Update에서 애니메이션이 안 돈 프레임을 Render에서 보정하므로, 오디오 위치도 그 이후가 안전
		RenderAudioUpdate();

		RenderMainPass();
		RenderUnbindDepthOnly();

		RenderComputeEffects();
		RenderParticleOverlayComposite();
		RenderDebugOverlayComposite();
		RenderGameModeToneMappingAndUI();

		RenderOverlayEffects();
		if (pImpl->m_editorMode)
			RenderEditorDraw();

		RenderEndFrame();
	}

	// =========================
	// Render helpers
	void Engine::RenderUpdateWorldTransformCache()
	{
		pImpl->m_world.UpdateTransformMatrices();
	}

	void Engine::RenderHandlePendingRenderSystemChange()
	{
		if (!pImpl->m_pendingRenderSystemChange) return;

		auto* context = pImpl->m_renderDevice->GetImmediateContext();
		if (context)
		{
			ID3D11RenderTargetView* nullRTVs[8] = { nullptr };
			context->OMSetRenderTargets(8, nullRTVs, nullptr);

			ID3D11ShaderResourceView* nullSRVs[16] = { nullptr };
			context->VSSetShaderResources(0, 16, nullSRVs);
			context->PSSetShaderResources(0, 16, nullSRVs);

			ID3D11Buffer* nullCBs[16] = { nullptr };
			context->VSSetConstantBuffers(0, 16, nullCBs);
			context->PSSetConstantBuffers(0, 16, nullCBs);

			context->VSSetShader(nullptr, nullptr, 0);
			context->PSSetShader(nullptr, nullptr, 0);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->CSSetShader(nullptr, nullptr, 0);

			context->Flush();
		}

		pImpl->m_useForwardRendering = pImpl->m_pendingUseForwardRendering;
		pImpl->m_pendingRenderSystemChange = false;

		ALICE_LOG_INFO("Engine::Render: 렌더링 시스템 전환 완료 (Forward: %s)",
			pImpl->m_useForwardRendering ? "true" : "false");
	}

	bool Engine::RenderValidateRenderSystems() const
	{
		if (pImpl->m_useForwardRendering && !pImpl->m_forwardRenderSystem) return false;
		if (!pImpl->m_useForwardRendering && !pImpl->m_deferredRenderSystem) return false;
		return true;
	}

	void Engine::RenderBeginFrame()
	{
		float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
		pImpl->m_renderDevice->BeginFrame(clearColor);
	}

	void Engine::RenderEditorUI()
	{
		if (!pImpl->m_editorMode) return;

		pImpl->m_editorCore.BeginFrame();

		int shadingMode = static_cast<int>(pImpl->m_shadingMode);
		pImpl->m_editorCore.DrawEditorUI(
			pImpl->m_world, pImpl->m_camera, *pImpl->m_forwardRenderSystem, *pImpl->m_deferredRenderSystem, pImpl->m_sceneManager.get(),
			pImpl->m_timer.DeltaTime(), (pImpl->m_timer.DeltaTime() > 0) ? (1.0f / pImpl->m_timer.DeltaTime()) : 0.0f,
			pImpl->m_isPlaying, shadingMode, pImpl->m_useFillLight,
			pImpl->m_selectedEntity, pImpl->m_viewportPicker, pImpl->m_cameraMoveSpeed,
			pImpl->m_useForwardRendering,
			pImpl->m_pvdEnabled, pImpl->m_pvdHost, pImpl->m_pvdPort,
			&pImpl->m_uiWorld,
			pImpl->m_debugDraw
		);

		if (static_cast<Impl::ShadingMode>(shadingMode) != pImpl->m_shadingMode)
		{
			pImpl->m_shadingMode = static_cast<Impl::ShadingMode>(shadingMode);
		}
	}

	void Engine::RenderEditorDebugBuild()
	{
		if (!pImpl->m_editorMode) return;

		DebugDrawSystem* gizmo = pImpl->m_gizmoDrawSystem.get();
		DebugDrawSystem* dbg = pImpl->m_debugDrawSystem.get();
		if (gizmo) gizmo->Clear();
		if (dbg) dbg->Clear();

		// 디버그 축(XYZ) + 그리드 (깊이 테스트용)
		if (gizmo && pImpl->m_debugDraw)
		{
			const float axisLen = 300.0f;
			const float axisRadius = 0.08f;

			// X축: 약간 다홍빛이 도는 레드 (순수 빨강보다 세련됨)
			const DirectX::XMFLOAT4 colorX = { 0.9f, 0.2f, 0.2f, 1.0f };
			// Y축: 형광 연두색 느낌을 약간 섞은 그린 (가시성 확보)
			const DirectX::XMFLOAT4 colorY = { 0.2f, 0.8f, 0.2f, 1.0f };
			// Z축: 깊이감 있는 스카이 블루/아주르 블루
			const DirectX::XMFLOAT4 colorZ = { 0.2f, 0.4f, 0.9f, 1.0f };

			gizmo->AddCylinder({ -axisLen, 0.f, 0.f }, { axisLen, 0.f, 0.f }, axisRadius, colorX); // X
			gizmo->AddCylinder({ 0.f, -axisLen, 0.f }, { 0.f, axisLen, 0.f }, axisRadius, colorY); // Y
			gizmo->AddCylinder({ 0.f, 0.0f, -axisLen }, { 0.f, 0.f, axisLen }, axisRadius, colorZ); // Z

			// 에디터 격자 (XZ 평면) - 카메라 높이에 따라 셀 크기를 키워 멀어질수록 합쳐 보이게 처리
			auto AddGridXZ = [&](float baseStep, int halfLines, float y,
				const DirectX::XMFLOAT4& minorCol, const DirectX::XMFLOAT4& majorCol)
			{
				const DirectX::XMFLOAT3 camPos = pImpl->m_camera.GetPosition();
				float step = baseStep;
				const float height = std::fabsf(camPos.y);

				// 높이에 따라 셀 크기를 키움 (멀수록 덜 촘촘하게)
				while (height > step * 5.0f && step < baseStep * 128.0f)
				{
					step *= 2.0f;
				}

				const int majorEvery = 5;
				const float extent = step * static_cast<float>(halfLines);

				const float centerX = std::round(camPos.x / step) * step;
				const float centerZ = std::round(camPos.z / step) * step;

				for (int i = -halfLines; i <= halfLines; ++i)
				{
					const float x = centerX + static_cast<float>(i) * step;
					const float z = centerZ + static_cast<float>(i) * step;
					const DirectX::XMFLOAT4 col = (i % majorEvery == 0) ? majorCol : minorCol;

					gizmo->AddLine({ x, y, centerZ - extent }, { x, y, centerZ + extent }, col);
					gizmo->AddLine({ centerX - extent, y, z }, { centerX + extent, y, z }, col);
				}
			};

			AddGridXZ(
				1.0f, 20, 0.0f,
				{ 0.25f, 0.25f, 0.25f, 1.0f },
				{ 0.35f, 0.35f, 0.35f, 1.0f }
			);
		}

		pImpl->m_debugDrawComponentSystem.Build(
			pImpl->m_world,
			dbg,
			gizmo,
			pImpl->m_selectedEntity,
			pImpl->m_debugDraw,
			true
		);

		// 나머지 디버그 요소 (항상 보이도록 오버레이)
		if (dbg && pImpl->m_debugDraw)
		{
			// === FBX/SkinnedMesh 디버그 AABB 박스 ===
			// - SkinnedMeshRegistry의 sourceModel(FbxModel)에서 로컬 AABB를 얻어,
			//   엔티티 Transform(S*R*T)을 적용한 OBB(로컬 AABB의 월드 변환)를 라인으로 표시합니다.
			auto AddBoxLines = [&](const DirectX::XMFLOAT3 corners[8], const DirectX::XMFLOAT4& col)
			{
				// bottom
				dbg->AddLine(corners[0], corners[1], col);
				dbg->AddLine(corners[1], corners[2], col);
				dbg->AddLine(corners[2], corners[3], col);
				dbg->AddLine(corners[3], corners[0], col);
				// top
				dbg->AddLine(corners[4], corners[5], col);
				dbg->AddLine(corners[5], corners[6], col);
				dbg->AddLine(corners[6], corners[7], col);
				dbg->AddLine(corners[7], corners[4], col);
				// sides
				dbg->AddLine(corners[0], corners[4], col);
				dbg->AddLine(corners[1], corners[5], col);
				dbg->AddLine(corners[2], corners[6], col);
				dbg->AddLine(corners[3], corners[7], col);
			};

			for (const auto& [entityId, skinned] : pImpl->m_world.GetComponents<SkinnedMeshComponent>())
			{
				if (skinned.meshAssetPath.empty())
					continue;

				const auto* t = pImpl->m_world.GetComponent<TransformComponent>(entityId);
				if (!t)
					continue;

				auto mesh = pImpl->m_skinnedMeshRegistry.Find(skinned.meshAssetPath);
				if (!mesh || !mesh->sourceModel)
					continue;

				DirectX::XMFLOAT3 mn{}, mx{};
				if (!mesh->sourceModel->GetLocalBounds(mn, mx))
					continue;

				// 로컬 AABB 8 코너
				DirectX::XMFLOAT3 local[8] = {
					{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
					{mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}
				};

				// 월드 행렬 (렌더러/피커와 동일: S*R*T)
				using namespace DirectX;
				const XMVECTOR S = XMLoadFloat3(&t->scale);
				const XMVECTOR R = XMLoadFloat3(&t->rotation);
				const XMVECTOR T = XMLoadFloat3(&t->position);
				const XMMATRIX worldM =
					XMMatrixScalingFromVector(S) *
					XMMatrixRotationRollPitchYawFromVector(R) *
					XMMatrixTranslationFromVector(T);

				// 월드 코너로 변환
				DirectX::XMFLOAT3 worldCorners[8]{};
				for (int i = 0; i < 8; ++i)
				{
					const XMVECTOR p = XMVectorSet(local[i].x, local[i].y, local[i].z, 1.0f);
					const XMVECTOR pw = XMVector3TransformCoord(p, worldM);
					XMStoreFloat3(&worldCorners[i], pw);
				}

				// 선택된 엔티티는 빨강, 나머지는 노랑
				const DirectX::XMFLOAT4 col = (entityId == pImpl->m_selectedEntity)
					? DirectX::XMFLOAT4(1.f, 0.f, 0.f, 1.f)
					: DirectX::XMFLOAT4(1.f, 1.f, 0.f, 1.f);

				AddBoxLines(worldCorners, col);
			}

			// AudioSource: 감쇠 반경 시각화
			auto DrawRing = [&](const DirectX::XMFLOAT3& center, float radius, const DirectX::XMFLOAT3& axisX, const DirectX::XMFLOAT3& axisZ, const DirectX::XMFLOAT4& color)
			{
				const int segments = 24;
				const float step = DirectX::XM_2PI / segments;

				DirectX::XMFLOAT3 prev;
				// 초기점: center + axisX * radius
				{
					using namespace DirectX;
					XMVECTOR c = XMLoadFloat3(&center);
					XMVECTOR ax = XMLoadFloat3(&axisX);
					XMVECTOR p = c + ax * radius;
					XMStoreFloat3(&prev, p);
				}

				for (int i = 1; i <= segments; ++i)
				{
					float angle = step * i;
					float c = cosf(angle);
					float s = sinf(angle);

					using namespace DirectX;
					XMVECTOR cent = XMLoadFloat3(&center);
					XMVECTOR ax = XMLoadFloat3(&axisX);
					XMVECTOR az = XMLoadFloat3(&axisZ);

					XMVECTOR currVec = cent + (ax * c * radius) + (az * s * radius);
					DirectX::XMFLOAT3 curr;
					XMStoreFloat3(&curr, currVec);

					dbg->AddLine(prev, curr, color);
					prev = curr;
				}
			};

			for (const auto& [entityId, src] : pImpl->m_world.GetComponents<AudioSourceComponent>())
			{
				if (!src.is3D) continue;
				if (entityId != pImpl->m_selectedEntity && !src.debugDraw) continue;

				const auto* t = pImpl->m_world.GetComponent<TransformComponent>(entityId);
				if (!t) continue;

				// Min Distance (Green)
				DrawRing(t->position, src.minDistance, { 1,0,0 }, { 0,0,1 }, { 0,1,0,1 }); // XZ plane
				DrawRing(t->position, src.minDistance, { 0,1,0 }, { 1,0,0 }, { 0,1,0,1 }); // YX plane

				// Max Distance (Red)
				DrawRing(t->position, src.maxDistance, { 1,0,0 }, { 0,0,1 }, { 1,0,0,1 }); // XZ plane
				DrawRing(t->position, src.maxDistance, { 0,1,0 }, { 1,0,0 }, { 1,0,0,1 }); // YX plane
			}
		}
	}

	void Engine::RenderEnsureAnimationIfNotUpdated()
	{
		if (pImpl->m_animUpdatedThisFrame) return;

		const double dtSec = static_cast<double>(pImpl->m_timer.DeltaTime());
		pImpl->m_attackDriverSystem.Update(pImpl->m_world);
		pImpl->m_advancedAnimSystem.Update(pImpl->m_world, dtSec);
		pImpl->m_skinnedAnimSystem.Update(pImpl->m_world, dtSec);
		pImpl->m_socketWorldUpdateSystem.Update(pImpl->m_world);
		pImpl->m_socketAttachmentSystem.Update(pImpl->m_world);
		pImpl->m_animUpdatedThisFrame = true;
	}

	void Engine::RenderBuildSkinnedDrawList()
	{
		pImpl->m_skinnedMeshSystem.BuildDrawList(pImpl->m_world, pImpl->m_skinnedDrawCommands);
	}

	void Engine::RenderOnDemandSkinnedMeshLoading()
	{
		FbxImporter importer(pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);
		auto* device = pImpl->m_renderDevice ? pImpl->m_renderDevice->GetDevice() : nullptr;
		if (!device) return;

		const auto& skinnedMap = pImpl->m_world.GetComponents<SkinnedMeshComponent>();
		for (const auto& [entityId, comp] : skinnedMap)
		{
			if (comp.meshAssetPath.empty()) continue;

			if (!pImpl->m_skinnedMeshRegistry.Has(comp.meshAssetPath) && !comp.instanceAssetPath.empty())
			{
				ALICE_LOG_INFO("[Engine] On-demand loading mesh: meshKey=\"%s\" instanceAssetPath=\"%s\"",
					comp.meshAssetPath.c_str(), comp.instanceAssetPath.c_str());

				pImpl->m_skinnedMeshRegistry.LoadFromFbxAsset(
					comp.meshAssetPath, comp.instanceAssetPath,
					pImpl->m_resourceManager, importer, device
				);
			}
		}
	}

	void Engine::RenderAudioUpdate()
	{
		pImpl->m_audioSystem.Update(pImpl->m_world, static_cast<double>(pImpl->m_timer.DeltaTime()));
	}

	void Engine::RenderMainPass()
	{
		EntityId renderEntity = (pImpl->m_sceneManager) ? pImpl->m_sceneManager->GetPrimaryRenderableEntity() : InvalidEntityId;

		std::unordered_set<EntityId> cameraIDs;
		for (const auto& [id, _] : pImpl->m_world.GetComponents<CameraComponent>())
			cameraIDs.insert(id);

		const int finalShadingMode = pImpl->m_editorMode
			? static_cast<int>(pImpl->m_shadingMode)
			: static_cast<int>(Impl::ShadingMode::PBR);

		if (pImpl->m_useForwardRendering)
		{
			pImpl->m_forwardRenderSystem->Render(
				pImpl->m_world, pImpl->m_camera, renderEntity, cameraIDs,
				finalShadingMode, pImpl->m_useFillLight, pImpl->m_skinnedDrawCommands,
				pImpl->m_uiWorld
			);
		}
		else
		{
			pImpl->m_deferredRenderSystem->Render(
				pImpl->m_world, pImpl->m_camera, renderEntity, cameraIDs,
				finalShadingMode, pImpl->m_useFillLight, pImpl->m_skinnedDrawCommands,
				pImpl->m_uiWorld, pImpl->m_editorMode, pImpl->m_isPlaying
			);
		}
	}

	void Engine::RenderUnbindDepthOnly()
	{
		ID3D11RenderTargetView* currentRTV = nullptr;
		ID3D11DepthStencilView* currentDSV = nullptr;

		auto* ctx = pImpl->m_renderDevice->GetImmediateContext();
		ctx->OMGetRenderTargets(1, &currentRTV, &currentDSV);

		if (currentRTV)
		{
			ctx->OMSetRenderTargets(1, &currentRTV, nullptr);
			currentRTV->Release();
		}
		if (currentDSV)
			currentDSV->Release();
	}

	void Engine::RenderComputeEffects()
	{
		if (pImpl->m_computeEffectSystem &&
			((pImpl->m_useForwardRendering && pImpl->m_forwardRenderSystem) ||
				(!pImpl->m_useForwardRendering && pImpl->m_deferredRenderSystem)))
		{
			DirectX::XMMATRIX viewProj = DirectX::XMMatrixIdentity();
			DirectX::XMFLOAT3 cameraPos(0.0f, 0.0f, -5.0f);

			if (pImpl->m_useForwardRendering && pImpl->m_forwardRenderSystem)
			{
				viewProj = pImpl->m_forwardRenderSystem->GetLastViewProj();
				cameraPos = pImpl->m_forwardRenderSystem->GetLastCameraPos();
			}
			else if (!pImpl->m_useForwardRendering && pImpl->m_deferredRenderSystem)
			{
				viewProj = pImpl->m_deferredRenderSystem->GetLastViewProj();
				cameraPos = pImpl->m_deferredRenderSystem->GetLastCameraPos();
			}

			ID3D11ShaderResourceView* depthSRV = nullptr;
			if (pImpl->m_useForwardRendering && pImpl->m_forwardRenderSystem)
			{
				depthSRV = pImpl->m_forwardRenderSystem->GetSceneDepthSRV();
			}
			else if (!pImpl->m_useForwardRendering && pImpl->m_deferredRenderSystem)
			{
				depthSRV = pImpl->m_deferredRenderSystem->GetSceneDepthSRV();
			}

			float dtSec = pImpl->m_timer.DeltaTime();
			float nearPlane = pImpl->m_camera.GetNearPlane();
			float farPlane = pImpl->m_camera.GetFarPlane();
			pImpl->m_computeEffectSystem->Execute(pImpl->m_world, viewProj, cameraPos, depthSRV, nearPlane, farPlane, dtSec);
		}
	}

	void Engine::RenderParticleOverlayComposite()
	{
		// 에디터 모드: 뷰포트 렌더 타겟에 파티클 오버레이 합성
		if (pImpl->m_editorMode && pImpl->m_computeEffectSystem && pImpl->m_computeEffectSystem->HasActiveEffect())
		{
			ID3D11ShaderResourceView* particleSRV = pImpl->m_computeEffectSystem->GetOutputSRV();
			if (particleSRV)
			{
				if (pImpl->m_useForwardRendering && pImpl->m_forwardRenderSystem)
				{
					pImpl->m_forwardRenderSystem->RenderParticleOverlayToViewport(particleSRV);
				}
				else if (!pImpl->m_useForwardRendering && pImpl->m_deferredRenderSystem)
				{
					pImpl->m_deferredRenderSystem->RenderParticleOverlayToViewport(particleSRV);
				}
			}
		}

		// 게임 모드: 백버퍼에 파티클 오버레이 합성
		if (!pImpl->m_editorMode && pImpl->m_computeEffectSystem && pImpl->m_computeEffectSystem->HasActiveEffect() && pImpl->m_forwardRenderSystem)
		{
			ID3D11RenderTargetView* backBufferRTV = pImpl->m_renderDevice->GetBackBufferRTV();
			if (backBufferRTV)
			{
				D3D11_VIEWPORT viewport = {};
				viewport.Width = static_cast<float>(pImpl->m_width);
				viewport.Height = static_cast<float>(pImpl->m_height);
				viewport.MaxDepth = 1.0f;

				if (pImpl->m_useForwardRendering)
				{
					pImpl->m_forwardRenderSystem->RenderToneMapping(backBufferRTV, viewport);
				}
				else
				{
					DeferredRenderSystem* deferred = pImpl->m_deferredRenderSystem.get();
					ID3D11ShaderResourceView* sceneSRV = deferred->GetSceneColorSRV();
					deferred->RenderToneMapping(sceneSRV, backBufferRTV, viewport);
					deferred->RenderPostProcess(backBufferRTV, viewport);
				}

				ID3D11ShaderResourceView* particleSRV = pImpl->m_computeEffectSystem->GetOutputSRV();
				if (particleSRV)
				{
					pImpl->m_forwardRenderSystem->RenderParticleOverlay(particleSRV, backBufferRTV, viewport);
				}

				pImpl->m_aliceUIRenderer.RenderScreen(pImpl->m_world, pImpl->m_camera, backBufferRTV, viewport.Width, viewport.Height);
			}
		}
	}

	void Engine::RenderDebugOverlayComposite()
	{
		if (!pImpl->m_editorMode) return;

		auto RenderDebugOverlay = [&](DebugDrawSystem* system, bool depthTest)
		{
			if (!system) return;
			if (pImpl->m_useForwardRendering && pImpl->m_forwardRenderSystem)
			{
				pImpl->m_forwardRenderSystem->RenderDebugOverlayToViewport(*system, pImpl->m_camera, depthTest);
			}
			else if (!pImpl->m_useForwardRendering && pImpl->m_deferredRenderSystem)
			{
				pImpl->m_deferredRenderSystem->RenderDebugOverlayToViewport(*system, pImpl->m_camera, depthTest);
			}
		};

		RenderDebugOverlay(pImpl->m_gizmoDrawSystem.get(), true);
		RenderDebugOverlay(pImpl->m_debugDrawSystem.get(), false);
	}

	void Engine::RenderGameModeToneMappingAndUI()
	{
		if (pImpl->m_editorMode) return;

		ID3D11RenderTargetView* backBufferRTV = pImpl->m_renderDevice->GetBackBufferRTV();
		if (!backBufferRTV) return;

		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(pImpl->m_width);
		viewport.Height = static_cast<float>(pImpl->m_height);
		viewport.MaxDepth = 1.0f;

		if (pImpl->m_useForwardRendering)
		{
			pImpl->m_forwardRenderSystem->RenderToneMapping(backBufferRTV, viewport);
			pImpl->m_aliceUIRenderer.RenderScreen(pImpl->m_world, pImpl->m_camera, backBufferRTV, viewport.Width, viewport.Height);
		}
		else
		{
			DeferredRenderSystem* deferred = pImpl->m_deferredRenderSystem.get();
			ID3D11ShaderResourceView* sceneSRV = deferred->GetSceneColorSRV();
			deferred->RenderPostProcess(backBufferRTV, viewport);
			pImpl->m_aliceUIRenderer.RenderScreen(pImpl->m_world, pImpl->m_camera, backBufferRTV, viewport.Width, viewport.Height);
		}
	}

	void Engine::RenderOverlayEffects()
	{
		if (pImpl->m_effectSystem) pImpl->m_effectSystem->Render(pImpl->m_world, pImpl->m_camera);
		if (pImpl->m_trailRenderSystem) pImpl->m_trailRenderSystem->Render(pImpl->m_world, pImpl->m_camera);
	}

	void Engine::RenderEditorDraw()
	{
		if (pImpl->m_editorMode)
			pImpl->m_editorCore.RenderDrawData();
	}

	void Engine::RenderEndFrame()
	{
		pImpl->m_renderDevice->EndFrame();
	}

	void Engine::EnsureSkinnedMeshesRegisteredForWorld()
	{
		auto* device = pImpl->m_renderDevice ? pImpl->m_renderDevice->GetDevice() : nullptr;
		if (!device || pImpl->m_world.GetComponents<SkinnedMeshComponent>().empty()) return;

		for (const auto& [entityId, comp] : pImpl->m_world.GetComponents<SkinnedMeshComponent>())
		{
			// 이미 등록되었거나 경로가 비어있으면 스킵
			if (comp.meshAssetPath.empty() || pImpl->m_skinnedMeshRegistry.Find(comp.meshAssetPath)) continue;

			// 논리적 파일 경로 구성
			std::filesystem::path assetPath = comp.instanceAssetPath.empty()
				? std::filesystem::path("Assets/Fbx") / (comp.meshAssetPath + ".fbxasset")
				: std::filesystem::path(comp.instanceAssetPath);

			// 절대경로가 섞여 있다면 파일명만 추출하여 표준 경로로 보정
			if (assetPath.is_absolute()) assetPath = std::filesystem::path("Assets/Fbx") / assetPath.filename();

			// .fbxasset 로드 (메타데이터)
			Alice::FbxInstanceAsset instance{};
			if (!Alice::LoadFbxInstanceAssetAuto(pImpl->m_resourceManager, assetPath, instance))
			{
				ALICE_LOG_WARN("Engine: Failed to load fbxasset '%s'", assetPath.string().c_str());
				continue;
			}

			// FBX 임포트 수행
			// GameMode: 논리 경로 유지 (Chunk 로딩), EditorMode: 물리 경로 변환 (파일 로딩)
			std::filesystem::path srcFbx = pImpl->m_editorMode
				? pImpl->m_resourceManager.Resolve(instance.sourceFbx)
				: std::filesystem::path(instance.sourceFbx);

			FbxImporter importer(pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);
			FbxImportResult res = importer.Import(device, srcFbx, FbxImportOptions{});

			ALICE_LOG_INFO("Engine: Registered Mesh '%s' -> '%s'", comp.meshAssetPath.c_str(), res.meshAssetPath.c_str());
		}
	}

	void Engine::TrimVideoMemory()
	{
		pImpl->m_renderDevice->TrimVideoMemory();
	}

	void Engine::SetUseForwardRendering(bool useForward)
	{
		// 즉시 전환하지 않고, 다음 프레임 시작 시 전환하도록 플래그만 설정
		// 이렇게 하면 렌더링 중간에 리소스 상태가 꼬이는 것을 방지할 수 있습니다.
		if (pImpl->m_useForwardRendering != useForward)
		{
			pImpl->m_pendingRenderSystemChange = true;
			pImpl->m_pendingUseForwardRendering = useForward;
		}
	}

	bool Engine::GetUseForwardRendering() const
	{
		return pImpl->m_useForwardRendering;
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
		// ============================================= 아이콘 로드 =============================================
		// 파일 로드 실패 시 기본 아이콘 사용
		// 경로는 한 번만 변환하여 사용
		const std::wstring iconPath = pImpl->m_resourceManager.Resolve("Resource/Icon/Alice.ico").wstring();

		auto hIconBig = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
		auto hIconSmall = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));

		// ============================================= 윈도우 클래스 등록 =============================================
		// C++ 구조체 제로 초기화({})를 활용하여 불필요한 0 대입 생략
		WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = &Engine::WindowProc;
		wc.hInstance = pImpl->m_hInstance;
		wc.hIcon = hIconBig ? hIconBig : LoadIcon(nullptr, IDI_APPLICATION);     // Fallback 처리
		wc.hIconSm = hIconSmall ? hIconSmall : LoadIcon(nullptr, IDI_APPLICATION);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszClassName = kWindowClassName;

		if (!RegisterClassExW(&wc)) return false;

		// ============================================= 실제 윈도우 크기 계산 =============================================
		// Client Size -> Window Size
		RECT rc = { 0, 0, static_cast<LONG>(pImpl->m_width), static_cast<LONG>(pImpl->m_height) };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		// ============================================= 윈도우 생성 =============================================
		// this 포인터 전달
		pImpl->m_hWnd = CreateWindowExW(
			0, kWindowClassName, L"AliceRenderer", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, // 계산된 너비/높이 바로 사용
			nullptr, nullptr, pImpl->m_hInstance, this
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

		// 디바이스 리사이즈 및 카메라 종횡비 갱신
		if (pImpl->m_renderDevice)
		{
			pImpl->m_renderDevice->Resize(width, height);

			// 높이가 0이어도 안전하게 1로 처리하여 계산
			const float aspect = static_cast<float>(width) / (std::max)(height, 1u);
			pImpl->m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
		}

		// 렌더러 리사이즈 (텍스처 재생성 등)
		if (pImpl->m_forwardRenderSystem)
		{
			pImpl->m_forwardRenderSystem->Resize(width, height);
		}
		if (pImpl->m_deferredRenderSystem)
		{
			pImpl->m_deferredRenderSystem->Resize(width, height);
		}
		if (pImpl->m_computeEffectSystem)
		{
			pImpl->m_computeEffectSystem->Resize(width, height);
		}

		if(pImpl->m_uiWorld.m_d3dDev)
		{
			// UI 시스템 리사이즈 (텍스처 재생성)
			//pImpl->m_uiWorld.Create2DTex(width, height);
		}
		
	}

	LRESULT Engine::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_SIZE:
			// 리사이즈
			// lParam의 하위/상위 워드에서 해상도 추출 후 즉시 반영
			OnResize(static_cast<std::uint32_t>(LOWORD(lParam)), static_cast<std::uint32_t>(HIWORD(lParam)));
			return 0;

		case WM_DESTROY:
			// 종료
			// 메인 루프 플래그 해제 및 종료 메시지 전송
			pImpl->m_isRunning = false;
			PostQuitMessage(0);
			return 0;

		case WM_INPUT:
			// Raw Input 처리
			pImpl->m_inputSystem.ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
			return 0;
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

	LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		// ============================================= ImGui 메시지 선처리 =============================================
		// 처리되었다면 즉시 종료)
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;

		// ============================================= DirectXTK 입력 처리 =============================================
		// Switch case로 메시지 호출
		switch (message)
		{
		case WM_ACTIVATEAPP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_INPUT:
			// Raw Input은 InputSystem에서 처리 (HandleMessage에서 처리됨)
			// DirectXTK에도 전달 (버튼 상태 등은 DirectXTK에서 관리)
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
		case WM_MOUSEWHEEL: case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_MOUSEHOVER:
			DirectX::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYUP:
			DirectX::Keyboard::ProcessMessage(message, wParam, lParam);
			break;
		}

		// ============================================= Engine 인스턴스 연동 =============================================
		// 창 생성 시(WM_NCCREATE), CreateWindow에서 넘긴 'this' 포인터를 HWND에 저장
		if (message == WM_NCCREATE)
		{
			auto* const createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		}

		// 저장된 Engine 포인터를 가져와 멤버 함수로 넣어줌, 없으면 기본 윈도우 처리 반환
		auto* const engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
		return engine ? engine->HandleMessage(hWnd, message, wParam, lParam)
			: DefWindowProcW(hWnd, message, wParam, lParam);
	}
}
