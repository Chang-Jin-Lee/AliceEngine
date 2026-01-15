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
#include "Core/World.h" // 여기 컴포넌트 있찌롱
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
#include "Rendering/SkinnedMeshRegistry.h"
#include "Editor/ViewportPicker.h"
#include "Editor/EditorCore.h"
#include "Game/SkinnedMeshSystem.h"
#include "Game/SkinnedAnimationSystem.h"

#include "PhysX/Module/PhysicsModule.h" // 물리 모듈

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

		//===============================		
		PhysicsModule m_physics; // 물리 모듈
		//테스트용
		std::unique_ptr<IRigidBody>    m_testBox;
		std::unique_ptr<IPhysicsActor> m_testGround; // 옵션
		EntityId                       m_testEntity = InvalidEntityId;

		float m_physAccum = 0.0f;
		float m_physFixedDt = 1.0f / 60.0f;
		int   m_physMaxSubsteps = 4;
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

		// 렌더링 모드 전환 (true: Forward, false: Deferred)
		bool m_useForwardRendering = false;

		
		// 렌더링 시스템 전환 지연 처리 (안전한 전환을 위해)
		bool m_pendingRenderSystemChange = false;
		bool m_pendingUseForwardRendering = true;

		// Skinned FBX 메시 렌더링용 레지스트리/시스템
		SkinnedMeshRegistry m_skinnedMeshRegistry;
		SkinnedMeshSystem   m_skinnedMeshSystem{ m_skinnedMeshRegistry };
		SkinnedAnimationSystem m_skinnedAnimSystem{ m_skinnedMeshRegistry };
		std::vector<SkinnedDrawCommand> m_skinnedDrawCommands;
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
		pImpl->m_physics.ShutdownContext();
		pImpl->m_editorCore.Shutdown();
	}

	bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
	{
		LinkComponentRegistry();
		ALICE_LOG_INFO("Engine::Initialize: Begin (EditorMode=%d)", pImpl->m_editorMode);

		pImpl->m_hInstance = hInstance;

		// ============================================= 경로 설정하는 부분 =============================================
		// 실행 파일 위치를 구하여 리소스 로드 및 빌드 설정의 기준점으로 사용
		wchar_t pathBuf[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);
		const std::filesystem::path exeDir = std::filesystem::path(pathBuf).parent_path();

		// Editor: 프로젝트 루트 기준, Game: 실행 파일 기준
		pImpl->m_resourceManager.Configure(!pImpl->m_editorMode, exeDir);

		//===============================================================
		// 물리 초기화(씬 초기화보다 선행되어야함 - 중요함)
		PhysicsModule::ContextInitDesc ctx{};
		if (!pImpl->m_physics.InitializeContext(ctx)) return false;

		// ============================================= 시스템 초기화 =============================================
		// 윈도우, 입력, 렌더 디바이스 생성
		if (!CreateMainWindow(nCmdShow)) return false;

		pImpl->m_inputSystem.Initialize(pImpl->m_hWnd);

		pImpl->m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!pImpl->m_renderDevice->Initialize(pImpl->m_hWnd, pImpl->m_width, pImpl->m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: RenderDevice failed.");
			return false;
		}

		//============================================= 에디터 코어 =============================================
		// 에디터 모드일 경우에만 초기화 및 의존성 주입
		if (pImpl->m_editorMode)
		{
			pImpl->m_editorCore.SetResourceManager(&pImpl->m_resourceManager);
			pImpl->m_editorCore.SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);
			pImpl->m_editorCore.SetInputSystem(&pImpl->m_inputSystem);

			if (!pImpl->m_editorCore.Initialize(pImpl->m_hWnd, *pImpl->m_renderDevice)) return false;
		}

		// ============================================= 렌더 시스템 =============================================
		// Forward 렌더러 및 디버그 드로우 설정
		pImpl->m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*pImpl->m_renderDevice);
		pImpl->m_forwardRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		pImpl->m_forwardRenderSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		if (!pImpl->m_forwardRenderSystem->Initialize(pImpl->m_width, pImpl->m_height)) return false;

		// Deferred 렌더러 설정
		pImpl->m_deferredRenderSystem = std::make_unique<DeferredRenderSystem>(*pImpl->m_renderDevice);
		pImpl->m_deferredRenderSystem->SetResourceManager(&pImpl->m_resourceManager);
		pImpl->m_deferredRenderSystem->SetSkinnedMeshRegistry(&pImpl->m_skinnedMeshRegistry);

		if (!pImpl->m_deferredRenderSystem->Initialize(pImpl->m_width, pImpl->m_height)) return false;

		pImpl->m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*pImpl->m_renderDevice);
		if (!pImpl->m_debugDrawSystem->Initialize()) return false;

		// ============================================= 카메라 & 스크립트 =============================================
		// 기본 카메라 위치 설정 및 핫리로드 로드
		pImpl->m_cameraPosition = { 0.0f, 2.0f, -5.0f };
		pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		pImpl->m_camera.SetPerspective(DirectX::XM_PIDIV4, static_cast<float>(pImpl->m_width) / pImpl->m_height, 0.1f, 5000.0f);

		ScriptHotReload_Load();

		// ============================================= 씬 관리 =============================================
		// 씬 매니저 생성 및 초기 씬 로드
		pImpl->m_resourceManager.Clear();
		pImpl->m_sceneManager = std::make_unique<SceneManager>(pImpl->m_world, pImpl->m_resourceManager);

		bool isSceneLoaded = false;
		if (!pImpl->m_editorMode) // 게임 모드: 빌드 설정에서 씬 로드 시도
		{
			isSceneLoaded = LoadStartupSceneFromBuildSettings(pImpl->m_world, pImpl->m_resourceManager, exeDir);
		}

		if (!isSceneLoaded) // 에디터 모드거나 로드 실패 시 샘플 씬 사용
		{
			pImpl->m_sceneManager->SwitchTo("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: Loaded SampleScene (Fallback or Editor).");
		}

		RefreshPhysicsForCurrentWorld(); // 물리 1회 수동호출 (씬 로드 이후 1회)

		// ============================================= 후처리 =============================================
		// 스키닝 레지스트리 확인 및 스크립트 서비스 바인딩
		// 씬 전환할 때 실행될 TrimVideoMemory 바인딩
		EnsureSkinnedMeshesRegisteredForWorld();

		pImpl->m_scriptSystem.SetServices(&pImpl->m_inputSystem, pImpl->m_sceneManager.get(), &pImpl->m_resourceManager, &pImpl->m_skinnedMeshRegistry);
		pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::EnsureSkinnedMeshesRegisteredForWorld);
		pImpl->m_scriptSystem.onAfterSceneLoaded.BindObject(this, &Engine::RefreshPhysicsForCurrentWorld); // 씬 로드 직후 추가작업 등록하는거 같음
		pImpl->m_scriptSystem.onTrimVideoMemory.BindObject(this, &Engine::TrimVideoMemory);


		ALICE_LOG_INFO("Engine::Initialize: Success (Entities: %zu)", pImpl->m_world.GetComponents<TransformComponent>().size());
		return true;
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
		// 1. 타이머 및 입력 갱신
		pImpl->m_timer.Tick();
		const float dt = pImpl->m_timer.DeltaTime();
		pImpl->m_inputSystem.Update(dt);

		using namespace DirectX;

		// 2. 카메라 데이터 갱신 (위치/회전)
		// - 게임 모드(또는 에디터 플레이): 씬의 카메라 컴포넌트 동기화
		// - 에디터 모드: 마우스/키보드 입력을 통한 프리 카메라 이동
		bool updateFromScene = (!pImpl->m_editorMode || pImpl->m_isPlaying);

		if (updateFromScene)
		{
			// 우선순위: Primary 카메라 -> 없으면 첫 번째 발견된 카메라
			EntityId camId = InvalidEntityId;
			for (const auto& [id, cam] : pImpl->m_world.GetComponents<CameraComponent>())
			{
				if (cam.primary) { camId = id; break; }
				if (camId == InvalidEntityId) camId = id;
			}

			if (const auto* t = pImpl->m_world.GetComponent<TransformComponent>(camId))
			{
				const auto* c = pImpl->m_world.GetComponent<CameraComponent>(camId);
				pImpl->m_cameraPosition = t->position;
				pImpl->m_cameraYawRadians = t->rotation.y;
				pImpl->m_cameraPitchRadians = t->rotation.x;

				// 투영 행렬 갱신 (게임 중 FOV 변경 대응)
				const float aspect = static_cast<float>(pImpl->m_width) / pImpl->m_height;
				pImpl->m_camera.SetPerspective(c->fovYRad, aspect, c->nearPlane, c->farPlane);
			}
		}
		else if (pImpl->m_inputSystem.IsRightButtonDown()) // 에디터 프리캠 조작
		{
			// 키 입력에 따른 이동 벡터 계산
			XMVECTOR moveDir = XMVectorZero();
			auto& input = pImpl->m_inputSystem;

			if (input.IsKeyDown(Keyboard::W)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
			if (input.IsKeyDown(Keyboard::S)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
			if (input.IsKeyDown(Keyboard::D)) moveDir = XMVectorAdd(moveDir, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			if (input.IsKeyDown(Keyboard::A)) moveDir = XMVectorAdd(moveDir, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));
			if (input.IsKeyDown(Keyboard::E)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
			if (input.IsKeyDown(Keyboard::Q)) moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));

			if (!XMVector3Equal(moveDir, XMVectorZero()))
			{
				// 현재 카메라 회전을 기준으로 로컬 이동 벡터를 월드로 변환
				const XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0.0f);
				const XMVECTOR worldDir = XMVector3Normalize(XMVector3TransformNormal(moveDir, rotMat));
				const XMVECTOR currentPos = XMLoadFloat3(&pImpl->m_cameraPosition);

				XMStoreFloat3(&pImpl->m_cameraPosition, XMVectorAdd(currentPos, XMVectorScale(worldDir, pImpl->m_cameraMoveSpeed * dt)));
			}

			// 마우스 델타로 회전 갱신
			const POINT mouseDelta = input.GetMouseDelta();
			pImpl->m_cameraYawRadians += mouseDelta.x * pImpl->m_cameraMouseSensitivity;
			pImpl->m_cameraPitchRadians += mouseDelta.y * pImpl->m_cameraMouseSensitivity;
		}

		// 3. 카메라 최종 적용
		// Pitch 제한 및 View Matrix 생성
		const float pitchLimit = XMConvertToRadians(89.0f);
		pImpl->m_cameraPitchRadians = std::clamp(pImpl->m_cameraPitchRadians, -pitchLimit, pitchLimit);

		const XMMATRIX camRot = XMMatrixRotationRollPitchYaw(pImpl->m_cameraPitchRadians, pImpl->m_cameraYawRadians, 0.0f);
		const XMVECTOR camForward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRot);
		const XMVECTOR camPos = XMLoadFloat3(&pImpl->m_cameraPosition);

		XMFLOAT3 targetPos;
		XMStoreFloat3(&targetPos, XMVectorAdd(camPos, camForward));
		pImpl->m_camera.SetLookAt(pImpl->m_cameraPosition, targetPos, XMFLOAT3(0.0f, 1.0f, 0.0f));

		// 4. 로직 업데이트 (씬/스크립트)
		if (updateFromScene)
		{
			if (pImpl->m_sceneManager) pImpl->m_sceneManager->Update(dt);
			pImpl->m_scriptSystem.Tick(pImpl->m_world, dt);

			TickPhysics(dt); // 물리
		}
	}

	//=========================================================
	// 물리
	void Alice::Engine::RefreshPhysicsForCurrentWorld()
	{
		// settings가 없으면, 물리월드 제거(비물리 씬)
		const auto& settingsMap = pImpl->m_world.GetComponents<PhysicsSceneSettingsComponent>();

		if (settingsMap.empty())
		{
			pImpl->m_world.SetPhysicsWorld(nullptr);

			return;
		}

		const auto& settings = settingsMap.begin()->second;
		if (!settings.enablePhysics)
		{
			pImpl->m_world.SetPhysicsWorld(nullptr);
			return;
		}

		if (pImpl->m_world.GetPhysicsWorld())
			return;

		PhysicsModule::WorldDesc desc{};
		desc.gravity = Vec3(settings.gravity.x, settings.gravity.y, settings.gravity.z);
		// 필요하면 여기서 CCD / 이벤트 옵션들 설정

		std::shared_ptr<IPhysicsWorld> world = pImpl->m_physics.CreateWorld(desc); // shared_ptr<IPhysicsWorld> 반환
		ALICE_LOG_INFO("PhysicsWorld created: %p", world.get());
		pImpl->m_world.SetPhysicsWorld(world);

		// fixedDt/maxSubsteps도 엔진이 기억해야 한다면 Engine::Impl에 저장해 둬라.

		//테스트용 바디 생성
		// settings 반영
		pImpl->m_physFixedDt = settings.fixedDt;
		pImpl->m_physMaxSubsteps = settings.maxSubsteps;
		pImpl->m_physAccum = 0.0f;

		// ---- Smoke test (딱 한번만) ----
		if (!pImpl->m_testBox)
		{
			IPhysicsWorld* pw = pImpl->m_world.GetPhysicsWorld();
			if (pw)
			{
				// 바닥
				pImpl->m_testGround = pw->CreateStaticPlaneActor();

				// 테스트 엔티티 하나 만들고 Transform 추가(이미 있으면 스킵 가능)
				pImpl->m_testEntity = pImpl->m_world.CreateEntity();
				auto& tr = pImpl->m_world.AddComponent<TransformComponent>(pImpl->m_testEntity);
				tr.position = { 0.f, 5.f, 0.f };
				tr.rotation = { 0.f, 0.f, 0.f };

				// 동적 박스
				RigidBodyDesc rb{};
				rb.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(pImpl->m_testEntity));

				BoxColliderDesc box{};
				box.halfExtents = { 0.5f, 0.5f, 0.5f };
				box.userData = rb.userData;

				pImpl->m_testBox = pw->CreateDynamicBox(
					Vec3(0.f, 5.f, 0.f),
					Quat::Identity,
					rb,
					box
				);

				ALICE_LOG_INFO("SmokeTest box created. entity=%llu", (unsigned long long)pImpl->m_testEntity);
			}
		}
		//----------여기까지 테스트용

	}

	void Engine::TickPhysics(float dt)
	{
		IPhysicsWorld* pw = pImpl->m_world.GetPhysicsWorld();
		if (!pw) return;

		dt = std::min(dt, 0.25f);

		pImpl->m_physAccum += dt;
		int steps = 0;

		std::vector<ActiveTransform> moved;

		while (pImpl->m_physAccum >= pImpl->m_physFixedDt && steps < pImpl->m_physMaxSubsteps)
		{
			pw->Step(pImpl->m_physFixedDt);

			moved.clear();
			pw->DrainActiveTransforms(moved);

			for (const auto& at : moved)
			{
				if (!at.userData) continue;
				const EntityId id = static_cast<EntityId>(reinterpret_cast<std::uintptr_t>(at.userData));

				auto* tr = pImpl->m_world.GetComponent<TransformComponent>(id);
				if (!tr) continue;

				tr->position = { at.position.x, at.position.y, at.position.z };
				// 회전은 나중에(일단 위치만 확인해도 스모크 테스트 충분)
			}

			pImpl->m_physAccum -= pImpl->m_physFixedDt;
			++steps;
		}

		if (steps == pImpl->m_physMaxSubsteps)
			pImpl->m_physAccum = 0.0f;

		// 로그로 떨어지는지 확인 (1초에 1번만)
		static float logAccum = 0.f;
		logAccum += dt;
		if (logAccum > 1.f && pImpl->m_testBox)
		{
			logAccum = 0.f;
			auto p = pImpl->m_testBox->GetPosition();
			ALICE_LOG_INFO("TestBox Y = %.3f", p.y);
		}
	}

	//=========================================================

	void Engine::Render()
	{
		if (!pImpl->m_renderDevice) return;

		
		// ============================================= 렌더링 시스템 전환 처리 =============================================
		// 렌더링 시작 전에 전환 요청이 있으면 안전하게 전환합니다.
		if (pImpl->m_pendingRenderSystemChange)
		{
			// GPU 컨텍스트의 모든 리소스 바인딩 해제 (안전한 전환을 위해)
			auto* context = pImpl->m_renderDevice->GetImmediateContext();
			if (context)
			{
				// 모든 렌더 타겟 해제
				ID3D11RenderTargetView* nullRTVs[8] = { nullptr };
				context->OMSetRenderTargets(8, nullRTVs, nullptr);

				
				// 모든 셰이더 리소스 해제
				ID3D11ShaderResourceView* nullSRVs[16] = { nullptr };
				context->VSSetShaderResources(0, 16, nullSRVs);
				context->PSSetShaderResources(0, 16, nullSRVs);

				
				// 모든 상수 버퍼 해제
				ID3D11Buffer* nullCBs[16] = { nullptr };
				context->VSSetConstantBuffers(0, 16, nullCBs);
				context->PSSetConstantBuffers(0, 16, nullCBs);

				
				// 모든 셰이더 해제
				context->VSSetShader(nullptr, nullptr, 0);
				context->PSSetShader(nullptr, nullptr, 0);
				context->GSSetShader(nullptr, nullptr, 0);
				context->HSSetShader(nullptr, nullptr, 0);
				context->DSSetShader(nullptr, nullptr, 0);
				context->CSSetShader(nullptr, nullptr, 0);
				
				// Flush (모든 명령이 완료될 때까지 대기)
				context->Flush();
			}
			
			// 렌더링 시스템 전환
			pImpl->m_useForwardRendering = pImpl->m_pendingUseForwardRendering;
			pImpl->m_pendingRenderSystemChange = false;
			
			ALICE_LOG_INFO("Engine::Render: 렌더링 시스템 전환 완료 (Forward: %s)", 
				pImpl->m_useForwardRendering ? "true" : "false");
		}
		
		if (pImpl->m_useForwardRendering && !pImpl->m_forwardRenderSystem) return;
		if (!pImpl->m_useForwardRendering && !pImpl->m_deferredRenderSystem) return;

		float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
		pImpl->m_renderDevice->BeginFrame(clearColor); // Clear Color: Dark Blue

		// ============================================= 에디터 =============================================
		// UI 및 디버그 축 그리기
		if (pImpl->m_editorMode)
		{
			pImpl->m_editorCore.BeginFrame();

			// 에디터 UI 그리기 (인자 전달 간소화)
			int shadingMode = static_cast<int>(pImpl->m_shadingMode);
			pImpl->m_editorCore.DrawEditorUI(
				pImpl->m_world, pImpl->m_camera, *pImpl->m_forwardRenderSystem, *pImpl->m_deferredRenderSystem, pImpl->m_sceneManager.get(),
				pImpl->m_timer.DeltaTime(), (pImpl->m_timer.DeltaTime() > 0) ? (1.0f / pImpl->m_timer.DeltaTime()) : 0.0f,
				pImpl->m_isPlaying, shadingMode, pImpl->m_useFillLight,
				pImpl->m_selectedEntity, pImpl->m_viewportPicker, pImpl->m_cameraMoveSpeed,
				pImpl->m_useForwardRendering
			);
			pImpl->m_shadingMode = static_cast<Impl::ShadingMode>(shadingMode);

			// 디버그 축(XYZ) 그리기
			if (auto* dbg = pImpl->m_debugDrawSystem.get())
			{
				dbg->Clear();
				dbg->AddLine({ 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f }); // X: Red
				dbg->AddLine({ 0.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f, 1.f }); // Y: Green
				dbg->AddLine({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, { 0.f, 0.f, 1.f, 1.f }); // Z: Blue

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
			}
		}

		// ============================================= 애니메이션 =============================================
		// 스키닝 업데이트 및 드로우 커맨드 빌드
		// dt가 0이어도(일시정지) 에디터 조작 반영을 위해 갱신
		pImpl->m_skinnedAnimSystem.Update(pImpl->m_world, static_cast<double>(pImpl->m_timer.DeltaTime()));
		pImpl->m_skinnedMeshSystem.BuildDrawList(pImpl->m_world, pImpl->m_skinnedDrawCommands);

	// ============================================= 렌더링 =============================================
	// Forward/Deferred 렌더링 모드에 따라 분기
	EntityId renderEntity = (pImpl->m_sceneManager) ? pImpl->m_sceneManager->GetPrimaryRenderableEntity() : InvalidEntityId;

	// 카메라 엔티티 ID 집합 구성
	std::unordered_set<EntityId> cameraIDs;
	for (const auto& [id, _] : pImpl->m_world.GetComponents<CameraComponent>()) cameraIDs.insert(id);

	const int finalShadingMode = pImpl->m_editorMode ? static_cast<int>(pImpl->m_shadingMode) : static_cast<int>(Impl::ShadingMode::PBR);

	if (pImpl->m_useForwardRendering)
	{
		// Forward 렌더링
		pImpl->m_forwardRenderSystem->Render(
			pImpl->m_world, pImpl->m_camera, renderEntity, cameraIDs,
			finalShadingMode, pImpl->m_useFillLight, pImpl->m_skinnedDrawCommands
		);
	}
	else
	{
		// Deferred 렌더링
		pImpl->m_deferredRenderSystem->Render(
			pImpl->m_world, pImpl->m_camera, renderEntity, cameraIDs,
			finalShadingMode, pImpl->m_useFillLight, pImpl->m_skinnedDrawCommands
		);
	}

        // 게임 모드(에디터 UI 없음)에서는 최종 백버퍼로 톤매핑까지 수행
        if (!pImpl->m_editorMode)
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
                    pImpl->m_deferredRenderSystem->RenderToneMapping(backBufferRTV, viewport);
                }
            }
        }



		// ============================================= 오버레이 =============================================
		// 디버그 드로우 및 ImGui(에디터 전용)
		if (pImpl->m_debugDrawSystem) pImpl->m_debugDrawSystem->Render(pImpl->m_camera);
		if (pImpl->m_editorMode)      pImpl->m_editorCore.RenderDrawData();

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

		case WM_INPUT: case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
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