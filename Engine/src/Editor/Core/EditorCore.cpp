#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"
#include "Editor/Core/EditorUndoRedo.h"

#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Editor/Core/ReflectionUI.h"
#include "Runtime/ECS/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Runtime/ECS/EditorComponentRegistry.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Resources/Serialization/SocketSerialization.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UIRenderer.h"
#include "Runtime/UI/UICurveAsset.h"
#include <cstdint>
#include <cstdio>
#include <set>
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/PostProcessSettings.h"
#include "Editor/Tools/Blueprint/AnimBlueprintEditor.h"
#include "Runtime/Gameplay/Combat/CombatPhysicsLayers.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <iterator>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "ImGuizmo.h"

#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <sstream>
#include <Runtime/Resources/Prefab.h>
#include <Runtime/Scripting/IScript.h>
#include <Runtime/Scripting/ScriptSystem.h>
#include <Runtime/Scripting/ScriptFactory.h>
#include <Runtime/Rendering/Data/Material.h>
#include <Runtime/Resources/SceneFile.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ShlObj.h>   // 폴더 선택 다이얼로그 (SHBrowseForFolderW)
#include <Runtime/Importing/FbxAsset.h>
#include "ThirdParty/json/json.hpp"

// 텍스처 로딩용 DirectXTK
#include <DirectXTK/WICTextureLoader.h>
#include "Runtime/ECS/Components/TransformComponent.h"

using namespace DirectX;

namespace Alice
{
	// 씬 상태 전역 - 여러 네임스페이스에서 공유
	bool g_SceneDirty = false;
	bool g_HasCurrentScenePath = false;
	std::filesystem::path g_CurrentScenePath{};
	bool g_ShowBuildGameWindow = false;
	bool g_ShowPvdSettingsWindow = false;
	bool g_RequestSceneLoad = false;
	std::filesystem::path g_NextScenePath{};
	bool g_ShowSceneLoadError = false;
	std::string g_SceneLoadErrorMsg{};
	bool g_MaterialEditorOpen = false;
	std::filesystem::path g_MaterialEditorPath{};
	MaterialComponent g_MaterialEditorData{};
	bool g_UICurveEditorOpen = false;
	std::filesystem::path g_UICurveEditorPath{};
	UICurveAsset g_UICurveEditorData{};
	int g_UICurveEditorSelected = -1;

	// ICommand는 이제 EditorCore.h에 정의됨

	namespace
	{
		// Undo/Redo 스택 관리
		std::vector<std::unique_ptr<ICommand>> g_UndoStack;
		std::vector<std::unique_ptr<ICommand>> g_RedoStack;
		constexpr size_t MAX_UNDO_STACK_SIZE = 50;

		// PushCommand는 EditorCore 클래스의 멤버 함수로 이동됨

		struct ScopedHandle
		{
			HANDLE h = nullptr;
			ScopedHandle() = default;
			explicit ScopedHandle(HANDLE handle) : h(handle) {}
			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;
			ScopedHandle(ScopedHandle&& other) noexcept : h(other.h) { other.h = nullptr; }
			ScopedHandle& operator=(ScopedHandle&& other) noexcept
			{
				if (this != &other)
				{
					if (h) CloseHandle(h);
					h = other.h;
					other.h = nullptr;
				}
				return *this;
			}
			~ScopedHandle() { if (h) CloseHandle(h); }
		};

		// 명령어를 실행하고 Exit Code를 반환하는 함수임
		int ExecuteCommandWithConsole(const std::wstring& command)
		{
			STARTUPINFOW si;
			PROCESS_INFORMATION pi;

			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));

			// cmd.exe /C 를 앞에 붙여서 실행해야 쉘 명령어(cmake 등)가 인식됨
			// 전체 명령어를 " "로 감싸서 공백이나 특수문자 문제를 방지합니다.
			std::wstring finalCmd = L"cmd.exe /C \"" + command + L"\"";

			// CreateProcess는 문자열 버퍼를 수정할 수 있어야 하므로 vector에 복사
			std::vector<wchar_t> cmdBuffer(finalCmd.begin(), finalCmd.end());
			cmdBuffer.push_back(0); // Null terminator

			// CreateProcess 실행
			// CREATE_NEW_CONSOLE: 부모가 GUI라도 무조건 새 콘솔창을 띄움
			BOOL result = CreateProcessW(
				NULL,                   // 어플리케이션 이름 (NULL이면 커맨드라인에서 파싱)
				cmdBuffer.data(),       // 커맨드 라인
				NULL,                   // 프로세스 보안 속성
				NULL,                   // 스레드 보안 속성
				FALSE,                  // 핸들 상속 여부
				CREATE_NEW_CONSOLE,     // 새 콘솔 창 생성 플래그
				NULL,                   // 환경 변수 (NULL이면 부모 상속)
				NULL,                   // 현재 디렉토리 (NULL이면 부모와 동일)
				&si,                    // 시작 정보
				&pi                     // 프로세스 정보 (핸들 등)
			);

			if (!result)
			{
				// 실행 자체 실패
				return -1;
			}

			// 프로세스가 끝날 때까지 대기
			WaitForSingleObject(pi.hProcess, INFINITE);

			// 종료 코드(Exit Code) 가져오기
			DWORD exitCode = 0;
			GetExitCodeProcess(pi.hProcess, &exitCode);

			// 핸들 닫기
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);

			return static_cast<int>(exitCode);
		}

		/// 에디터 Reload Scripts 버튼에서 호출하는 헬퍼입니다.
		/// - ScriptsBuild CMake 프로젝트를 configure/build 해서 AliceScripts.dll 을 만들고
		///   현재 실행 중인 exe 옆으로 복사한 뒤 ScriptHotReload_Reload 를 호출합니다.
		struct ScriptReloadSnap
		{
			std::string name;
			bool enabled{};
			nlohmann::json props;
		};

		struct EntityReloadSnap
		{
			EntityId id{};
			std::vector<ScriptReloadSnap> scripts;
		};

		static void SnapshotAndDestroyScripts(World& world, std::vector<EntityReloadSnap>& out)
		{
			out.clear();

			auto& map = world.GetAllScriptsInWorld();
			out.reserve(map.size());

			for (auto& [id, list] : map)
			{
				EntityReloadSnap e{};
				e.id = id;
				e.scripts.reserve(list.size());

				for (auto& sc : list)
				{
					ScriptReloadSnap s{};
					s.name = sc.scriptName;
					s.enabled = sc.enabled;

					if (sc.instance && !sc.scriptName.empty())
					{
						rttr::type t = rttr::type::get_by_name(sc.scriptName);
						s.props = JsonRttr::ToJsonObject(*sc.instance, t);

						// DLL이 살아있는 동안 가상함수 호출해서 정리
						sc.instance->OnDisable();
						sc.instance->OnDestroy();
						sc.instance.reset();
					}

					sc.awoken = false;
					sc.started = false;
					sc.wasEnabled = sc.enabled;
					sc.defaultsApplied = false;

					e.scripts.push_back(std::move(s));
				}
				out.push_back(std::move(e));
			}
		}

		static void RestoreScripts(World& world, const std::vector<EntityReloadSnap>& snaps)
		{
			auto& map = world.GetAllScriptsInWorld();

			for (const auto& e : snaps)
			{
				auto it = map.find(e.id);
				if (it == map.end())
					continue;

				std::vector<ScriptComponent> rebuilt;
				rebuilt.reserve(e.scripts.size());

				for (const auto& s : e.scripts)
				{
					if (s.name.empty())
						continue;

					ScriptComponent sc{};
					sc.scriptName = s.name;
					sc.enabled = s.enabled;
					sc.instance = ScriptFactory::Create(s.name.c_str());
					if (!sc.instance)
						continue;

					// 컨텍스트 주입 (필수): World와 EntityId 설정
					sc.instance->SetContext(&world, e.id);

					rttr::instance inst = *sc.instance;
					rttr::type t = rttr::type::get_by_name(sc.scriptName);
					JsonRttr::FromJsonObject(inst, s.props, t);

					sc.defaultsApplied = true;
					rebuilt.push_back(std::move(sc));
				}

				it->second = std::move(rebuilt);
				if (it->second.empty())
					map.erase(it);
			}
		}

		bool ReloadScripts_FromButton(World& world)
		{
			using namespace std::filesystem;

			// 1) 실행 파일 위치 기준으로 프로젝트 루트 / ScriptsBuild 경로 계산
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			path exePath = exePathW;
			path exeDir = exePath.parent_path();
			path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
			path scriptsRoot = projectRoot / "ScriptsBuild";
			path scriptsCMakePath = scriptsRoot / "CMakeLists.txt";
			path scriptsBuildDir = scriptsRoot / "build";

			if (!exists(scriptsCMakePath))
			{
				ALICE_LOG_ERRORF("Reload Scripts: ScriptsBuild/CMakeLists.txt not found. path=\"%s\"",
					(scriptsCMakePath).string().c_str());
				return false;
			}

#ifdef _DEBUG
			constexpr const wchar_t* kConfig = L"Debug";
#else
			constexpr const wchar_t* kConfig = L"Release";
#endif

			// ----------------------------------------------------------------------
			// 1: Configure 명령어 (cmd.exe /C는 ExecuteCommandWithConsole에서 처리)
			// ----------------------------------------------------------------------
			std::wstring cmdConfig = L"cmake -S \"";
			cmdConfig += scriptsRoot.wstring();
			cmdConfig += L"\" -B \"";
			cmdConfig += scriptsBuildDir.wstring();
			cmdConfig += L"\"";

			// Configure 실행
			int configResult = ExecuteCommandWithConsole(cmdConfig);
			if (configResult != 0)
			{
				ALICE_LOG_ERRORF("Reload Scripts: CMake Configure failed (exit code: %d).", configResult);
				// 실패 시에만 pause 실행 (사용자가 에러를 볼 수 있도록)
				ExecuteCommandWithConsole(L"pause");
				return false;
			}

			// ----------------------------------------------------------------------
			// 2: Build 명령어 (cmd.exe /C는 ExecuteCommandWithConsole에서 처리)
			// ----------------------------------------------------------------------
			std::wstring cmdBuild = L"cmake --build \"";
			cmdBuild += scriptsBuildDir.wstring();
			cmdBuild += L"\" --config ";
			cmdBuild += kConfig;
			cmdBuild += L" --target AliceScripts";

			// Build 실행
			int buildResult = ExecuteCommandWithConsole(cmdBuild);
			if (buildResult != 0)
			{
				ALICE_LOG_ERRORF("Reload Scripts: CMake Build failed (exit code: %d).", buildResult);
				// 실패 시에만 pause 실행 (사용자가 에러를 볼 수 있도록)
				ExecuteCommandWithConsole(L"pause");
				return false;
			}

			// 4) ScriptsBuild/build/<Config>/AliceScripts.dll 을 실행 파일 옆으로 복사
			path builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
			if (!exists(builtDll))
			{
				ALICE_LOG_ERRORF("Reload Scripts: built DLL not found: \"%s\"",
					builtDll.string().c_str());
				return false;
			}

			// RTTR shared DLL도 같이 복사해 둡니다. (스크립트 RTTR 등록이 엔진에서 보이려면 필수)
			// - ScriptsBuild는 자체적으로 rttr_core.dll을 빌드합니다.
			// - 실행 파일 폴더에 하나만 존재하면, EXE/DLL이 같은 registry를 공유합니다.
			{
				path builtRttr = scriptsBuildDir / path(kConfig) / "rttr_core.dll";
				if (exists(builtRttr))
				{
					std::error_code ecRttr;
					copy_file(builtRttr, exeDir / "rttr_core.dll",
						copy_options::overwrite_existing,
						ecRttr);
					if (ecRttr)
					{
						ALICE_LOG_WARN("Reload Scripts: failed to copy rttr_core.dll (%s)",
							ecRttr.message().c_str());
					}
				}
			}

			// 기존 DLL을 언로드하기 전에, 기존 스크립트 인스턴스(가상 함수)가 남아있으면 크래시가 납니다.
			// - 값은 스냅샷 후 새 DLL 로드 뒤에 다시 주입합니다.
			std::vector<EntityReloadSnap> snaps;
			SnapshotAndDestroyScripts(world, snaps);

			ScriptHotReload_Unload();

			path targetDll = exeDir / "AliceScripts.dll";
			std::error_code ecCopy;
			copy_file(builtDll, targetDll,
				copy_options::overwrite_existing,
				ecCopy);
			if (ecCopy)
			{
				ALICE_LOG_ERRORF("Reload Scripts: failed to copy DLL from \"%s\" to \"%s\" (%s)",
					builtDll.string().c_str(),
					targetDll.string().c_str(),
					ecCopy.message().c_str());
				return false;
			}

			ALICE_LOG_INFO("Reload Scripts: copied \"%s\" -> \"%s\"",
				builtDll.string().c_str(),
				targetDll.string().c_str());

			// 6) 새 DLL 로드
			if (!ScriptHotReload_Reload())
			{
				ALICE_LOG_ERRORF("Reload Scripts: ScriptHotReload_Reload() failed.");
				return false;
			}

			// 새 DLL의 vtable/RTTR이 준비된 뒤에 인스턴스를 다시 만듭니다.
			RestoreScripts(world, snaps);
			return true;
		}

	}

	bool ExecuteUndo(World& world, EntityId& selectedEntity)
	{
		if (g_UndoStack.empty())
			return false;

		auto cmd = std::move(g_UndoStack.back());
		g_UndoStack.pop_back();

		cmd->Undo(world, selectedEntity);

		// Redo 지원하는 커맨드만 Redo 스택에 추가
		if (cmd->SupportsRedo())
		{
			g_RedoStack.push_back(std::move(cmd));
			if (g_RedoStack.size() > MAX_UNDO_STACK_SIZE)
			{
				g_RedoStack.erase(g_RedoStack.begin());
			}
		}
		// else: Redo 불가 커맨드는 버림

		g_SceneDirty = true;
		return true;
	}

	bool ExecuteRedo(World& world, EntityId& selectedEntity)
	{
		if (g_RedoStack.empty())
			return false;

		auto cmd = std::move(g_RedoStack.back());
		g_RedoStack.pop_back();

		cmd->Execute(world, selectedEntity);

		// Undo 스택에 다시 추가
		g_UndoStack.push_back(std::move(cmd));
		if (g_UndoStack.size() > MAX_UNDO_STACK_SIZE)
		{
			g_UndoStack.erase(g_UndoStack.begin());
		}

		g_SceneDirty = true;
		return true;
	}

	void ClearUndoStack()
	{
		g_UndoStack.clear();
		g_RedoStack.clear();
	}

	EditorCore::~EditorCore()
	{
		Shutdown();
	}

	bool EditorCore::Initialize(HWND hwnd, ID3D11RenderDevice& renderDevice)
	{
		if (m_initialized)
			return true;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// 폰트 아틀라스를 모두 지우고, 한글/일본어를 포함한 폰트를 기본 폰트로 사용합니다.
		io.Fonts->Clear();

		ImFontConfig baseConfig{};
		baseConfig.MergeMode = false;
		const std::wstring fontKr =
			ResourceManager::Get().Resolve("Resource/Fonts/NotoSansKR-Regular.ttf").wstring();
		io.FontDefault = io.Fonts->AddFontFromFileTTF(
			Utf8FromWString(fontKr).c_str(),
			18.0f,
			&baseConfig,
			io.Fonts->GetGlyphRangesKorean());

		ImFontConfig jpConfig{};
		jpConfig.MergeMode = true;
		jpConfig.PixelSnapH = true;
		const std::wstring fontJp =
			ResourceManager::Get().Resolve("Resource/Fonts/meiryo.ttc").wstring();
		io.Fonts->AddFontFromFileTTF(
			Utf8FromWString(fontJp).c_str(),
			18.0f,
			&jpConfig,
			io.Fonts->GetGlyphRangesJapanese());

		m_hwnd = hwnd;
		m_renderDevice = &renderDevice;

		auto* d3dDevice = renderDevice.GetDevice();
		auto* d3dContext = renderDevice.GetImmediateContext();

		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(d3dDevice, d3dContext);
		ImGui_ImplDX11_CreateDeviceObjects();

		if (m_aliceUIRenderer && io.FontDefault && io.Fonts)
		{
			const ImTextureID texId = io.Fonts->TexID.GetTexID();
			if (texId != ImTextureID_Invalid)
			{
				m_aliceUIRenderer->SetDefaultImGuiFont(io.FontDefault,
					reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<uintptr_t>(texId)));
			}
		}

		// ImGuizmo 스타일 설정
		ImGuizmo::Style& style = ImGuizmo::GetStyle();
		style.RotationLineThickness = 3.0f;
		style.RotationOuterLineThickness = 2.0f;

		// Default PostProcess Settings 초기화 및 로드
		m_defaultPostProcessSettings = PostProcessSettings::FromDefaults();
		LoadDefaultPostProcessSettings();

		m_initialized = true;
		return true;
	}

	void EditorCore::SetAliceUIRenderer(UIRenderer* renderer)
	{
		m_aliceUIRenderer = renderer;
		if (!m_initialized || !m_aliceUIRenderer)
			return;

		ImGuiIO& io = ImGui::GetIO();

		if (io.FontDefault && io.Fonts)
		{
			const ImTextureID texId = io.Fonts->TexID.GetTexID();
			if (texId != ImTextureID_Invalid)
			{
				m_aliceUIRenderer->SetDefaultImGuiFont(io.FontDefault,
					reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<uintptr_t>(texId)));
			}
		}
	}

	void EditorCore::Shutdown()
	{
		if (!m_initialized)
			return;

		if (ImGui::GetCurrentContext() != nullptr)
		{
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}

		m_initialized = false;
	}

	void EditorCore::BeginFrame()
	{
		if (!m_initialized)
			return;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void EditorCore::RenderDrawData()
	{
		if (!m_initialized)
			return;

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	void EditorCore::HandleGlobalUndoRedo(World& world, EntityId& selectedEntity, bool isPlaying)
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool isTextInputActive = io.WantTextInput || ImGui::IsAnyItemActive();

		if (m_inputSystem && !isTextInputActive && !isPlaying)
		{
			const bool ctrlDown = m_inputSystem->IsKeyDown(Keyboard::Keys::LeftControl) ||
				m_inputSystem->IsKeyDown(Keyboard::Keys::RightControl);

			if (ctrlDown && m_inputSystem->IsKeyPressed(Keyboard::Keys::Z))
			{
				ExecuteUndo(world, selectedEntity);
			}
			else if (ctrlDown && m_inputSystem->IsKeyPressed(Keyboard::Keys::Y))
			{
				ExecuteRedo(world, selectedEntity);
			}
		}
	}

	void EditorCore::DrawEditorUI(World& world,
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
		bool& useForwardRendering,
		bool& pvdEnabled,
		std::string& pvdHost,
		int& pvdPort,
		bool& isDebugDraw)
	{
		// 매 프레임 Default PostProcess Settings를 RenderSystem에 전달
		deferred.SetDefaultPostProcessSettings(m_defaultPostProcessSettings);
		// ForwardRenderSystem에도 동일한 함수가 필요하면 추가
		// forward.SetDefaultPostProcessSettings(m_defaultPostProcessSettings);

		// SceneManager에서 현재 씬 파일 경로를 조회하여 g_CurrentScenePath 업데이트
		if (sceneManager)
		{
			const auto& currentScenePath = sceneManager->GetCurrentSceneFilePath();
			if (!currentScenePath.empty() && currentScenePath != g_CurrentScenePath)
			{
				g_CurrentScenePath = currentScenePath;
				g_HasCurrentScenePath = true;
			}
		}

		HandleGlobalUndoRedo(world, selectedEntity, isPlaying);
		SetupDockSpaceAndDefaultLayout();
		DrawMainMenuBar(world, deltaTime, fps, isPlaying, selectedEntity, useForwardRendering, isDebugDraw);

		DrawPvdSettingsWindow(pvdEnabled, pvdHost, pvdPort);
		DrawBuildGameWindow();

		DrawHierarchyWindow(world, selectedEntity);
		DrawInspectorWindow(world, selectedEntity);
		DrawProjectWindow(world, selectedEntity);
		DrawGameViewportWindow(world, camera, forward, deferred, selectedEntity, picker, cameraMoveSpeed, useForwardRendering, isPlaying, shadingMode, useFillLight);
		DrawCameraWindow(world, camera, cameraMoveSpeed, selectedEntity);
		DrawLightingWindow(world, forward, deferred, shadingMode, useFillLight, useForwardRendering);

		DrawMaterialAssetEditorWindow(world);
		DrawUICurveAssetEditorWindow();

		HandleSceneLoadFlow(world, sceneManager, isPlaying, selectedEntity);
	}

																					// =========================================================================================
	// Camera 컴포넌트 인스펙터 함수들
	// =========================================================================================

							void EditorCore::DrawDirectoryNode(World& world,
		EntityId& selectedEntity,
		const std::filesystem::path& path)
	{
		namespace fs = std::filesystem;
		if (!fs::exists(path)) return;

		const bool        isDirectory = fs::is_directory(path);
		const std::string label = path.filename().string();

		ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth;

		// 파일/폴더 이름 변경 상태를 관리하는 간단한 정적 상태입니다.
		static bool                 s_renaming = false;
		static std::filesystem::path s_renamingPath;
		static char                 s_renameBuffer[260] = {};
		static bool                 s_renameFocus = false;

		// 공통 Rename 상태: 파일/폴더 모두 이 플래그를 사용합니다.
		const bool isRenamingThis = s_renaming && (s_renamingPath == path);

		if (isDirectory)
		{
			bool open = false;

			// 폴더 이름 영역: 일반 텍스트 또는 인라인 입력 박스
			ImGui::PushID(label.c_str());
			if (isRenamingThis)
			{
				ImGui::SetNextItemWidth(-1.0f);
				if (s_renameFocus)
				{
					ImGui::SetKeyboardFocusHere();
					s_renameFocus = false;
				}

				bool enterPressed = ImGui::InputText(
					"##RenameFolder",
					s_renameBuffer,
					sizeof(s_renameBuffer),
					ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

				bool finished = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
				if (finished)
				{
					if (std::strlen(s_renameBuffer) > 0)
					{
						fs::path newPath = path.parent_path() / s_renameBuffer;
						if (!fs::exists(newPath))
						{
							std::error_code ec;
							fs::rename(path, newPath, ec);
						}
					}
					s_renaming = false;
				}
			}
			else
			{
				open = ImGui::TreeNodeEx(label.c_str(), baseFlags);
			}
			ImGui::PopID();

			// 디렉터리 노드에 대한 우클릭 컨텍스트 메뉴 (폴더/스크립트/프리팹 생성 등)
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Rename Folder..."))
				{
					std::string folderName = path.filename().string();
					std::memset(s_renameBuffer, 0, sizeof(s_renameBuffer));
					strncpy_s(s_renameBuffer,
						sizeof(s_renameBuffer),
						folderName.c_str(),
						_TRUNCATE);
					s_renaming = true;
					s_renamingPath = path;
					s_renameFocus = true;
					ImGui::CloseCurrentPopup();
				}

				// 새 하위 폴더 생성
				if (ImGui::MenuItem("Create Folder"))
				{
					fs::path newPath = path / "NewFolder";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewFolder" + std::to_string(index) + "");
						++index;
					}

					std::error_code ec;
					fs::create_directories(newPath, ec);
				}

				// 새 Material 파일 생성
				if (ImGui::MenuItem("Create Material"))
				{
					const std::string baseName = "NewMaterial";
					fs::path matPath = path / (baseName + ".mat");

					int index = 1;
					while (fs::exists(matPath))
					{
						matPath = path / (baseName + std::to_string(index) + ".mat");
						++index;
					}

					// 기본 MaterialComponent 생성 및 저장
					MaterialComponent defaultMat;
					defaultMat.color = DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
					defaultMat.roughness = 0.5f;
					defaultMat.metalness = 0.0f;
					defaultMat.shadingMode = -1; // Global

					if (MaterialFile::Save(matPath, defaultMat))
					{
						ALICE_LOG_INFO("[EditorCore] Created new Material file: %s", matPath.string().c_str());
					}
					else
					{
						ALICE_LOG_ERRORF("[EditorCore] Failed to create Material file: %s", matPath.string().c_str());
					}
				}

				// 새 UI Curve Asset 생성
				if (ImGui::MenuItem("Create CurveAsset"))
				{
					const std::string baseName = "NewCurve";
					fs::path curvePath = path / (baseName + ".uicurve");

					int index = 1;
					while (fs::exists(curvePath))
					{
						curvePath = path / (baseName + std::to_string(index) + ".uicurve");
						++index;
					}

					UICurveAsset asset;
					asset.name = curvePath.stem().string();
					asset.keys.push_back({ 0.0f, 0.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					asset.keys.push_back({ 1.0f, 1.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					asset.Sort();
					asset.RecalcAutoTangents();

					if (SaveUICurveAsset(curvePath, asset))
					{
						ALICE_LOG_INFO("[EditorCore] Created new Curve asset: %s", curvePath.string().c_str());
					}
					else
					{
						ALICE_LOG_ERRORF("[EditorCore] Failed to create Curve asset: %s", curvePath.string().c_str());
					}
				}

				// Unity 스타일: C++ 스크립트(.h/.cpp)와 프리팹을 간단하게 생성합니다.
				if (ImGui::MenuItem("Create C++ Script"))
				{
					const std::string baseName = "NewScript";

					fs::path headerPath = path / (baseName + ".h");
					fs::path sourcePath = path / (baseName + ".cpp");

					int index = 1;
					while (fs::exists(headerPath) || fs::exists(sourcePath))
					{
						const std::string numbered = baseName + std::to_string(index);
						headerPath = path / (numbered + ".h");
						sourcePath = path / (numbered + ".cpp");
						++index;
					}

					const std::string className = headerPath.stem().string();

					// 헤더 파일 템플릿 작성
					{
						std::ofstream hfs(headerPath);
						if (hfs.is_open())
						{
							hfs << "#pragma once\n\n";
							hfs << "#include \"Runtime/Scripting/IScript.h\"\n";
							hfs << "#include \"Runtime/Scripting/ScriptReflection.h\"\n\n";
							hfs << "namespace Alice\n";
							hfs << "{\n";
							hfs << "    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.\n";
							hfs << "    class " << className << " : public IScript\n";
							hfs << "    {\n";
							hfs << "        ALICE_BODY(" << className << ");\n\n";
							hfs << "    public:\n";
							hfs << "        void Start() override;\n";
							hfs << "        void Update(float deltaTime) override;\n\n";
							hfs << "        // --- 변수 리플렉션 예시 (에디터에서 수정 가능) ---\n";
							hfs << "        ALICE_PROPERTY(float, m_exampleValue, 1.0f);\n\n";
							hfs << "        // --- 함수 리플렉션 예시 ---\n";
							hfs << "        void ExampleFunction();\n";
							hfs << "        ALICE_FUNC(ExampleFunction);\n";
							hfs << "    };\n";
							hfs << "}\n";
						}
					}

					// cpp 파일 템플릿 작성
					{
						std::ofstream cfs(sourcePath);
						if (cfs.is_open())
						{
							cfs << "#include \"" << headerPath.filename().string() << "\"\n";
							cfs << "#include \"Runtime/Scripting/ScriptFactory.h\"\n";
							cfs << "#include \"Runtime/Foundation/Logger.h\"\n";
							cfs << "#include \"Runtime/ECS/World.h\"\n\n";
							cfs << "namespace Alice\n";
							cfs << "{\n";
							cfs << "    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.\n";
							cfs << "    REGISTER_SCRIPT(" << className << ");\n\n";
							cfs << "    void " << className << "::Start()\n";
							cfs << "    {\n";
							cfs << "        // 초기화 로직을 여기에 작성하세요.\n";
							cfs << "    }\n\n";
							cfs << "    void " << className << "::Update(float deltaTime)\n";
							cfs << "    {\n";
							cfs << "        // 매 프레임 호출되는 로직을 여기에 작성하세요.\n";
							cfs << "    }\n\n";
							cfs << "    void " << className << "::ExampleFunction()\n";
							cfs << "    {\n";
							cfs << "        // 리플렉션으로 등록된 함수 예시입니다.\n";
							cfs << "        // 이 함수는 에디터에서 호출할 수 있습니다.\n";
							cfs << "        \n";
							cfs << "        // 예시: Transform 컴포넌트 가져오기\n";
							cfs << "        if (auto* transform = GetComponent<TransformComponent>())\n";
							cfs << "        {\n";
							cfs << "            // 위치를 (0, 0, 0)으로 리셋하는 예시\n";
							cfs << "            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);\n";
							cfs << "        }\n";
							cfs << "    }\n";
							cfs << "}\n";
						}
					}
				}

				if (ImGui::MenuItem("Create Prefab"))
				{
					// 기본 프리팹(JSON) 생성 (.prefab)
					fs::path newPath = path / "NewPrefab.prefab";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewPrefab" + std::to_string(index) + ".prefab");
						++index;
					}

					nlohmann::json j;
					j["version"] = 1;
					j["name"] = "NewPrefab";
					j["Transform"] = {
						{ "position", { { "x", 0.0f }, { "y", 0.0f }, { "z", 0.0f } } },
						{ "rotation", { { "x", 0.0f }, { "y", 0.0f }, { "z", 0.0f } } },
						{ "scale",    { { "x", 1.0f }, { "y", 1.0f }, { "z", 1.0f } } },
						{ "enabled", true },
						{ "visible", true }
					};
					j["Scripts"] = nlohmann::json::array();

					std::ofstream ofs(newPath);
					if (ofs.is_open())
						ofs << j.dump(4);
				}


				if (ImGui::MenuItem("Create Scene"))
				{
					fs::path newPath = path / "NewScene.scene";
					int index = 1;
					while (fs::exists(newPath))
					{
						newPath = path / ("NewScene" + std::to_string(index) + ".scene");
						++index;
					}

					// 기본 씬: 큐브(Transform 1개) + 기본 Material 1개
					// ForwardRenderSystem은 Transform만 있어도 기본 큐브를 그립니다.
					World temp;
					std::string cubeAssetPath = "Assets/Fbx/Cube.fbxasset";
					EntityId e = InstantiateFbxAssetToWorld(temp, cubeAssetPath, "Cube");
					if (e == InvalidEntityId)
					{
						e = temp.CreateEntity();
						temp.AddComponent<TransformComponent>(e);
						temp.AddComponent<MaterialComponent>(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
					}

					SceneFile::Save(temp, newPath);
				}

				// 디렉터리 삭제 (Assets 안에서만 사용)
				if (ImGui::MenuItem("Delete Folder"))
				{
					std::error_code ec;
					fs::remove_all(path, ec);
				}

				ImGui::EndPopup();
			}

			if (open)
			{
				// 이 노드가 그 사이에 삭제되었으면 순회를 건너뜁니다.
				if (fs::exists(path) && fs::is_directory(path))
				{
					for (const auto& entry : fs::directory_iterator(path))
					{
						DrawDirectoryNode(world, selectedEntity, entry.path());
					}
				}

				ImGui::TreePop();
			}
		}
		else
		{
			const std::string ext = path.extension().string();

			// 파일 이름 렌더링: 일반 텍스트 또는 인라인 입력 박스
			ImGui::PushID(label.c_str());
			if (isRenamingThis)
			{
				ImGui::SetNextItemWidth(-1.0f);
				if (s_renameFocus)
				{
					ImGui::SetKeyboardFocusHere();
					s_renameFocus = false;
				}

				bool enterPressed = ImGui::InputText(
					"##RenameFile",
					s_renameBuffer,
					sizeof(s_renameBuffer),
					ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

				bool finished = enterPressed || ImGui::IsItemDeactivatedAfterEdit();
				if (finished)
				{
					if (std::strlen(s_renameBuffer) > 0)
					{
						fs::path newPath = path.parent_path() / s_renameBuffer;
						if (!fs::exists(newPath))
						{
							std::error_code ec;
							fs::rename(path, newPath, ec);
						}
					}
					s_renaming = false;
				}
			}
			else
			{
				ImGui::TreeNodeEx(label.c_str(),
					baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

				// 파일 드래그 소스: Inspector로 드래그앤드롭 가능
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					// 파일 경로를 문자열로 전달
					std::string pathStr = path.string();
					ImGui::SetDragDropPayload("ASSET_FILE_PATH", pathStr.c_str(), pathStr.size() + 1);
					ImGui::TextUnformatted(label.c_str());
					ImGui::EndDragDropSource();
				}
			}
			ImGui::PopID();

			// 파일 노드를 더블클릭하면 파일 형식에 따라 동작합니다.
			if (!isRenamingThis &&
				ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx")
				{
					//fs::path absPath = fs::absolute(path);
					//std::wstring wpath = absPath.wstring();
					//ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					wchar_t exePathW[MAX_PATH] = {};
					GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
					std::filesystem::path exePath = exePathW;
					std::filesystem::path exeDir = exePath.parent_path();
					std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
					std::filesystem::path scriptsSolutionRoot = projectRoot / "ScriptsBuild" / "build" / "AliceUserScripts.sln";
					ALICE_LOG_INFO("[Editor] Opening script solution: \"%s\"", scriptsSolutionRoot.string().c_str());
					ShellExecuteW(nullptr, L"open", scriptsSolutionRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				else if (ext == ".scene")
				{
					// 씬 파일을 더블클릭하면, 필요한 경우 저장 여부를 물은 뒤 로드합니다.
					g_NextScenePath = path;
					g_RequestSceneLoad = true;
				}
				else if (ext == ".mat")
				{
					// 머티리얼 에셋 전용 편집 창을 엽니다.
					g_MaterialEditorPath = path;
					g_MaterialEditorData = {};
					// 파일에서 값을 불러옵니다. 실패하면 기본 값으로 남겨둡니다.
					MaterialFile::Load(path, g_MaterialEditorData, &ResourceManager::Get());
					g_MaterialEditorData.assetPath = path.string();
					g_MaterialEditorOpen = true;
				}
				else if (ext == ".uicurve")
				{
					g_UICurveEditorPath = path;
					g_UICurveEditorData = {};
					if (!LoadUICurveAsset(path, g_UICurveEditorData))
					{
						g_UICurveEditorData.name = path.stem().string();
						g_UICurveEditorData.keys.push_back({ 0.0f, 0.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
						g_UICurveEditorData.keys.push_back({ 1.0f, 1.0f, 0.0f, 0.0f, UICurveInterp::Cubic, UICurveTangentMode::Auto });
					}
					g_UICurveEditorData.Sort();
					g_UICurveEditorData.RecalcAutoTangents();
					g_UICurveEditorSelected = -1;
					g_UICurveEditorOpen = true;
				}
			}

			// 파일 노드에 대한 우클릭 컨텍스트 메뉴 (열기/이름 바꾸기/삭제/프리팹 Instantiate 등)
			if (ImGui::BeginPopupContextItem())
			{
				// 어떤 확장자든 기본 Open / Rename / Delete 는 제공한다.
				if (ImGui::MenuItem("Open"))
				{
					fs::path absPath = fs::absolute(path);
					std::wstring wpath = absPath.wstring();
					ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}

				if (ImGui::MenuItem("Rename..."))
				{
					std::string fileName = path.filename().string();
					std::memset(s_renameBuffer, 0, sizeof(s_renameBuffer));
					strncpy_s(s_renameBuffer,
						sizeof(s_renameBuffer),
						fileName.c_str(),
						_TRUNCATE);
					s_renaming = true;
					s_renamingPath = path;
					s_renameFocus = true;
					// 바로 인라인 입력 박스를 보여주기 위해 팝업을 닫습니다.
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Delete"))
				{
					std::error_code ec;
					fs::remove(path, ec);

					// .cpp 삭제 시 같은 폴더의 <stem>.meta 도 같이 제거합니다.
					if (ext == ".cpp")
					{
						std::error_code ec2;
						fs::path metaPath = path.parent_path() / (path.stem().string() + ".meta");
						fs::remove(metaPath, ec2);
					}
				}

				// 프리팹 파일에 대한 Instantiate 동작
				if (ext == ".prefab")
				{
					if (ImGui::MenuItem("Instantiate Prefab"))
					{
						EntityId e = Alice::Prefab::InstantiateFromFile(world, path);
						if (e != InvalidEntityId)
						{
							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}

				// 머티리얼 파일에 대한 간단한 적용 기능
				if (ext == ".mat")
				{
					if (ImGui::MenuItem("Assign To Selected Entity") &&
						selectedEntity != InvalidEntityId &&
						world.GetComponent<TransformComponent>(selectedEntity))
					{
						MaterialComponent* mat = world.GetComponent<MaterialComponent>(selectedEntity);
						if (!mat)
						{
							DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							mat = &world.AddComponent<MaterialComponent>(selectedEntity, defaultColor);
						}

						if (mat)
						{
							MaterialFile::Load(path, *mat, &ResourceManager::Get());
							mat->assetPath = path.string();
							g_SceneDirty = true;
						}
					}
				}

					// 씬 파일 저장/로드
					if (ext == ".scene")
					{
						if (ImGui::MenuItem("Load Scene"))
						{
							g_NextScenePath = path;
							g_RequestSceneLoad = true;
						}
						if (ImGui::MenuItem("Save Current Scene"))
						{
							SceneFile::Save(world, path);
							g_CurrentScenePath = path;
							g_HasCurrentScenePath = true;
							g_SceneDirty = false;
						}
					}

				// FBX 인스턴스 에셋(.fbxasset)을 월드에 배치
				if (ext == ".fbxasset")
				{
					if (ImGui::MenuItem("Instantiate FBX"))
					{
						Alice::FbxInstanceAsset asset{};
						if (Alice::LoadFbxInstanceAsset(path, asset) && !asset.meshAssetPath.empty())
						{
							// 디버그 로깅: .fbxasset 로드 결과
							ALICE_LOG_INFO("[Editor] Instantiate FBX: assetPath=\"%s\" sourceFbx=\"%s\" meshKey=\"%s\" mats=%zu\n",
								path.string().c_str(),
								asset.sourceFbx.c_str(),
								asset.meshAssetPath.c_str(),
								asset.materialAssetPaths.size());

							// 레지스트리에 GPU 메시가 없다면, 원본 FBX 를 다시 임포트해서 등록합니다.
							if (m_skinnedRegistry && m_renderDevice)
							{
								if (!m_skinnedRegistry->Find(asset.meshAssetPath))
								{
									FbxImportOptions opt{};
									FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
									auto* device = m_renderDevice->GetDevice();
									std::filesystem::path srcFbxPath = ResourceManager::Get().Resolve(asset.sourceFbx);
									importer.Import(device, srcFbxPath, opt);

									ALICE_LOG_INFO("[Editor] Instantiate FBX: mesh was not in registry, re-imported FBX");
								}
								else
								{
									ALICE_LOG_INFO("[Editor] Instantiate FBX: mesh already in registry");
								}
							}

							EntityId e = world.CreateEntity();
							TransformComponent& t = world.AddComponent<TransformComponent>(e);
							t.position = { 0.0f, 0.0f, 0.0f };
							t.scale = { 1.0f, 1.0f, 1.0f };
							t.rotation = { 0.0f, 0.0f, 0.0f };

							SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, asset.meshAssetPath);
							skinned.instanceAssetPath = path.string();
							static DirectX::XMFLOAT4X4 s_identityBone =
								DirectX::XMFLOAT4X4(1, 0, 0, 0,
									0, 1, 0, 0,
									0, 0, 1, 0,
									0, 0, 0, 1);
							skinned.boneMatrices = &s_identityBone;
							skinned.boneCount = 1;

							ALICE_LOG_INFO("[Editor] Instantiate FBX: created entity=%u, boneCount=%u\n",
								static_cast<unsigned>(e),
								skinned.boneCount);

							if (!asset.materialAssetPaths.empty())
							{
								DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
								MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
								mat.assetPath = asset.materialAssetPaths.front();
								MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
							}

							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}

				ImGui::EndPopup();
			}
		}
	}

	// BuildSettings.json 파싱 및 시작 씬 로드
	bool LoadStartupSceneFromBuildSettings(World& world, const std::filesystem::path& exeDir)
	{
		// 1. 설정 파일 경로 확보 (Exe위치 -> 프로젝트 루트 순)
		std::filesystem::path cfg = exeDir / "BuildSettings.json";
		if (!std::filesystem::exists(cfg))
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

		// 3. 타겟 씬 결정 및 경로 보정
		if (target.empty() && !scenes.empty()) target = scenes.front();
		if (target.empty()) return false;

		std::filesystem::path finalPath = target;
		if (!finalPath.is_absolute())
		{
			// Exe 기준 존재 여부 확인 후, 없으면 루트 기준 적용
			if (std::filesystem::exists(exeDir / finalPath)) finalPath = exeDir / finalPath;
			else finalPath = exeDir.parent_path().parent_path().parent_path() / finalPath;
		}

		ALICE_LOG_INFO("Loading Startup Scene: %s", finalPath.string().c_str());

		// 4. 로드 실패 검사
		if (!SceneFile::Load(world, finalPath))
		{
			ALICE_LOG_ERRORF("Scene Load Failed: %s", finalPath.string().c_str());
			return false;
		}

		return true;
	}

	// 스킨 메쉬 등록 보장
	void EditorCore::EnsureSkinnedMeshesRegistered(World& world)
	{
		if (!m_skinnedRegistry || !m_renderDevice || world.GetComponents<SkinnedMeshComponent>().empty())
			return;

		for (const auto& [entityId, comp] : world.GetComponents<SkinnedMeshComponent>())
		{
			if (comp.meshAssetPath.empty() || m_skinnedRegistry->Find(comp.meshAssetPath))
				continue;

			// 경로 결정 (.fbxasset 우선, 없으면 관례 경로)
			std::filesystem::path fbxPath = comp.instanceAssetPath.empty()
				? std::filesystem::path("Assets/Fbx") / (comp.meshAssetPath + ".fbxasset")
				: std::filesystem::path(comp.instanceAssetPath);

			Alice::FbxInstanceAsset instance{};
			std::filesystem::path absPath = ResourceManager::Get().Resolve(fbxPath);

			// 로드 실패 검사
			if (!Alice::LoadFbxInstanceAsset(absPath, instance) || instance.sourceFbx.empty())
			{
				ALICE_LOG_WARN("[Editor] Failed loading fbxasset: %s", absPath.string().c_str());
				continue;
			}

			// 재임포트 및 등록
			FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
			FbxImportResult res = importer.Import(m_renderDevice->GetDevice(), ResourceManager::Get().Resolve(instance.sourceFbx), {});

			ALICE_LOG_INFO("[Editor] Re-imported FBX: %s -> %s", instance.sourceFbx.c_str(), res.meshAssetPath.c_str());
		}
	}

	// 씬 저장
	void EditorCore::SaveScene(World& world)
	{
		std::filesystem::path savePath = g_CurrentScenePath.empty() ? "Assets/AutoSaved.scene" : g_CurrentScenePath;

		ALICE_LOG_INFO("[Editor] Saving Scene: %s", savePath.string().c_str());


		// 저장 실행
		std::filesystem::path absPath = Alice::ResourceManager::Get().Resolve(savePath);
		SceneFile::Save(world, absPath);

		// 상태 갱신
		g_CurrentScenePath = savePath;
		g_HasCurrentScenePath = true;
		g_SceneDirty = false;
	}

	// FBX 에셋을 월드에 인스턴스화
	EntityId EditorCore::InstantiateFbxAssetToWorld(World& world,
		const std::filesystem::path& fbxAssetPath,
		std::string_view entityName)
	{
		Alice::FbxInstanceAsset asset{};
		std::filesystem::path abs = fbxAssetPath;

		// path가 논리 경로면 Resolve
		if (!abs.is_absolute())
			abs = ResourceManager::Get().Resolve(abs);

		if (!Alice::LoadFbxInstanceAsset(abs, asset) || asset.meshAssetPath.empty())
			return InvalidEntityId;

		// GPU 메시 없으면 원본 FBX 재임포트로 레지스트리 채움
		if (m_skinnedRegistry && m_renderDevice)
		{
			if (!m_skinnedRegistry->Find(asset.meshAssetPath))
			{
				FbxImportOptions opt{};
				FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
				auto* device = m_renderDevice->GetDevice();

				std::filesystem::path src = asset.sourceFbx;
				if (!src.is_absolute())
					src = ResourceManager::Get().Resolve(src);

				importer.Import(device, src, opt);
			}
		}

		EntityId e = world.CreateEntity();

		auto& t = world.AddComponent<TransformComponent>(e);
		t.position = { 0, 0, 0 };
		t.rotation = { 0, 0, 0 };
		t.scale = { 1, 1, 1 };

		auto& skinned = world.AddComponent<SkinnedMeshComponent>(e, asset.meshAssetPath);
		skinned.instanceAssetPath = abs.string();

		static DirectX::XMFLOAT4X4 s_identityBone =
			DirectX::XMFLOAT4X4(1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1);
		skinned.boneMatrices = &s_identityBone;
		skinned.boneCount = 1;

		if (!asset.materialAssetPaths.empty())
		{
			DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
			auto& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
			mat.assetPath = asset.materialAssetPaths.front();
			MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
		}

		if (!entityName.empty())
			world.SetEntityName(e, std::string(entityName));

		g_SceneDirty = true;
		return e;
	}

	// ComponentEditCommandRTTR 구현
	ComponentEditCommandRTTR::ComponentEditCommandRTTR(EntityId id,
		const EditorComponentDesc* d,
		JsonRttr::json oldJ,
		JsonRttr::json newJ)
		: entityId(id), desc(d), oldJson(std::move(oldJ)), newJson(std::move(newJ))
	{
		description = std::string("Edit ") + (desc ? desc->displayName : "Component");
	}

	void ComponentEditCommandRTTR::Execute(World& world, EntityId&)
	{
		if (!desc) return;
		rttr::instance inst = desc->getInstance(world, entityId);
		if (!inst.is_valid()) return;
		JsonRttr::FromJsonObject(inst, newJson);
	}

	void ComponentEditCommandRTTR::Undo(World& world, EntityId&)
	{
		if (!desc) return;
		rttr::instance inst = desc->getInstance(world, entityId);
		if (!inst.is_valid()) return;
		JsonRttr::FromJsonObject(inst, oldJson);
	}

	// 씬 로드 (레거시 함수 - 이제는 LoadSceneFileRequest 사용 권장)
	void EditorCore::PushCommand(std::unique_ptr<ICommand> cmd)
	{
		// 새로운 액션이 들어오면 Redo 스택 클리어 (일반적인 Undo/Redo 동작)
		g_RedoStack.clear();

		g_UndoStack.push_back(std::move(cmd));
		if (g_UndoStack.size() > MAX_UNDO_STACK_SIZE)
		{
			g_UndoStack.erase(g_UndoStack.begin());
		}
	}

	void EditorCore::LoadScene(World& world)
	{
		// 이 함수는 더 이상 사용하지 않음. SceneManager::LoadSceneFileRequest을 사용해야 함.
		// 하지만 호환성을 위해 남겨둠 (내부적으로는 즉시 로드)
		ALICE_LOG_WARN("[Editor] LoadScene() is deprecated. Use SceneManager::LoadSceneFileRequest() instead.");

		const std::filesystem::path loadAbs = ResourceManager::Get().Resolve(g_NextScenePath);

		// 로드 실행 및 반환값 체크
		if (!SceneFile::Load(world, loadAbs))
		{
			// 로드 실패: 에러 로그 및 팝업 표시
			const std::string errorMsg = "씬 로드 실패: " + g_NextScenePath.string() + "\n\n파일을 읽거나 역직렬화하는 중 오류가 발생했습니다.\n일부 컴포넌트만 로드되었을 수 있습니다.";
			ALICE_LOG_ERRORF("[Editor] Scene load failed: %s", g_NextScenePath.string().c_str());

			g_SceneLoadErrorMsg = errorMsg;
			g_ShowSceneLoadError = true;

			// 후처리하지 않고 종료 (부분 로드 방지)
			return;
		}

			// 로드 성공: 후처리 및 상태 갱신
	EnsureSkinnedMeshesRegistered(world);
	g_CurrentScenePath = g_NextScenePath;
	g_HasCurrentScenePath = true;
	g_SceneDirty = false;
	}

	EntityId EditorCore::CreateAliceUIRoot(World& world, std::string_view name)
	{
		EntityId e = world.CreateEntity();
		world.SetEntityName(e, std::string(name));

		UIWidgetComponent& widget = world.AddComponent<UIWidgetComponent>(e);
		widget.widgetName = std::string(name);
		widget.space = AliceUI::UISpace::Screen;

		UITransformComponent& t = world.AddComponent<UITransformComponent>(e);
		t.anchorMin = DirectX::XMFLOAT2(0.5f, 0.5f);
		t.anchorMax = DirectX::XMFLOAT2(0.5f, 0.5f);
		t.position = DirectX::XMFLOAT2(0.0f, 0.0f);
		t.size = DirectX::XMFLOAT2(200.0f, 80.0f);
		t.pivot = DirectX::XMFLOAT2(0.5f, 0.5f);

		// Always attach a 3D Transform so UI can be switched to World space later.
		world.AddComponent<TransformComponent>(e);

		return e;
	}

	EntityId EditorCore::CreateAliceUIImage(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Image");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIImageComponent>(e);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIText(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Text");
		if (e != InvalidEntityId)
		{
			UITextComponent& text = world.AddComponent<UITextComponent>(e);
			text.text = "Text";
			text.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIButton(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Button");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIButtonComponent>(e);
			world.AddComponent<UIImageComponent>(e);
			UITextComponent& text = world.AddComponent<UITextComponent>(e);
			text.text = "Button";
			text.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(220.0f, 60.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIGauge(World& world)
	{
		EntityId e = CreateAliceUIRoot(world, "UI_Gauge");
		if (e != InvalidEntityId)
		{
			world.AddComponent<UIGaugeComponent>(e);
			UITransformComponent* t = world.GetComponent<UITransformComponent>(e);
			if (t)
				t->size = DirectX::XMFLOAT2(260.0f, 24.0f);
		}
		return e;
	}

	EntityId EditorCore::CreateAliceUIWorldImage(World& world)
	{
		EntityId e = world.CreateEntity();
		world.SetEntityName(e, "World_UI_Image");

		auto& widget = world.AddComponent<UIWidgetComponent>(e);
		widget.widgetName = "World_UI_Image";
		widget.space = AliceUI::UISpace::World;
		widget.billboard = true;

		auto& uiTransform = world.AddComponent<UITransformComponent>(e);
		uiTransform.size = DirectX::XMFLOAT2(0.6f, 0.6f);

		world.AddComponent<UIImageComponent>(e);

		TransformComponent& t = world.AddComponent<TransformComponent>(e);
		t.position = DirectX::XMFLOAT3(0.0f, 2.0f, 0.0f);

		return e;
	}

} // namespace Alice
