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

// 문자열 변환 / ImGui 래퍼
#include "Core/StringUtils.h"
#include "Core/ImGuiEx.h"
#include "Core/ScriptHotReload.h"
#include "Core/SceneFile.h"
#include "Core/Logger.h"
#include "Game/FbxImporter.h"
#include "Game/FbxAsset.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Alice
{
	namespace
	{
		// 윈도우 클래스 이름은 전역 상수로 관리합니다.
		constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";

		// BuildSettings.txt 에서 시작 씬(.scene 파일)을 읽어와 World 에 로드합니다.
		// - scenes 섹션은 "index: path" 형식으로 저장되어 있다고 가정합니다.
		bool LoadStartupSceneFromBuildSettings(World& world, const std::filesystem::path& exeDir)
		{
			namespace fs = std::filesystem;

			fs::path cfgPath = exeDir / "BuildSettings.txt";
			if (!fs::exists(cfgPath))
			{
				// 에디터에서 기본으로 저장하는 위치 (프로젝트 루트/Build) 도 한 번 더 시도
				fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Release → 프로젝트 루트
				cfgPath = projectRoot / "Build/BuildSettings.txt";
				if (!fs::exists(cfgPath))
					return false;
			}

			std::ifstream ifs(cfgPath);
			if (!ifs.is_open())
				return false;

			auto trim = [](std::string& s)
				{
					const char* ws = " \t\r\n";
					const auto  b = s.find_first_not_of(ws);
					const auto  e = s.find_last_not_of(ws);
					if (b == std::string::npos)
					{
						s.clear();
						return;
					}
					s = s.substr(b, e - b + 1);
				};

			bool inScenes = false;
			std::vector<std::string> scenes;
			std::string defaultScene;
			std::string line;
			while (std::getline(ifs, line))
			{
				trim(line);
				if (line.empty() || line[0] == '#')
					continue;

				// default: 행은 어디에 있어도 처리
				if (line.rfind("default:", 0) == 0)
				{
					std::string path = line.substr(std::strlen("default:"));
					trim(path);
					if (!path.empty())
					{
						defaultScene = path;
					}
					continue;
				}

				if (!inScenes)
				{
					if (line.rfind("scenes:", 0) == 0)
					{
						inScenes = true;
					}
					continue;
				}

				// "- path" 형식의 씬 목록
				if (!line.empty() && line[0] == '-')
				{
					std::string path = line.substr(1);
					trim(path);
					if (!path.empty())
					{
						scenes.push_back(path);
					}
					continue;
				}
			}

			if (defaultScene.empty())
			{
				if (!scenes.empty())
					defaultScene = scenes.front();
			}

			if (defaultScene.empty())
				return false;

			const std::string& scenePathStr = defaultScene;
			fs::path scenePath = scenePathStr;

			// 상대 경로는 exeDir 기준으로 해석
			if (!scenePath.is_absolute())
			{
				// 1) exeDir 기준으로 시도
				fs::path candidate = exeDir / scenePath;
				if (fs::exists(candidate))
				{
					scenePath = candidate;
				}
				else
				{
					// 2) exeDir 상위(프로젝트 루트) 기준으로도 시도
					fs::path projectRoot = exeDir.parent_path().parent_path().parent_path();
					candidate = projectRoot / scenePath;
					if (fs::exists(candidate))
					{
						scenePath = candidate;
					}
					else
					{
						// 그래도 없으면 exeDir 기준 상대 경로로 둠
						scenePath = exeDir / scenePath;
					}
				}
			}

			ALICE_LOG_INFO("LoadStartupSceneFromBuildSettings: loading scene \"%s\"",
				scenePath.string().c_str());

			bool ok = SceneFile::Load(world, scenePath);
			if (!ok)
			{
				ALICE_LOG_ERRORF("LoadStartupSceneFromBuildSettings: SceneFile::Load failed for \"%s\"",
					scenePath.string().c_str());
			}
			return ok;
		}
	}

	Engine::Engine(bool editorMode)
		: m_editorMode(editorMode)
	{
	}

	Engine::~Engine()
	{
		m_editorCore.Shutdown();
	}

	bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
	{
		ALICE_LOG_INFO("Engine::Initialize: begin (editorMode=%d)", m_editorMode ? 1 : 0);

		// 1) 인스턴스 핸들 보관
		m_hInstance = hInstance;

		// ResourceManager: 경로 해석 기준을 "모드"로 단순하게 고정합니다.
		// - editorMode(true)  : 프로젝트 루트 기준(= exeDir/../../..) Assets/Resource/Cooked
		// - gameMode(false)   : exeDir 기준 Assets/Resource/Cooked
		{
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			const std::filesystem::path exeDir = std::filesystem::path(exePathW).parent_path();
			m_resourceManager.Configure(/*gameMode=*/!m_editorMode, exeDir);
		}

		// 2) 윈도우 생성
		if (!CreateMainWindow(nCmdShow))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: CreateMainWindow failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: CreateMainWindow succeeded.");

		// 3) 입력 시스템 초기화 (DirectXTK Keyboard/Mouse)
		m_inputSystem.Initialize(m_hWnd);
		ALICE_LOG_INFO("Engine::Initialize: InputSystem initialized.");

		// 4) 렌더 디바이스 생성(D3D11 구현체 사용)
		m_renderDevice = std::make_unique<D3D11RenderDevice>();
		if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: D3D11RenderDevice::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: D3D11RenderDevice initialized.");

		// 5) ImGui / Editor 코어 초기화 (에디터 모드에서만)
		if (m_editorMode)
		{
			// EditorCore::Initialize 단계에서도 폰트/아이콘 등 리소스 경로가 필요하므로,
			// 리소스 포인터는 Initialize 이전에 주입합니다.
			m_editorCore.SetResourceManager(&m_resourceManager);
			m_editorCore.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);

			if (!m_editorCore.Initialize(m_hWnd, *m_renderDevice))
			{
				ALICE_LOG_ERRORF("Engine::Initialize: EditorCore::Initialize failed.");
				return false;
			}
			ALICE_LOG_INFO("Engine::Initialize: EditorCore initialized.");
		}

		// 6) Forward 렌더 시스템 초기화
		m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
		// 리소스 매니저를 렌더 시스템에 주입합니다 (텍스처 쿠킹/로딩 등에 사용).
		m_forwardRenderSystem->SetResourceManager(&m_resourceManager);
		// 스키닝 메시 레지스트리를 렌더 시스템에 주입 (서브셋/스켈레톤 메타데이터 조회용)
		m_forwardRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);
		if (!m_forwardRenderSystem->Initialize(m_width, m_height))
		{
			ALICE_LOG_ERRORF("Engine::Initialize: ForwardRenderSystem::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: ForwardRenderSystem initialized.");

		// 7) DebugDraw 시스템 초기화 (옵션 기능)
		m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
		if (!m_debugDrawSystem->Initialize())
		{
			ALICE_LOG_ERRORF("Engine::Initialize: DebugDrawSystem::Initialize failed.");
			return false;
		}
		ALICE_LOG_INFO("Engine::Initialize: DebugDrawSystem initialized.");

		// 8) 카메라 설정
		const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
		m_cameraPosition = DirectX::XMFLOAT3(0.0f, 2.0f, -5.0f);
		DirectX::XMFLOAT3 target(0.0f, 0.0f, 0.0f);
		m_camera.SetLookAt(m_cameraPosition, target, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
		m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 5000.0f);

		// 9) 스크립트 DLL (라이브 코딩용) 로드 시도
		ScriptHotReload_Load();
		ALICE_LOG_INFO("Engine::Initialize: ScriptHotReload_Load called.");

		// 10) 씬 매니저 생성 및 기본 씬/씬 파일 로드
		m_resourceManager.Clear();
		m_sceneManager = std::make_unique<SceneManager>(m_world, m_resourceManager);
		ALICE_LOG_INFO("Engine::Initialize: SceneManager created.");

		// 에디터 모드: 코드 기반 SampleScene 을 기본으로 사용
		if (m_editorMode)
		{
			m_sceneManager->SwitchTo("SampleScene");
			ALICE_LOG_INFO("Engine::Initialize: editor mode, switched to SampleScene.");
		}
		else
		{
			// 게임 모드: BuildSettings.txt 에 정의된 0번 인덱스 씬(.scene)을 우선 로드
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			std::filesystem::path exePath = exePathW;
			std::filesystem::path exeDir = exePath.parent_path();

			if (!LoadStartupSceneFromBuildSettings(m_world, exeDir))
			{
				// 실패 시 최후의 수단으로 SampleScene 을 사용
				m_sceneManager->SwitchTo("SampleScene");
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

		const auto& transforms = m_world.GetTransforms();
		const auto& skinnedMeshes = m_world.GetSkinnedMeshes();
		const auto& scripts = m_world.GetScripts();
		const auto& materials = m_world.GetMaterials();
		ALICE_LOG_INFO("Engine::Initialize: world summary: transforms=%zu, skinnedMeshes=%zu, scripts=%zu, materials=%zu",
			transforms.size(), skinnedMeshes.size(), scripts.size(), materials.size());

		ALICE_LOG_INFO("Engine::Initialize: success.");
		return true;
	}

	int Engine::Run()
	{
		m_isRunning = true;

		MSG msg = {};

		// 고해상도 타이머 초기화
		m_timer.Reset();
		m_timer.Start();

		// 기본 게임 루프
		while (m_isRunning)
		{
			// 1) 윈도우 메시지 처리
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					m_isRunning = false;
					break;
				}

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (!m_isRunning) break;

			Update();
			Render();
		}

		return static_cast<int>(msg.wParam);
	}

	void Engine::Update()
	{
		m_timer.Tick();
		m_inputSystem.Update(m_timer.DeltaTime());

		using namespace DirectX;

		// 1) 카메라 이동 (WASD + Q/E) - 오른쪽 마우스 버튼을 누르고 있을 때만 동작
		const bool canControlCamera = m_inputSystem.IsRightButtonDown(); // 우클릭 상태에서만 이동/회전

		XMVECTOR moveDir = XMVectorZero();

		if (canControlCamera)
		{
			if (m_inputSystem.IsKeyDown(Keyboard::W))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
			}
			if (m_inputSystem.IsKeyDown(Keyboard::S))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
			}
			if (m_inputSystem.IsKeyDown(Keyboard::D))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			}
			if (m_inputSystem.IsKeyDown(Keyboard::A))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));
			}
			// E: 위로, Q: 아래로 이동
			if (m_inputSystem.IsKeyDown(Keyboard::E))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
			}
			if (m_inputSystem.IsKeyDown(Keyboard::Q))
			{
				moveDir = XMVectorAdd(moveDir, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
			}

			if (!XMVector3Equal(moveDir, XMVectorZero()))
			{
				// 카메라의 현재 회전에 맞춰 이동 벡터를 회전
				XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
				XMVECTOR worldMoveDir = XMVector3TransformNormal(moveDir, rotMatrix);
				worldMoveDir = XMVector3Normalize(worldMoveDir);

				XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
				pos = XMVectorAdd(pos, XMVectorScale(worldMoveDir, m_cameraMoveSpeed * m_timer.DeltaTime()));
				XMStoreFloat3(&m_cameraPosition, pos);
			}

			// 2) 마우스 이동으로 카메라 회전 (우클릭 상태에서만)
			POINT mouseDelta = m_inputSystem.GetMouseDelta();
			m_cameraYawRadians += static_cast<float>(mouseDelta.x) * m_cameraMouseSensitivity;
			// 마우스를 아래로 내리면 화면도 아래를 보도록 Y축 회전을 반대로 적용합니다.
			m_cameraPitchRadians += static_cast<float>(mouseDelta.y) * m_cameraMouseSensitivity;
		}

		// 피치 각도는 -89 ~ 89도 사이로 제한
		const float pitchLimit = XMConvertToRadians(89.0f);
		if (m_cameraPitchRadians > pitchLimit)  m_cameraPitchRadians = pitchLimit;
		if (m_cameraPitchRadians < -pitchLimit) m_cameraPitchRadians = -pitchLimit;

		// 3) 카메라 LookAt 갱신
		XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
		XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);

		XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
		XMVECTOR target = XMVectorAdd(pos, forward);

		XMFLOAT3 targetFloat3;
		XMStoreFloat3(&targetFloat3, target);

		m_camera.SetLookAt(m_cameraPosition, targetFloat3, XMFLOAT3(0.0f, 1.0f, 0.0f));

		// 4) 현재 씬 및 스크립트 업데이트
		//    - 에디터 모드: Play 버튼이 눌렸을 때만 진행
		//    - 게임 전용 모드: 항상 진행
		const bool play = m_editorMode ? m_isPlaying : true;
		if (play)
		{
			if (m_sceneManager)
			{
				m_sceneManager->Update(m_timer.DeltaTime());
			}

			// 엔티티에 붙어 있는 모든 ScriptComponent 를 갱신합니다.
			m_scriptSystem.Update(m_world, m_timer.DeltaTime());
		}
	}

	void Engine::Render()
	{
		if (!m_renderDevice || !m_forwardRenderSystem)
			return;

		// 화면 클리어 색상 (짙은 파란색 계열)
		const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

		m_renderDevice->BeginFrame(clearColor);

		// 에디터 모드에서만 ImGui/도킹 UI + 디버그 축을 그립니다.
		if (m_editorMode)
		{
			// ImGui 프레임 시작 (EditorCore 에 위임)
			m_editorCore.BeginFrame();

			const float dt = m_timer.DeltaTime();
			const float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
			int shadingModeValue = static_cast<int>(m_shadingMode);
			m_editorCore.DrawEditorUI(
				m_world,
				m_camera,
				*m_forwardRenderSystem,
				m_sceneManager.get(),
				dt,
				fps,
				m_isPlaying,
				shadingModeValue,
				m_useFillLight,
				m_selectedEntity,
				m_viewportPicker,
				m_cameraMoveSpeed);
			m_shadingMode = static_cast<ShadingMode>(shadingModeValue);

			// DebugDraw 라인 초기화 및 예제 축(axis) 추가
			if (m_debugDrawSystem)
			{
				m_debugDrawSystem->Clear();

				// 원점에서 XYZ 축을 그립니다.
				// X: 빨강, Y: 초록, Z: 파랑
				m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
				m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f),
					DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f));
				m_debugDrawSystem->AddLine(
					DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f),
					DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f),
					DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f));
			}
		}

		// 스키닝 메시 드로우 리스트를 먼저 구성합니다.
		m_skinnedMeshSystem.BuildDrawList(m_world, m_skinnedDrawCommands);

		// 간단한 Forward 렌더링 (큐브 + 스키닝 메시)
		EntityId renderEntity = InvalidEntityId;
		if (m_sceneManager)
		{
			renderEntity = m_sceneManager->GetPrimaryRenderableEntity();
		}

		const int shadingModeValue2 = static_cast<int>(m_shadingMode);
		m_forwardRenderSystem->Render(
			m_world,
			m_camera,
			renderEntity,
			shadingModeValue2,
			m_useFillLight,
			m_skinnedDrawCommands);

		{
			auto* ctx = m_renderDevice->GetImmediateContext();

			auto* backBufferRTV = m_renderDevice->GetBackBufferRTV();

			// SRV/RTV 에서 리소스 꺼내기
			Microsoft::WRL::ComPtr<ID3D11Resource> src;
			Microsoft::WRL::ComPtr<ID3D11Resource> dst;

			// src: ForwardRenderSystem 의 컬러 텍스처
			auto* sceneSRV = m_forwardRenderSystem->GetSceneSRV();
			sceneSRV->GetResource(src.GetAddressOf());

			// dst: 백버퍼 텍스처
			backBufferRTV->GetResource(dst.GetAddressOf());

			// 실제 복사
			ctx->CopyResource(dst.Get(), src.Get());
		}

		// DebugDraw 렌더링 (Forward 렌더 이후, 같은 카메라 기준)
		if (m_debugDrawSystem)
		{
			m_debugDrawSystem->Render(m_camera);
		}

		// ImGui 렌더링 (에디터 모드에서만)
		if (m_editorMode)
		{
			m_editorCore.RenderDrawData();
		}

		m_renderDevice->EndFrame();
	}

	void Engine::EnsureSkinnedMeshesRegisteredForWorld()
	{
		if (!m_renderDevice)
			return;

		auto* device = m_renderDevice->GetDevice();
		if (!device)
			return;

		const auto& skinnedMap = m_world.GetSkinnedMeshes();
		if (skinnedMap.empty())
		{
			ALICE_LOG_INFO("Engine::EnsureSkinnedMeshesRegisteredForWorld: no SkinnedMeshComponents in world.");
			return;
		}

		for (const auto& [entityId, comp] : skinnedMap)
		{
			if (comp.meshAssetPath.empty())
				continue;

			if (m_skinnedMeshRegistry.Find(comp.meshAssetPath))
				continue; // 이미 등록됨

			std::filesystem::path fbxAssetPath;
			if (!comp.instanceAssetPath.empty())
			{
				fbxAssetPath = comp.instanceAssetPath;
			}
			else
			{
				// 논리 경로(Assets/...)만 저장/사용하고, 실제 파일 경로는 ResourceManager 가 해석합니다.
				fbxAssetPath = std::filesystem::path("Assets/Fbx")
					/ (comp.meshAssetPath + ".fbxasset");
			}

			Alice::FbxInstanceAsset instance{};
			const std::filesystem::path fbxAssetAbs = m_resourceManager.Resolve(fbxAssetPath);
			if (!Alice::LoadFbxInstanceAsset(fbxAssetAbs, instance))
			{
				ALICE_LOG_WARN("Engine::EnsureSkinnedMeshesRegisteredForWorld: failed to load .fbxasset \"%s\" for meshKey=\"%s\"",
					fbxAssetAbs.string().c_str(),
					comp.meshAssetPath.c_str());
				continue;
			}

			if (instance.sourceFbx.empty())
			{
				ALICE_LOG_WARN("Engine::EnsureSkinnedMeshesRegisteredForWorld: .fbxasset has empty source_fbx for \"%s\"",
					fbxAssetPath.string().c_str());
				continue;
			}

			FbxImportOptions opt{};
			FbxImporter importer(m_resourceManager, &m_skinnedMeshRegistry);

			// source_fbx 는 "Assets/..." 같은 논리 경로일 수 있으므로 Resolve 로 변환합니다.
			std::filesystem::path srcFbxPath = m_resourceManager.Resolve(instance.sourceFbx);
			FbxImportResult result = importer.Import(device, srcFbxPath, opt);

			ALICE_LOG_INFO("Engine::EnsureSkinnedMeshesRegisteredForWorld: re-import FBX \"%s\" -> meshKey=\"%s\" result.mesh=\"%s\"",
				srcFbxPath.string().c_str(),
				comp.meshAssetPath.c_str(),
				result.meshAssetPath.c_str());
		}
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
		wc.hInstance = m_hInstance;
		// 엔진 전용 아이콘을 로드합니다. (실패하면 기본 아이콘을 사용)
		const std::filesystem::path iconAbs = m_resourceManager.Resolve("Resource/Icon/Alice.ico");
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
		RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		const int windowWidth = windowRect.right - windowRect.left;
		const int windowHeight = windowRect.bottom - windowRect.top;

		// 3) 윈도우 생성 (this 포인터를 lpParam으로 전달)
		m_hWnd = CreateWindowExW(
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
			m_hInstance,
			this
		);

		if (!m_hWnd) return false;

		ShowWindow(m_hWnd, nCmdShow);
		UpdateWindow(m_hWnd);

		return true;
	}

	void Engine::OnResize(std::uint32_t width, std::uint32_t height)
	{
		m_width = width;
		m_height = height;

		if (m_renderDevice)
		{
			m_renderDevice->Resize(width, height);

			const float aspect = (height != 0)
				? static_cast<float>(width) / static_cast<float>(height)
				: 1.0f;
			m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
		}

		if (m_forwardRenderSystem)
		{
			m_forwardRenderSystem->Resize(width, height);
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
			m_isRunning = false;
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