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
		// RTTR 기반 Inspector 렌더링 헬퍼
		ReflectionUI::UIEditEvent RenderInspectorInstance(rttr::instance inst, World* world)
		{
			ReflectionUI::UIEditEvent result{};
			if (!inst.is_valid()) return result;

			rttr::type t = inst.get_type();
			for (auto& prop : t.get_properties())
			{
				const std::string propName = prop.get_name().to_string();

				ReflectionUI::UIEditEvent ev{};
				if (propName == "roughness" || propName == "metalness")
				{
					ev = ReflectionUI::Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
				}
				else
				{
					ev = ReflectionUI::Detail::RenderProperty(prop, inst, "", world);
				}

				result.changed |= ev.changed;
				result.activated |= ev.activated;
				result.deactivatedAfterEdit |= ev.deactivatedAfterEdit;
			}
			return result;
		}
		// Undo/Redo 스택 관리
		std::vector<std::unique_ptr<ICommand>> g_UndoStack;
		std::vector<std::unique_ptr<ICommand>> g_RedoStack;
		constexpr size_t MAX_UNDO_STACK_SIZE = 50;

		// PushCommand는 EditorCore 클래스의 멤버 함수로 이동됨

		inline bool MaterialInspectorFilter(const std::string& propName)
		{
			// assetPath/albedoTexturePath/shadingMode는 특별 UI 처리하므로 제외
			return propName != "assetPath" && propName != "albedoTexturePath" && propName != "shadingMode";
		}

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

	void EditorCore::DrawInspectorTransform(World& world, const EntityId& _selectedEntity)
	{
		if (auto* transform =
			world.GetComponent<TransformComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Transform",
				ImGuiTreeNodeFlags_DefaultOpen)) {
				// 편집 시작 시 old state 저장
				static EntityId lastEditedEntity = InvalidEntityId;
				static TransformCommand::TransformData editStartTransform;
				static bool isEditing = false;

				// 편집 시작 감지
				if (ImGui::IsItemActive() && !isEditing)
				{
					editStartTransform.position = transform->position;
					editStartTransform.rotation = transform->rotation;
					editStartTransform.scale = transform->scale;
					editStartTransform.enabled = transform->enabled;
					editStartTransform.visible = transform->visible;
					isEditing = true;
					lastEditedEntity = _selectedEntity;
				}
				else if (lastEditedEntity != _selectedEntity)
				{
					// 다른 엔티티로 변경: 상태 리셋
					isEditing = false;
					lastEditedEntity = _selectedEntity;
				}

				bool changed = false;
				bool anyTransformItemActive = false;
				bool anyTransformItemActivated = false;

				// ---- Position
				{
					auto r = ReflectionUI::RenderProperty(*transform, "position", "Position");
					changed |= r.changed;
					anyTransformItemActive |= ImGui::IsItemActive();
					anyTransformItemActivated |= ImGui::IsItemActivated();
				}

				// ---- Rotation
				{
					// 라디안(Radian) -> 디그리(Degree) 변환하여 표시
					DirectX::XMFLOAT3 rotDeg;
					rotDeg.x = DirectX::XMConvertToDegrees(transform->rotation.x);
					rotDeg.y = DirectX::XMConvertToDegrees(transform->rotation.y);
					rotDeg.z = DirectX::XMConvertToDegrees(transform->rotation.z);

					// ImGui로 직접 그림 (ReflectionUI 대신 사용)
					if (ImGui::DragFloat3("Rotation", &rotDeg.x, 0.1f))
					{
						// 변경된 디그리 값을 다시 라디안으로 변환하여 저장
						transform->rotation.x = DirectX::XMConvertToRadians(rotDeg.x);
						transform->rotation.y = DirectX::XMConvertToRadians(rotDeg.y);
						transform->rotation.z = DirectX::XMConvertToRadians(rotDeg.z);
						changed = true;
					}

					// Undo/Redo 로직 유지를 위한 상태 플래그 갱신
					anyTransformItemActive |= ImGui::IsItemActive();
					anyTransformItemActivated |= ImGui::IsItemActivated();
				}

				// ---- Scale
				{
					auto r = ReflectionUI::RenderProperty(*transform, "scale", "Scale");
					changed |= r.changed;
					anyTransformItemActive |= ImGui::IsItemActive();
					anyTransformItemActivated |= ImGui::IsItemActivated();
				}

				// ---- Enabled
				{
					auto r = ReflectionUI::RenderProperty(*transform, "enabled", "Enabled");
					changed |= r.changed;
					anyTransformItemActive |= ImGui::IsItemActive();
					anyTransformItemActivated |= ImGui::IsItemActivated();
				}

				// ---- Render Enabled
				{
					auto r = ReflectionUI::RenderProperty(*transform, "visible", "Visible");
					changed |= r.changed;
					anyTransformItemActive |= ImGui::IsItemActive();
					anyTransformItemActivated |= ImGui::IsItemActivated();
				}

				// === 편집 시작 감지 (Transform 위젯 중 하나라도 막 활성화됐을 때)
				if (!isEditing && anyTransformItemActivated)
				{
					isEditing = true;
					lastEditedEntity = _selectedEntity;

					editStartTransform.position = transform->position;
					editStartTransform.rotation = transform->rotation;
					editStartTransform.scale = transform->scale;
					editStartTransform.enabled = transform->enabled;
					editStartTransform.visible = transform->visible;
				}

				// === Transform 변경 시: 물리 텔레포트 + 월드행렬 캐시 무효화 + dirty
				if (changed)
				{
					if (auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(_selectedEntity))
						rigidBody->teleport = true;

					if (auto* cct = world.GetComponent<Phy_CCTComponent>(_selectedEntity))
						cct->teleport = true;

					// PostProcessVolumeComponent가 있으면 DebugDrawBoxComponent 업데이트
					if (auto* volume = world.GetComponent<PostProcessVolumeComponent>(_selectedEntity))
					{
						world.UpdatePostProcessVolumeDebugBox(_selectedEntity, *volume);
					}

					world.MarkTransformDirty(_selectedEntity);
					g_SceneDirty = true;
				}

				// === 편집 종료 감지: Transform 위젯이 더 이상 Active가 아닐 때
				if (isEditing && lastEditedEntity == _selectedEntity && !anyTransformItemActive)
				{
					TransformCommand::TransformData newTransform;
					newTransform.position = transform->position;
					newTransform.rotation = transform->rotation;
					newTransform.scale = transform->scale;
					newTransform.enabled = transform->enabled;
					newTransform.visible = transform->visible;

					// float 비교(너무 타이트하면 커맨드가 과하게 쌓임)
					constexpr float kEps = 1e-5f;
					auto NE = [](float a, float b) { return std::fabs(a - b) > kEps; };

					bool hasChanged =
						NE(editStartTransform.position.x, newTransform.position.x) ||
						NE(editStartTransform.position.y, newTransform.position.y) ||
						NE(editStartTransform.position.z, newTransform.position.z) ||
						NE(editStartTransform.rotation.x, newTransform.rotation.x) ||
						NE(editStartTransform.rotation.y, newTransform.rotation.y) ||
						NE(editStartTransform.rotation.z, newTransform.rotation.z) ||
						NE(editStartTransform.scale.x, newTransform.scale.x) ||
						NE(editStartTransform.scale.y, newTransform.scale.y) ||
						NE(editStartTransform.scale.z, newTransform.scale.z) ||
						(editStartTransform.enabled != newTransform.enabled) ||
						(editStartTransform.visible != newTransform.visible);

					if (hasChanged)
					{
						PushCommand(std::make_unique<TransformCommand>(
							_selectedEntity, editStartTransform, newTransform));
					}

					isEditing = false;
				}
			}
		}
	}

	void EditorCore::DrawInspectorAnimationStatus(World& world, const EntityId& _selectedEntity)
	{
		if (auto* anim = world.GetComponent<SkinnedAnimationComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Animation Status", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Playing: %s", anim->playing ? "Yes" : "No");
				ImGui::Text("Speed: %.2f", anim->speed);
				ImGui::Text("Clip Index: %d", anim->clipIndex);
				ImGui::Text("Time: %.3f sec", anim->timeSec);

				// SkinnedMesh가 있으면 애니메이션 이름도 표시
				if (auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity))
				{
					if (m_skinnedRegistry)
					{
						auto mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
						if (mesh && mesh->sourceModel)
						{
							const auto& names = mesh->sourceModel->GetAnimationNames();
							if (anim->clipIndex >= 0 && anim->clipIndex < (int)names.size())
							{
								ImGui::Text("Clip Name: %s", names[(size_t)anim->clipIndex].c_str());
								const double dur = mesh->sourceModel->GetClipDurationSec(anim->clipIndex);
								if (dur > 0.0)
								{
									ImGui::Text("Duration: %.3f sec", dur);
									ImGui::Text("Progress: %.1f%%", (anim->timeSec / dur) * 100.0);
								}
							}
						}
					}
				}
			}
		}
	}

	void EditorCore::DrawInspectorScripts(World& world, const EntityId& _selectedEntity)
	{
		static std::vector<std::string> scriptNames;
		if (ImGui::BeginCombo("Add Script", "Select Script...")) {
			if (scriptNames.empty() || m_scriptBuilded) {
				m_scriptBuilded = false;
				scriptNames = ScriptFactory::GetRegisteredScriptNames();
				std::sort(scriptNames.begin(), scriptNames.end());
				scriptNames.erase(std::unique(scriptNames.begin(), scriptNames.end()),
					scriptNames.end());
			}

			for (const auto& name : scriptNames) {
				if (ImGui::Selectable(name.c_str())) {
					world.AddScript(_selectedEntity, name);
					g_SceneDirty = true;
				}
			}
			ImGui::EndCombo();
		}

		// Script 추가 필드에 드롭 타겟 추가
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
			{
				const char* pathStr = static_cast<const char*>(payload->Data);
				std::filesystem::path droppedPath(pathStr);
				std::string ext = droppedPath.extension().string();

				// 스크립트 파일인지 확인 (.h, .cpp)
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx")
				{
					// 파일명에서 스크립트 이름 추출 (확장자 제외)
					std::string scriptName = droppedPath.stem().string();

					// 등록된 스크립트 목록 확인
					if (scriptNames.empty() || m_scriptBuilded) {
						m_scriptBuilded = false;
						scriptNames = ScriptFactory::GetRegisteredScriptNames();
						std::sort(scriptNames.begin(), scriptNames.end());
						scriptNames.erase(std::unique(scriptNames.begin(), scriptNames.end()),
							scriptNames.end());
					}

					// 등록된 스크립트 목록에 있는지 확인
					bool found = std::find(scriptNames.begin(), scriptNames.end(), scriptName) != scriptNames.end();
					if (found) {
						world.AddScript(_selectedEntity, scriptName);
						g_SceneDirty = true;
					}
					// 등록되지 않은 경우에도 시도 (나중에 빌드되면 사용 가능할 수 있음)
					else {
						world.AddScript(_selectedEntity, scriptName);
						g_SceneDirty = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 엔진 컴포넌트 추가 UI - 레지스트리 기반
		if (ImGui::BeginCombo("Add Engine Component", "Select Component..."))
		{
			auto& reg = EditorComponentRegistry::Get();
			const auto& list = reg.All();

			std::string currentCat;
			for (auto& d : list)
			{
				if (!d.addable) continue;

				if (d.category != currentCat)
				{
					currentCat = d.category;
					ImGui::Separator();
					ImGui::TextDisabled("%s", currentCat.c_str());
				}

				const bool has = d.has(world, _selectedEntity);

				if (has) ImGui::BeginDisabled();
				if (ImGui::Selectable(d.displayName.c_str(), false) && !has)
				{
					d.add(world, _selectedEntity);
					g_SceneDirty = true;
				}
				if (has) ImGui::EndDisabled();

				if (has && ImGui::IsItemHovered())
					ImGui::SetTooltip("이 컴포넌트는 이미 추가되어 있습니다.");
			}
			ImGui::EndCombo();
		}



		// 엔진 컴포넌트 표시 - 레지스트리 기반
		// 레지스트리 순회로 컴포넌트 표시
		auto& reg = EditorComponentRegistry::Get();
		for (auto& d : reg.All())
		{
			// 고정 레이아웃에서 처리되는 컴포넌트들은 제외 (중복 방지)
			std::string typeName = d.type.get_name().to_string();
			if (typeName == "TransformComponent" ||
				typeName == "MaterialComponent" ||
				typeName == "ComputeEffectComponent" ||
				typeName == "PointLightComponent" ||
				typeName == "SpotLightComponent" ||
				typeName == "RectLightComponent" ||
				typeName == "PostProcessVolumeComponent" ||
				typeName == "SkinnedMeshComponent" ||
				typeName == "SkinnedAnimationComponent")  // Animation Status 섹션에서 처리됨
				continue;

			// 특수 처리 필요한 컴포넌트들 (물리 컴포넌트 등)
			if (typeName == "Phy_ColliderComponent")
			{
				DrawInspectorCollider(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_MeshColliderComponent")
			{
				DrawInspectorMeshCollider(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_CCTComponent")
			{
				DrawInspectorCharacterController(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_TerrainHeightFieldComponent")
			{
				DrawInspectorTerrainHeightField(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_SettingsComponent")
			{
				DrawInspectorPhysicsSceneSettings(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_JointComponent")
			{
				DrawInspectorJoint(world, _selectedEntity);
				continue;
			}
			else if (typeName == "AttackDriverComponent")
			{
				DrawInspectorAttackDriver(world, _selectedEntity);
				continue;
			}
			else if (typeName == "HurtboxComponent")
			{
				DrawInspectorHurtbox(world, _selectedEntity);
				continue;
			}
			else if (typeName == "WeaponTraceComponent")
			{
				DrawInspectorWeaponTrace(world, _selectedEntity);
				continue;
			}
			else if (typeName == "SocketAttachmentComponent")
			{
				DrawInspectorSocketAttachment(world, _selectedEntity);
				continue;
			}
			else if (typeName == "SocketComponent")
			{
				DrawInspectorSocketComponent(world, _selectedEntity);
				continue;
			}
			// 일반 컴포넌트: 레지스트리 기반 렌더링
			if (!d.has(world, _selectedEntity)) continue;

			if (ImGui::CollapsingHeader(d.displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (d.removable)
				{
					std::string btn = "Remove##" + d.displayName;
					if (ImGui::Button(btn.c_str()))
					{
						d.remove(world, _selectedEntity);
						g_SceneDirty = true;
						continue;
					}
				}

				// 편집 시작/종료 감지 및 Undo 스냅샷
				static EntityId lastEditedEntity = InvalidEntityId;
				static std::string lastEditedComponentType;
				static JsonRttr::json editStartJson;
				static const EditorComponentDesc* lastEditedDesc = nullptr;

				rttr::instance inst = d.getInstance(world, _selectedEntity);
				ReflectionUI::UIEditEvent ev = RenderInspectorInstance(inst, &world);

				// 편집 시작: oldJson 스냅샷 저장
				if (ev.activated && (_selectedEntity != lastEditedEntity || lastEditedComponentType != typeName))
				{
					editStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedEntity = _selectedEntity;
					lastEditedComponentType = typeName;
					lastEditedDesc = &d;
				}

				// 편집 종료: newJson 저장하고 커맨드 푸시
				if (ev.deactivatedAfterEdit && _selectedEntity == lastEditedEntity && lastEditedComponentType == typeName)
				{
					JsonRttr::json editEndJson = JsonRttr::ToJsonObject(inst);

					// 변경사항이 있으면 커맨드 푸시
					if (editStartJson != editEndJson && lastEditedDesc)
					{
						PushCommand(std::make_unique<ComponentEditCommandRTTR>(
							_selectedEntity, lastEditedDesc, editStartJson, editEndJson));
						g_SceneDirty = true;
					}

					lastEditedEntity = InvalidEntityId;
					lastEditedComponentType.clear();
					lastEditedDesc = nullptr;
				}

				if (ev.changed)
				{
					g_SceneDirty = true;
				}
			}
		}

		// List Scripts
		if (auto* scripts = world.GetScripts(_selectedEntity);
			scripts && !scripts->empty()) {
			for (size_t i = 0; i < scripts->size();) {
				auto& sc = (*scripts)[i];
				bool removed = false;

				ImGui::PushID(static_cast<int>(i));
				std::string header = sc.scriptName.empty() ? "Script" : sc.scriptName;
				if (ImGui::CollapsingHeader(header.c_str(),
					ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled", &sc.enabled);
					ImGui::SameLine();
					if (ImGui::Button("Remove##ScriptRemove"))
						removed = true;

					// Save/Load Defaults (.meta)
					ImGui::SameLine();
					if (sc.instance && ImGui::Button("SaveDefaults")) {
						auto path = std::filesystem::path("Assets/Scripts") /
							(sc.scriptName + ".meta");
						JsonRttr::json root;
						root["version"] = 1;
						root["props"] = JsonRttr::ToJsonObject(
							*sc.instance, rttr::type::get_by_name(sc.scriptName));
						JsonRttr::SaveJsonFile(path, root, 4);
						ALICE_LOG_INFO("[Editor] Saved script defaults: %s",
							path.string().c_str());
					}
					ImGui::SameLine();
					if (sc.instance && ImGui::Button("LoadDefaults")) {
						auto path = std::filesystem::path("Assets/Scripts") /
							(sc.scriptName + ".meta");
						JsonRttr::json root;
						if (JsonRttr::LoadJsonFile(path, root)) {
							JsonRttr::FromJsonObject(
								*sc.instance, root["props"],
								rttr::type::get_by_name(sc.scriptName));
							g_SceneDirty = true;
						}
					}

					// Properties
					if (sc.instance) {
						rttr::instance inst = *sc.instance;
						rttr::type type = rttr::type::get_by_name(sc.scriptName);
						if (!type.is_valid()) type = inst.get_type();

						//rttr::instance inst = sc.instance;
						//rttr::type type = inst.get_derived_type(); // 이제 정확한 자식 타입이 나옴
						//if (!type.is_valid()) return;

						for (auto prop : type.get_properties()) {
							// Entity Reference Check
							// 1. Type is EntityId
							// 2. Metadata "EntityRef" is present
							rttr::type pType = prop.get_type();
							std::string pTypeName = pType.get_name().to_string();
							bool isEntityRef = (pType == rttr::type::get<EntityId>()) ||
								prop.get_metadata("EntityRef") ||
								(pTypeName == "EntityId") ||
								(pTypeName == "Alice::EntityId");

							if (isEntityRef) {
								EntityId currentRef = InvalidEntityId;
								rttr::variant val = prop.get_value(inst);
								if (val.can_convert<EntityId>())
									currentRef = val.get_value<EntityId>();

								std::string currentName = "None";
								if (currentRef != InvalidEntityId) {
									currentName = world.GetEntityName(currentRef);
									if (currentName.empty())
										currentName =
										"Entity " + std::to_string((uint32_t)currentRef);
								}

								if (ImGui::BeginCombo(prop.get_name().to_string().c_str(),
									currentName.c_str())) {
									if (ImGui::Selectable("None", currentRef == InvalidEntityId)) {
										prop.set_value(inst, InvalidEntityId);
										g_SceneDirty = true;
									}

									for (auto [eid, t] :
										world.GetComponents<TransformComponent>()) {
										std::string name = world.GetEntityName(eid);
										if (name.empty()) name = "Entity " + std::to_string((uint32_t)eid);
										if (ImGui::Selectable(name.c_str(), eid == currentRef)) {
											prop.set_value(inst, eid);
											g_SceneDirty = true;
										}
									}
									ImGui::EndCombo();
								}

								// EntityId 참조 필드에 드롭 타겟 추가 (Hierarchy에서 드래그한 엔티티)
								if (ImGui::BeginDragDropTarget())
								{
									if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
									{
										IM_ASSERT(payload->DataSize == sizeof(EntityId));
										EntityId draggedId = *(const EntityId*)payload->Data;

										if (draggedId != InvalidEntityId)
										{
											prop.set_value(inst, draggedId);
											g_SceneDirty = true;
										}
									}
									ImGui::EndDragDropTarget();
								}
							}
							else {
								// Generic - string 타입의 경우 world를 전달하여 드래그 앤 드롭 지원
								// ReflectionUI::Detail::RenderProperty가 자동으로 엔티티 참조 필드를 감지하고 처리함
								ReflectionUI::UIEditEvent propEvent = ReflectionUI::Detail::RenderProperty(prop, inst, "", &world);
								if (propEvent.changed)
									g_SceneDirty = true;
							}
						}
					}
				}
				ImGui::PopID();

				if (removed) {
					world.RemoveScript(_selectedEntity, i);
					g_SceneDirty = true;
				}
				else
					i++;
			}
		}
	}


	void EditorCore::DrawInspectorMaterial(World& world, const EntityId& _selectedEntity)
	{
		if (auto* mat = world.GetComponent<MaterialComponent>(_selectedEntity)) {
			ImGui::Text("Material");
			if (!mat->assetPath.empty())
				ImGui::Text("Asset: %s", mat->assetPath.c_str());

			bool changed = false;

			// Material assetPath 필드에 드롭 타겟 추가
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);
					std::string ext = droppedPath.extension().string();

					// Material 파일인지 확인
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (ext == ".mat")
					{
						// 논리 경로로 변환
						std::string logicalPath = droppedPath.string();
						{
							std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(droppedPath);
							if (!logical.empty())
							{
								logicalPath = logical.string();
							}
						}
						mat->assetPath = logicalPath;
						// Material 파일에서 속성 로드
						MaterialFile::Load(droppedPath, *mat, &ResourceManager::Get());
						changed = true;
						g_SceneDirty = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			changed |= ReflectionUI::RenderInspector(*mat, MaterialInspectorFilter).changed;

			struct ShadingItem
			{
				const char* label;
				int value;
			};
			const ShadingItem shadingItems[] = {
				{ "Global", -1 },
				{ "Lambert", 0 },
				{ "Phong", 1 },
				{ "Blinn-Phong", 2 },
				{ "Toon", 3 },
				{ "PBR", 4 },
				{ "ToonPBR", 5 },
				{ "ToonPBREditable", 7 },
				{ "OnlyTextureWithOutline", 6 }
			};
			int shadingIndex = 0;
			for (int i = 0; i < (int)std::size(shadingItems); ++i)
			{
				if (shadingItems[i].value == mat->shadingMode)
				{
					shadingIndex = i;
					break;
				}
			}
			if (ImGui::Combo("Shading", &shadingIndex, [](void* data, int idx, const char** out_text) {
				auto* items = static_cast<const ShadingItem*>(data);
				*out_text = items[idx].label;
				return true;
			}, (void*)shadingItems, (int)std::size(shadingItems)))
			{
				mat->shadingMode = shadingItems[shadingIndex].value;
				changed = true;
			}

			auto IsImageExt = [](std::string ext)
			{
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp";
			};

			auto NormalizeToLogicalIfPossible = [&](const std::filesystem::path& p) -> std::string
			{
				// 기본은 입력 경로 문자열
				std::string out = p.string();

				// 가능하면 "논리 경로"로 변환
				{
					std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(p);
					if (!logical.empty() && !logical.is_absolute())
						out = logical.string();
				}
				return out;
			};

			auto ApplyAlbedoPath = [&](const std::filesystem::path& anyPath)
			{
				if (!IsImageExt(anyPath.extension().string()))
					return;

				mat->albedoTexturePath = NormalizeToLogicalIfPossible(anyPath);
				changed = true;
				g_SceneDirty = true;
			};

			// ---- UI
			ImGui::Text("Albedo: %s", mat->albedoTexturePath.empty() ? "None" : mat->albedoTexturePath.c_str());

			// 드롭 타겟
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					// payload가 널 종결 문자열이라는 전제(너희 쪽에서 보장해야 함)
					const char* pathStr = static_cast<const char*>(payload->Data);
					if (pathStr && pathStr[0] != '\0')
					{
						ApplyAlbedoPath(std::filesystem::path(pathStr));
					}
				}
				ImGui::EndDragDropTarget();
			}

			// Browse
			if (ImGui::Button("Browse..."))
			{
				wchar_t buf[MAX_PATH] = {};
				OPENFILENAMEW ofn = { sizeof(ofn) };
				ofn.hwndOwner = m_hwnd;
				ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp\0All\0*.*\0";
				ofn.lpstrFile = buf;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameW(&ofn))
				{
					ApplyAlbedoPath(std::filesystem::path(buf));
				}
			}


			if (changed) {
				g_SceneDirty = true;
			}

			if (ImGui::Button("Remove Material")) {
				world.RemoveComponent<MaterialComponent>(_selectedEntity);
				g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorPointLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<PointLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##PointLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##PointLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##PointLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Range##PointLight", &light->range, 0.1f, 200.0f);

				if (ImGui::Button("Remove Point Light")) {
					world.RemoveComponent<PointLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorSpotLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<SpotLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##SpotLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##SpotLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##SpotLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Range##SpotLight", &light->range, 0.1f, 200.0f);
				changed |= ImGui::SliderFloat("Inner Angle (deg)##SpotLight", &light->innerAngleDeg, 0.0f, 89.0f);
				changed |= ImGui::SliderFloat("Outer Angle (deg)##SpotLight", &light->outerAngleDeg, 0.0f, 89.0f);

				if (light->innerAngleDeg > light->outerAngleDeg)
					light->innerAngleDeg = light->outerAngleDeg;

				if (ImGui::Button("Remove Spot Light")) {
					world.RemoveComponent<SpotLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorRectLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<RectLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Rect Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##RectLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##RectLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##RectLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Width##RectLight", &light->width, 0.1f, 50.0f);
				changed |= ImGui::SliderFloat("Height##RectLight", &light->height, 0.1f, 50.0f);
				changed |= ImGui::SliderFloat("Range##RectLight", &light->range, 0.1f, 200.0f);

				if (ImGui::Button("Remove Rect Light")) {
					world.RemoveComponent<RectLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::SaveDefaultPostProcessSettings()
	{
		namespace fs = std::filesystem;
		
		// 프로젝트 루트 경로 계산
		wchar_t exePathW[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
		fs::path exePath = exePathW;
		fs::path exeDir = exePath.parent_path();
		fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
		fs::path settingsPath = projectRoot / "EngineSettings.json";

		try
		{
			nlohmann::json j;
			
			// 기존 파일이 있으면 읽기
			if (fs::exists(settingsPath))
			{
				std::ifstream ifs(settingsPath);
				if (ifs.is_open())
				{
					ifs >> j;
					ifs.close();
				}
			}

			// Default PostProcess Settings 저장
			nlohmann::json ppSettings;
			ppSettings["exposure"] = m_defaultPostProcessSettings.exposure;
			ppSettings["maxHDRNits"] = m_defaultPostProcessSettings.maxHDRNits;
			ppSettings["saturation"] = { m_defaultPostProcessSettings.saturation.x, m_defaultPostProcessSettings.saturation.y, m_defaultPostProcessSettings.saturation.z };
			ppSettings["contrast"] = { m_defaultPostProcessSettings.contrast.x, m_defaultPostProcessSettings.contrast.y, m_defaultPostProcessSettings.contrast.z };
			ppSettings["gamma"] = { m_defaultPostProcessSettings.gamma.x, m_defaultPostProcessSettings.gamma.y, m_defaultPostProcessSettings.gamma.z };
			ppSettings["gain"] = { m_defaultPostProcessSettings.gain.x, m_defaultPostProcessSettings.gain.y, m_defaultPostProcessSettings.gain.z };
			ppSettings["bloomThreshold"] = m_defaultPostProcessSettings.bloomThreshold;
			ppSettings["bloomKnee"] = m_defaultPostProcessSettings.bloomKnee;
			ppSettings["bloomIntensity"] = m_defaultPostProcessSettings.bloomIntensity;
			ppSettings["bloomGaussianIntensity"] = m_defaultPostProcessSettings.bloomGaussianIntensity;
			ppSettings["bloomRadius"] = m_defaultPostProcessSettings.bloomRadius;
			ppSettings["bloomDownsample"] = m_defaultPostProcessSettings.bloomDownsample;

			j["defaultPostProcess"] = ppSettings;

			// 파일 저장
			std::ofstream ofs(settingsPath);
			if (ofs.is_open())
			{
				ofs << j.dump(4);
				ofs.close();
				ALICE_LOG_INFO("Default PostProcess Settings saved to EngineSettings.json");
			}
			else
			{
				ALICE_LOG_ERRORF("Failed to save Default PostProcess Settings to %s", settingsPath.string().c_str());
			}
		}
		catch (const std::exception& e)
		{
			ALICE_LOG_ERRORF("Exception while saving Default PostProcess Settings: %s", e.what());
		}
	}

	void EditorCore::LoadDefaultPostProcessSettings()
	{
		namespace fs = std::filesystem;
		
		// 프로젝트 루트 경로 계산
		wchar_t exePathW[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
		fs::path exePath = exePathW;
		fs::path exeDir = exePath.parent_path();
		fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
		fs::path settingsPath = projectRoot / "EngineSettings.json";

		try
		{
			if (!fs::exists(settingsPath))
			{
				// 파일이 없으면 기본값 유지
				return;
			}

			std::ifstream ifs(settingsPath);
			if (!ifs.is_open())
			{
				return;
			}

			nlohmann::json j;
			ifs >> j;
			ifs.close();

			// Default PostProcess Settings 로드
			if (j.contains("defaultPostProcess"))
			{
				const auto& ppSettings = j["defaultPostProcess"];
				
				if (ppSettings.contains("exposure"))
					m_defaultPostProcessSettings.exposure = ppSettings["exposure"].get<float>();
				if (ppSettings.contains("maxHDRNits"))
					m_defaultPostProcessSettings.maxHDRNits = ppSettings["maxHDRNits"].get<float>();
				
				if (ppSettings.contains("saturation") && ppSettings["saturation"].is_array() && ppSettings["saturation"].size() >= 3)
				{
					m_defaultPostProcessSettings.saturation.x = ppSettings["saturation"][0].get<float>();
					m_defaultPostProcessSettings.saturation.y = ppSettings["saturation"][1].get<float>();
					m_defaultPostProcessSettings.saturation.z = ppSettings["saturation"][2].get<float>();
				}
				
				if (ppSettings.contains("contrast") && ppSettings["contrast"].is_array() && ppSettings["contrast"].size() >= 3)
				{
					m_defaultPostProcessSettings.contrast.x = ppSettings["contrast"][0].get<float>();
					m_defaultPostProcessSettings.contrast.y = ppSettings["contrast"][1].get<float>();
					m_defaultPostProcessSettings.contrast.z = ppSettings["contrast"][2].get<float>();
				}
				
				if (ppSettings.contains("gamma") && ppSettings["gamma"].is_array() && ppSettings["gamma"].size() >= 3)
				{
					m_defaultPostProcessSettings.gamma.x = ppSettings["gamma"][0].get<float>();
					m_defaultPostProcessSettings.gamma.y = ppSettings["gamma"][1].get<float>();
					m_defaultPostProcessSettings.gamma.z = ppSettings["gamma"][2].get<float>();
				}
				
				if (ppSettings.contains("gain") && ppSettings["gain"].is_array() && ppSettings["gain"].size() >= 3)
				{
					m_defaultPostProcessSettings.gain.x = ppSettings["gain"][0].get<float>();
					m_defaultPostProcessSettings.gain.y = ppSettings["gain"][1].get<float>();
					m_defaultPostProcessSettings.gain.z = ppSettings["gain"][2].get<float>();
				}
				
				if (ppSettings.contains("bloomThreshold"))
					m_defaultPostProcessSettings.bloomThreshold = ppSettings["bloomThreshold"].get<float>();
				if (ppSettings.contains("bloomKnee"))
					m_defaultPostProcessSettings.bloomKnee = ppSettings["bloomKnee"].get<float>();
				if (ppSettings.contains("bloomIntensity"))
					m_defaultPostProcessSettings.bloomIntensity = ppSettings["bloomIntensity"].get<float>();
				if (ppSettings.contains("bloomGaussianIntensity"))
					m_defaultPostProcessSettings.bloomGaussianIntensity = ppSettings["bloomGaussianIntensity"].get<float>();
				if (ppSettings.contains("bloomRadius"))
					m_defaultPostProcessSettings.bloomRadius = ppSettings["bloomRadius"].get<float>();
				if (ppSettings.contains("bloomDownsample"))
					m_defaultPostProcessSettings.bloomDownsample = ppSettings["bloomDownsample"].get<int>();

				ALICE_LOG_INFO("Default PostProcess Settings loaded from EngineSettings.json");
			}
		}
		catch (const std::exception& e)
		{
			ALICE_LOG_ERRORF("Exception while loading Default PostProcess Settings: %s", e.what());
		}
	}

	void EditorCore::DrawInspectorPostProcessVolume(World& world, const EntityId& _selectedEntity)
	{
		if (auto* volume = world.GetComponent<PostProcessVolumeComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Post Process Volume", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				// ==== Unbound 설정 (최상단) ====
				ImGui::Text("Volume Type");
				bool unboundBefore = volume->unbound;
				changed |= ImGui::Checkbox("Unbound (전역 적용)##PostProcessVolume", &volume->unbound);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Unbound: ON이면 항상 전역 적용 (무한 범위)\nOFF이면 Shape + BlendRadius 기반 공간 적용");
				
				if (volume->unbound)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[전역 적용 중]");
				}
				else
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "[공간 기반 적용]");
				}

				// Unbound 변경 시 DebugDrawBoxComponent 업데이트
				if (unboundBefore != volume->unbound)
				{
					world.UpdatePostProcessVolumeDebugBox(_selectedEntity, *volume);
				}

				ImGui::Separator();

				// ==== Bound 설정 (Unbound OFF일 때만 의미 있음) ====
				if (volume->unbound)
				{
					// Unbound ON: Bound 비활성화
					ImGui::BeginDisabled();
				}

				float blendRadius = volume->GetBlendRadius();
				if (ImGui::SliderFloat("Bound##PostProcessVolume", &blendRadius, 0.0f, 50.0f))
				{
					volume->SetBlendRadius(blendRadius);
					// DebugDrawBoxComponent bounds 업데이트
					world.UpdatePostProcessVolumeDebugBox(_selectedEntity, *volume);
					changed = true;
				}
				if (ImGui::IsItemHovered())
				{
					if (volume->unbound)
						ImGui::SetTooltip("Unbound가 켜져 있어 Bound는 적용되지 않습니다.");
					else
						ImGui::SetTooltip("보간이 적용되는 범위 (0이면 내부에서만 적용)");
				}

				if (volume->unbound)
				{
					ImGui::EndDisabled();
				}

				ImGui::Separator();

				// ==== Post Process Settings ====
				ImGui::Text("Post Process Settings");
				PostProcessSettings& settings = volume->settings;

				// Exposure
				if (ImGui::TreeNode("Exposure##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Exposure##PostProcessVolume", &settings.bOverride_Exposure);
					if (settings.bOverride_Exposure)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Exposure##PostProcessVolume", &settings.exposure, -3.0f, 3.0f, "%.2f");
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				// Max HDR Nits
				if (ImGui::TreeNode("Max HDR Nits##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Max HDR Nits##PostProcessVolume", &settings.bOverride_MaxHDRNits);
					if (settings.bOverride_MaxHDRNits)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Max HDR Nits##PostProcessVolume", &settings.maxHDRNits, 100.0f, 10000.0f, "%.0f nits");
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				// Color Grading
				if (ImGui::TreeNode("Color Grading##PostProcessVolume"))
				{
					// Saturation
					changed |= ImGui::Checkbox("Override Saturation##PostProcessVolume", &settings.bOverride_ColorGradingSaturation);
					if (settings.bOverride_ColorGradingSaturation)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Saturation (RGB)##PostProcessVolume", &settings.saturation.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Contrast
					changed |= ImGui::Checkbox("Override Contrast##PostProcessVolume", &settings.bOverride_ColorGradingContrast);
					if (settings.bOverride_ColorGradingContrast)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Contrast (RGB)##PostProcessVolume", &settings.contrast.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Gamma
					changed |= ImGui::Checkbox("Override Gamma##PostProcessVolume", &settings.bOverride_ColorGradingGamma);
					if (settings.bOverride_ColorGradingGamma)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Gamma (RGB)##PostProcessVolume", &settings.gamma.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Gain
					changed |= ImGui::Checkbox("Override Gain##PostProcessVolume", &settings.bOverride_ColorGradingGain);
					if (settings.bOverride_ColorGradingGain)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Gain (RGB)##PostProcessVolume", &settings.gain.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					ImGui::TreePop();
				}

				// Bloom
				if (ImGui::TreeNode("Bloom##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Threshold##PostProcessVolume", &settings.bOverride_BloomThreshold);
					if (settings.bOverride_BloomThreshold)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Threshold##PostProcessVolume", &settings.bloomThreshold, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Knee##PostProcessVolume", &settings.bOverride_BloomKnee);
					if (settings.bOverride_BloomKnee)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Knee##PostProcessVolume", &settings.bloomKnee, 0.0f, 1.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Intensity##PostProcessVolume", &settings.bOverride_BloomIntensity);
					if (settings.bOverride_BloomIntensity)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Intensity##PostProcessVolume", &settings.bloomIntensity, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Gaussian Intensity##PostProcessVolume", &settings.bOverride_BloomGaussianIntensity);
					if (settings.bOverride_BloomGaussianIntensity)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Gaussian Intensity##PostProcessVolume", &settings.bloomGaussianIntensity, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Radius##PostProcessVolume", &settings.bOverride_BloomRadius);
					if (settings.bOverride_BloomRadius)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Radius##PostProcessVolume", &settings.bloomRadius, 0.1f, 10.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Downsample##PostProcessVolume", &settings.bOverride_BloomDownsample);
					if (settings.bOverride_BloomDownsample)
					{
						ImGui::Indent();
						const char* downsampleNames[] = { "1x", "2x", "4x" };
						int downsampleValues[] = { 1, 2, 4 };
						int currentDownsample = settings.bloomDownsample;
						int currentIndex = 0;
						for (int i = 0; i < IM_ARRAYSIZE(downsampleValues); ++i)
						{
							if (downsampleValues[i] == currentDownsample)
							{
								currentIndex = i;
								break;
							}
						}
						if (ImGui::Combo("Downsample##PostProcessVolume", &currentIndex, downsampleNames, IM_ARRAYSIZE(downsampleNames)))
						{
							settings.bloomDownsample = downsampleValues[currentIndex];
							changed = true;
						}
						ImGui::Unindent();
					}

					ImGui::TreePop();
				}

				ImGui::Separator();

				// ==== 참조 오브젝트 설정 ====
				if (ImGui::TreeNode("Reference Object##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Use Reference Object##PostProcessVolume", &volume->useReferenceObject);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("PostProcessVolume 보간 기준을 참조 오브젝트로 사용할지 여부\n비활성화하면 카메라 위치 사용");

					if (volume->useReferenceObject)
					{
						ImGui::Indent();
						char nameBuf[256] = {};
						strncpy_s(nameBuf, volume->referenceObjectName.c_str(), sizeof(nameBuf) - 1);
						if (ImGui::InputText("Reference GameObject Name##PostProcessVolume", nameBuf, sizeof(nameBuf)))
						{
							volume->SetReferenceObjectName(nameBuf);
							changed = true;
						}
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("PostProcessVolume 보간 기준이 될 GameObject 이름\n비어있으면 카메라 위치 사용");

						if (!volume->referenceObjectName.empty())
						{
							GameObject refObj = world.FindGameObject(volume->referenceObjectName);
							if (refObj.IsValid())
							{
								auto* transform = world.GetComponent<TransformComponent>(refObj.id());
								if (transform && transform->enabled)
								{
									ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
										"Bound to: %s (Position: %.2f, %.2f, %.2f)",
										volume->referenceObjectName.c_str(),
										transform->position.x, transform->position.y, transform->position.z);
								}
								else
								{
									ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
										"Bound to: %s (Transform not found or disabled)", volume->referenceObjectName.c_str());
								}
							}
							else
							{
								ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
									"Object not found: %s (using camera position)", volume->referenceObjectName.c_str());
							}
						}
						else
						{
							ImGui::TextDisabled("No reference object set (using camera position)");
						}
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				if (ImGui::Button("Remove Post Process Volume"))
				{
					world.RemoveComponent<PostProcessVolumeComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorComputeEffect(World& world, const EntityId& _selectedEntity)
	{
		if (auto* effect = world.GetComponent<ComputeEffectComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Compute Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##ComputeEffect", &effect->enabled);

				// 파티클 타입 콤보박스
				const char* particleTypes[] = { "Particle", "Sparks", "Smoke", "Vortex", "Snow", "Explosion" };
				int currentIndex = 0;
				for (int i = 0; i < IM_ARRAYSIZE(particleTypes); ++i) {
					if (effect->shaderName == particleTypes[i]) {
						currentIndex = i;
						break;
					}
				}
				if (ImGui::Combo("Particle Type##ComputeEffect", &currentIndex, particleTypes, IM_ARRAYSIZE(particleTypes))) {
					effect->shaderName = particleTypes[currentIndex];
					changed = true;
				}

				// 위치 소스 선택
				changed |= ImGui::Checkbox("Use Transform##ComputeEffect", &effect->useTransform);

				if (effect->useTransform) {
					ImGui::Indent();
					changed |= ImGui::SliderFloat3("Local Offset##ComputeEffect", &effect->localOffset.x, -10.0f, 10.0f);
					ImGui::Unindent();
				}
				else {
					changed |= ImGui::SliderFloat3("Emitter Position (World)##ComputeEffect", &effect->effectParams.x, -100.0f, 100.0f);
				}

				// 이미터 파라미터
				changed |= ImGui::SliderFloat("Radius##ComputeEffect", &effect->radius, 0.01f, 5.0f);
				changed |= ImGui::ColorEdit3("Color##ComputeEffect", &effect->color.x);
				changed |= ImGui::SliderFloat("Size (px)##ComputeEffect", &effect->sizePx, 1.0f, 50.0f);
				changed |= ImGui::SliderFloat("Intensity##ComputeEffect", &effect->intensity, 0.0f, 10.0f);

				// 물리/수명 파라미터
				if (ImGui::TreeNode("Physics##ComputeEffect")) {
					changed |= ImGui::SliderFloat3("Gravity##ComputeEffect", &effect->gravity.x, -10.0f, 10.0f);
					changed |= ImGui::SliderFloat("Drag##ComputeEffect", &effect->drag, 0.0f, 1.0f);
					changed |= ImGui::SliderFloat("Life Min##ComputeEffect", &effect->lifeMin, 0.1f, 5.0f);
					changed |= ImGui::SliderFloat("Life Max##ComputeEffect", &effect->lifeMax, 0.1f, 10.0f);
					ImGui::TreePop();
				}

				// 기타 설정
				changed |= ImGui::Checkbox("Depth Test##ComputeEffect", &effect->depthTest);

				if (ImGui::Button("Remove Compute Effect")) {
					world.RemoveComponent<ComputeEffectComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	bool EditorCore::DrawLayerMaskEditor(const char* label, uint32_t& mask, const std::array<std::string, 32>& layerNames)
	{
		bool changed = false;
		ImGui::Text("%s", label);
		ImGui::Indent();

		// 최대 32개 레이어를 2열로 표시
		for (int i = 0; i < 32; i++)
		{
			bool bit = (mask & (1u << i)) != 0;
			std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
			std::string checkboxLabel = layerName + "##" + label + std::to_string(i);

			if (ImGui::Checkbox(checkboxLabel.c_str(), &bit))
			{
				if (bit)
					mask |= (1u << i);
				else
					mask &= ~(1u << i);
				changed = true;
			}

			// 2열로 배치
			if ((i + 1) % 2 == 0)
				ImGui::SameLine();
		}

		ImGui::Unindent();
		return changed;
	}

	bool EditorCore::DrawLayerMaskChipEditor(const char* label, uint32_t& mask, const std::array<std::string, 32>& layerNames)
	{
		bool changed = false;
		ImGui::Text("%s", label);
		ImGui::Indent();

		// 현재 선택된 레이어들을 칩으로 표시
		bool hasAnyLayers = false;
		for (int i = 0; i < 32; ++i)
		{
			if ((mask & (1u << i)) != 0)
			{
				hasAnyLayers = true;

				// 레이어 이름
				std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];

				// 칩 스타일 버튼 (레이어 이름) - 클릭하면 토글
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));

				std::string chipLabel = layerName + "##" + label + "_chip_" + std::to_string(i);
				if (ImGui::Button(chipLabel.c_str()))
				{
					mask &= ~(1u << i); // 토글: 제거
					changed = true;
				}

				ImGui::PopStyleColor(3);

				// 다음 줄로 넘어가기 위해
				ImGui::SameLine(0.0f, 4.0f);
			}
		}

		// 줄바꿈이 필요하면
		if (hasAnyLayers)
		{
			ImGui::NewLine();
		}

		// + 버튼 (레이어 추가)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));

		std::string addButtonLabel = "+##" + std::string(label) + "_add";
		if (ImGui::SmallButton(addButtonLabel.c_str()))
		{
			ImGui::OpenPopup((std::string("AddLayer##") + label).c_str());
		}

		ImGui::PopStyleColor(3);

		// 팝업: 레이어 선택 (선택되지 않은 레이어만 표시)
		if (ImGui::BeginPopup((std::string("AddLayer##") + label).c_str()))
		{
			ImGui::Text("Add Layer");
			ImGui::Separator();

			bool foundAny = false;
			for (int i = 0; i < 32; ++i)
			{
				if ((mask & (1u << i)) == 0) // 선택되지 않은 레이어만 표시
				{
					foundAny = true;
					std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
					if (ImGui::Selectable(layerName.c_str()))
					{
						mask |= (1u << i);
						changed = true;
						ImGui::CloseCurrentPopup();
					}
				}
			}

			if (!foundAny)
			{
				ImGui::TextDisabled("All layers are selected");
			}

			ImGui::EndPopup();
		}

		ImGui::Unindent();
		return changed;
	}

	bool EditorCore::DrawIgnoreLayersChipEditor(const char* label, uint32_t& ignoreLayers, const std::array<std::string, 32>& layerNames)
	{
		bool changed = false;
		ImGui::Text("%s", label);
		ImGui::Indent();

		// 현재 선택된 레이어들을 칩으로 표시
		bool hasAnyLayers = false;
		for (int i = 0; i < 32; ++i)
		{
			if ((ignoreLayers & (1u << i)) != 0)
			{
				hasAnyLayers = true;

				// 레이어 이름
				std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];

				// 칩 스타일 버튼 (레이어 이름) - 클릭해도 아무 일도 안 일어남 (표시만)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));

				std::string chipLabel = layerName + "##" + label + "_chip_" + std::to_string(i);
				ImGui::Button(chipLabel.c_str()); // 버튼으로 표시만 (클릭 비활성화)

				ImGui::PopStyleColor(3);

				ImGui::SameLine(0.0f, 4.0f);

				// [x] 버튼 (제거용)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));

				std::string removeLabel = std::string(" [x]##") + label + "_remove_" + std::to_string(i);
				if (ImGui::SmallButton(removeLabel.c_str()))
				{
					ignoreLayers &= ~(1u << i);
					changed = true;
				}

				ImGui::PopStyleColor(3);

				// 다음 줄로 넘어가기 위해
				ImGui::SameLine(0.0f, 0.0f);
			}
		}

		// 줄바꿈이 필요하면
		if (hasAnyLayers)
		{
			ImGui::NewLine();
		}

		// + 버튼 (레이어 추가)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));

		std::string addButtonLabel = "+##" + std::string(label) + "_add";
		if (ImGui::SmallButton(addButtonLabel.c_str()))
		{
			ImGui::OpenPopup((std::string("AddIgnoreLayer##") + label).c_str());
		}

		ImGui::PopStyleColor(3);

		// 팝업: 레이어 선택
		if (ImGui::BeginPopup((std::string("AddIgnoreLayer##") + label).c_str()))
		{
			ImGui::Text("Select layer to ignore:");
			ImGui::Separator();

			for (int i = 0; i < 32; ++i)
			{
				// 이미 추가된 레이어는 표시하지 않음
				if ((ignoreLayers & (1u << i)) != 0)
					continue;

				std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
				std::string selectLabel = layerName + "##" + label + "_select_" + std::to_string(i);

				if (ImGui::Selectable(selectLabel.c_str()))
				{
					ignoreLayers |= (1u << i);
					changed = true;
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::EndPopup();
		}

		ImGui::Unindent();
		return changed;
	}

	void EditorCore::DrawInspectorCollider(World& world, const EntityId& _selectedEntity)
	{
		if (auto* collider = world.GetComponent<Phy_ColliderComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##ColliderRemove"))
				{
					world.RemoveComponent<Phy_ColliderComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				changed |= ImGui::Checkbox("Debug Draw", &collider->debugDraw);

				// Collider Type 선택
				ImGui::Text("Collider Type");
				ImGui::Indent();
				{
					const char* typeLabels[] = { "Box", "Sphere", "Capsule" };
					int typeIndex = static_cast<int>(collider->type);
					if (ImGui::Combo("##ColliderType", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels)))
					{
						collider->type = static_cast<ColliderType>(typeIndex);
						changed = true;
					}
				}
				ImGui::Unindent();

				// 기본 프로퍼티는 ReflectionUI로
				// Collider 편집 이벤트 처리
				static EntityId lastEditedColliderEntity = InvalidEntityId;
				static JsonRttr::json colliderEditStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*collider, [](const std::string& name) {
					// type, layerBits는 커스텀 UI로 처리 (collideMask/queryMask는 레이어 매트릭스로만 결정)
					return name != "type" && name != "layerBits" && name != "physicsActorHandle";
				});

				// 편집 시작
				if (event.activated && _selectedEntity != lastEditedColliderEntity)
				{
					rttr::instance inst = *collider;
					colliderEditStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedColliderEntity = _selectedEntity;
				}

				// 편집 종료
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedColliderEntity)
				{
					rttr::instance inst = *collider;
					JsonRttr::json colliderEditEndJson = JsonRttr::ToJsonObject(inst);

					if (colliderEditStartJson != colliderEditEndJson)
					{
						Phy_ColliderComponent oldCollider, newCollider;
						rttr::instance oldInst = oldCollider;
						rttr::instance newInst = newCollider;
						JsonRttr::FromJsonObject(oldInst, colliderEditStartJson);
						JsonRttr::FromJsonObject(newInst, colliderEditEndJson);
						PushCommand(std::make_unique<ComponentEditCommand<Phy_ColliderComponent>>(_selectedEntity, oldCollider, newCollider));
						g_SceneDirty = true;
					}

					lastEditedColliderEntity = InvalidEntityId;
				}

				changed |= event.changed;

				// 레이어 마스크 편집
				ImGui::Separator();
				ImGui::Text("Layer Settings");

				// Phy_SettingsComponent에서 레이어 이름 가져오기
				std::array<std::string, 32> layerNames;
				for (int i = 0; i < 32; ++i)
					layerNames[i] = "Layer " + std::to_string(i);

				const auto& settingsMap = world.GetComponents<Phy_SettingsComponent>();
				if (!settingsMap.empty())
				{
					const auto& settings = settingsMap.begin()->second;
					layerNames = settings.layerNames;
				}

				// Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능
				// 주의: PhysX 엔진은 32개 레이어를 지원하지만, 에디터 UI는 편의상 16개만 표시합니다.
				ImGui::Text("Layer");
				ImGui::Indent();
				{
					// 현재 선택된 레이어 찾기
					int currentLayer = -1;
					for (int i = 0; i < 16; ++i) // 16개만 확인 (UI 제한)
					{
						if ((collider->layerBits & (1u << i)) != 0)
						{
							currentLayer = i;
							break;
						}
					}

					// 16개 이상의 레이어가 선택되어 있으면 초기화
					if (currentLayer == -1 && collider->layerBits != 0)
					{
						collider->layerBits = 0;
						changed = true;
					}

					// ComboBox로 레이어 선택
					std::string preview = (currentLayer >= 0) ?
						(layerNames[currentLayer].empty() ? ("Layer " + std::to_string(currentLayer)) : layerNames[currentLayer]) :
						"None";

					if (ImGui::BeginCombo("##LayerBits", preview.c_str()))
					{
						if (ImGui::Selectable("None", currentLayer == -1))
						{
							collider->layerBits = 0;
							changed = true;
						}
						for (int i = 0; i < 16; ++i) // 16개만 표시
						{
							std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
							bool isSelected = (currentLayer == i);
							if (ImGui::Selectable(layerName.c_str(), isSelected))
							{
								collider->layerBits = (1u << i); // 단일 레이어만 설정
								changed = true;
							}
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Unindent();

				// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)
				ImGui::TextDisabled("(Collide/Query Mask는 Physics Scene Settings의 레이어 매트릭스로 결정됩니다)");

				// Ignore Layers (칩 UI)
				ImGui::Text("Ignore Layers");
				ImGui::Indent();
				changed |= DrawIgnoreLayersChipEditor("IgnoreLayers", collider->ignoreLayers, layerNames);
				ImGui::Unindent();

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorMeshCollider(World& world, const EntityId& _selectedEntity)
	{
		if (auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Mesh Collider", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##MeshColliderRemove"))
				{
					world.RemoveComponent<Phy_MeshColliderComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				changed |= ImGui::Checkbox("Debug Draw", &meshCollider->debugDraw);

				// Mesh Collider Type 선택
				ImGui::Text("Mesh Collider Type");
				ImGui::Indent();
				{
					const char* typeLabels[] = { "Triangle", "Convex" };
					int typeIndex = static_cast<int>(meshCollider->type);
					if (ImGui::Combo("##MeshColliderType", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels)))
					{
						meshCollider->type = static_cast<MeshColliderType>(typeIndex);
						changed = true;
					}
				}
				ImGui::Unindent();

				// MeshCollider 편집 이벤트 처리
				static EntityId lastEditedMeshColliderEntity = InvalidEntityId;
				static JsonRttr::json meshColliderEditStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*meshCollider, [](const std::string& name) {
					return name != "type" && name != "layerBits" && name != "collideMask" && name != "queryMask" &&
						name != "ignoreLayers" && name != "physicsActorHandle" &&
						name != "flipNormals" && name != "doubleSidedQueries" && name != "validate" &&
						name != "shiftVertices" && name != "vertexLimit";
				});

				// 편집 시작
				if (event.activated && _selectedEntity != lastEditedMeshColliderEntity)
				{
					rttr::instance inst = *meshCollider;
					meshColliderEditStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedMeshColliderEntity = _selectedEntity;
				}

				// 편집 종료
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedMeshColliderEntity)
				{
					rttr::instance inst = *meshCollider;
					JsonRttr::json meshColliderEditEndJson = JsonRttr::ToJsonObject(inst);

					if (meshColliderEditStartJson != meshColliderEditEndJson)
					{
						Phy_MeshColliderComponent oldMeshCollider, newMeshCollider;
						rttr::instance oldInst = oldMeshCollider;
						rttr::instance newInst = newMeshCollider;
						JsonRttr::FromJsonObject(oldInst, meshColliderEditStartJson);
						JsonRttr::FromJsonObject(newInst, meshColliderEditEndJson);
						PushCommand(std::make_unique<ComponentEditCommand<Phy_MeshColliderComponent>>(_selectedEntity, oldMeshCollider, newMeshCollider));
						g_SceneDirty = true;
					}

					lastEditedMeshColliderEntity = InvalidEntityId;
				}

				changed |= event.changed;

				ImGui::Separator();
				ImGui::TextUnformatted("Mesh Options");

				// Mesh Asset 선택 (ComboBox로 이미 로드된 메시 목록 표시)
				ImGui::Text("Mesh Asset");
				ImGui::Indent();
				{
					// 현재 선택된 메시 경로
					std::string currentPath = meshCollider->meshAssetPath;

					// 동일 엔티티의 SkinnedMeshComponent 확인
					const auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity);
					bool useSkinnedMesh = currentPath.empty() && skinned && !skinned->meshAssetPath.empty();

					if (useSkinnedMesh)
						currentPath = skinned->meshAssetPath;

					// Preview 텍스트
					std::string preview = "Auto (Use SkinnedMeshComponent)";
					if (!currentPath.empty())
						preview = currentPath;

					if (ImGui::BeginCombo("##MeshAssetPath", preview.c_str()))
					{
						// Auto 옵션 (SkinnedMeshComponent 사용)
						bool isAuto = meshCollider->meshAssetPath.empty();
						if (ImGui::Selectable("Auto (Use SkinnedMeshComponent)", isAuto))
						{
							meshCollider->meshAssetPath.clear();
							changed = true;
						}
						if (isAuto)
							ImGui::SetItemDefaultFocus();

						// 이미 로드된 메시 목록 표시 (SkinnedMeshRegistry에서)
						if (m_skinnedRegistry)
						{
							// SkinnedMeshComponent가 있는 모든 엔티티를 순회하여 등록된 메시 수집
							std::set<std::string> registeredMeshes;
							auto skinnedComps = world.GetComponents<SkinnedMeshComponent>();
							for (const auto& [eid, comp] : skinnedComps)
							{
								if (!comp.meshAssetPath.empty())
								{
									// 레지스트리에 실제로 등록되어 있는지 확인
									if (m_skinnedRegistry->Find(comp.meshAssetPath))
										registeredMeshes.insert(comp.meshAssetPath);
								}
							}

							// 등록된 메시 목록 표시
							for (const auto& meshPath : registeredMeshes)
							{
								bool isSelected = (currentPath == meshPath);
								if (ImGui::Selectable(meshPath.c_str(), isSelected))
								{
									meshCollider->meshAssetPath = meshPath;
									changed = true;
								}
								if (isSelected)
									ImGui::SetItemDefaultFocus();
							}
						}
						else
						{
							ImGui::TextDisabled("(SkinnedMeshRegistry not available)");
						}

						ImGui::EndCombo();
					}

					// 현재 상태 표시
					if (meshCollider->meshAssetPath.empty())
					{
						if (skinned && !skinned->meshAssetPath.empty())
						{
							ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
								"Using: %s", skinned->meshAssetPath.c_str());
						}
						else
						{
							ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
								"No SkinnedMeshComponent found on this entity");
						}
					}
				}
				ImGui::Unindent();

				if (meshCollider->type == MeshColliderType::Triangle)
				{
					changed |= ImGui::Checkbox("Flip Normals", &meshCollider->flipNormals);
					changed |= ImGui::Checkbox("Double-Sided Queries", &meshCollider->doubleSidedQueries);
					changed |= ImGui::Checkbox("Validate (Debug)", &meshCollider->validate);
				}
				else
				{
					changed |= ImGui::Checkbox("Shift Vertices", &meshCollider->shiftVertices);
					changed |= ImGui::InputScalar("Vertex Limit", ImGuiDataType_U32, &meshCollider->vertexLimit);
					changed |= ImGui::Checkbox("Validate (Debug)", &meshCollider->validate);
				}

				// 레이어 마스크 편집
				ImGui::Separator();
				ImGui::Text("Layer Settings");

				std::array<std::string, 32> layerNames;
				for (int i = 0; i < 32; ++i)
					layerNames[i] = "Layer " + std::to_string(i);

				const auto& settingsMap = world.GetComponents<Phy_SettingsComponent>();
				if (!settingsMap.empty())
				{
					const auto& settings = settingsMap.begin()->second;
					layerNames = settings.layerNames;
				}

				ImGui::Text("Layer");
				ImGui::Indent();
				{
					int currentLayer = -1;
					for (int i = 0; i < 16; ++i)
					{
						if ((meshCollider->layerBits & (1u << i)) != 0)
						{
							currentLayer = i;
							break;
						}
					}

					if (currentLayer == -1 && meshCollider->layerBits != 0)
					{
						meshCollider->layerBits = 0;
						changed = true;
					}

					std::string preview = (currentLayer >= 0) ?
						(layerNames[currentLayer].empty() ? ("Layer " + std::to_string(currentLayer)) : layerNames[currentLayer]) :
						"None";

					if (ImGui::BeginCombo("##MeshColliderLayerBits", preview.c_str()))
					{
						if (ImGui::Selectable("None", currentLayer == -1))
						{
							meshCollider->layerBits = 0;
							changed = true;
						}
						for (int i = 0; i < 16; ++i)
						{
							std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
							bool isSelected = (currentLayer == i);
							if (ImGui::Selectable(layerName.c_str(), isSelected))
							{
								meshCollider->layerBits = (1u << i);
								changed = true;
							}
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Unindent();

				// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)
				ImGui::TextDisabled("(Collide/Query Mask는 Physics Scene Settings의 레이어 매트릭스로 결정됩니다)");

				ImGui::Text("Ignore Layers");
				ImGui::Indent();
				changed |= DrawIgnoreLayersChipEditor("MeshIgnoreLayers", meshCollider->ignoreLayers, layerNames);
				ImGui::Unindent();

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCharacterController(World& world, const EntityId& _selectedEntity)
	{
		if (auto* cct = world.GetComponent<Phy_CCTComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##CCTRemove"))
				{
					world.RemoveComponent<Phy_CCTComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				// 기본 프로퍼티는 ReflectionUI로
				// CCT 편집 이벤트 처리
				static EntityId lastEditedCCTEntity = InvalidEntityId;
				static JsonRttr::json cctEditStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*cct, [](const std::string& name) {
					// layerBits는 커스텀 UI로 처리 (collideMask/queryMask는 레이어 매트릭스로만 결정)
					return name != "layerBits" && name != "controllerHandle";
				});

				// 편집 시작
				if (event.activated && _selectedEntity != lastEditedCCTEntity)
				{
					rttr::instance inst = *cct;
					cctEditStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedCCTEntity = _selectedEntity;
				}

				// 편집 종료
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedCCTEntity)
				{
					rttr::instance inst = *cct;
					JsonRttr::json cctEditEndJson = JsonRttr::ToJsonObject(inst);

					if (cctEditStartJson != cctEditEndJson)
					{
						Phy_CCTComponent oldCCT, newCCT;
						rttr::instance oldInst = oldCCT;
						rttr::instance newInst = newCCT;
						JsonRttr::FromJsonObject(oldInst, cctEditStartJson);
						JsonRttr::FromJsonObject(newInst, cctEditEndJson);
						PushCommand(std::make_unique<ComponentEditCommand<Phy_CCTComponent>>(_selectedEntity, oldCCT, newCCT));
						g_SceneDirty = true;
					}

					lastEditedCCTEntity = InvalidEntityId;
				}

				changed |= event.changed;

				// 레이어 마스크 편집
				ImGui::Separator();
				ImGui::Text("Layer Settings");

				// Phy_SettingsComponent에서 레이어 이름 가져오기
				std::array<std::string, 32> layerNames;
				for (int i = 0; i < 32; ++i)
					layerNames[i] = "Layer " + std::to_string(i);

				const auto& settingsMap = world.GetComponents<Phy_SettingsComponent>();
				if (!settingsMap.empty())
				{
					const auto& settings = settingsMap.begin()->second;
					layerNames = settings.layerNames;
				}

				// Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능
				// 주의: PhysX 엔진은 32개 레이어를 지원하지만, 에디터 UI는 편의상 16개만 표시합니다.
				ImGui::Text("Layer");
				ImGui::Indent();
				{
					// 현재 선택된 레이어 찾기
					int currentLayer = -1;
					for (int i = 0; i < 16; ++i) // 16개만 확인 (UI 제한)
					{
						if ((cct->layerBits & (1u << i)) != 0)
						{
							currentLayer = i;
							break;
						}
					}

					// 16개 이상의 레이어가 선택되어 있으면 초기화
					if (currentLayer == -1 && cct->layerBits != 0)
					{
						cct->layerBits = 0;
						changed = true;
					}

					// ComboBox로 레이어 선택
					std::string preview = (currentLayer >= 0) ?
						(layerNames[currentLayer].empty() ? ("Layer " + std::to_string(currentLayer)) : layerNames[currentLayer]) :
						"None";

					if (ImGui::BeginCombo("##LayerBits", preview.c_str()))
					{
						if (ImGui::Selectable("None", currentLayer == -1))
						{
							cct->layerBits = 0;
							changed = true;
						}
						for (int i = 0; i < 16; ++i) // 16개만 표시
						{
							std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
							bool isSelected = (currentLayer == i);
							if (ImGui::Selectable(layerName.c_str(), isSelected))
							{
								cct->layerBits = (1u << i); // 단일 레이어만 설정
								changed = true;
							}
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Unindent();

				// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)
				ImGui::TextDisabled("(Collide/Query Mask는 Physics Scene Settings의 레이어 매트릭스로 결정됩니다)");

				// Ignore Layers (칩 UI)
				ImGui::Text("Ignore Layers");
				ImGui::Indent();
				changed |= DrawIgnoreLayersChipEditor("IgnoreLayers", cct->ignoreLayers, layerNames);
				ImGui::Unindent();

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorPhysicsSceneSettings(World& world, const EntityId& _selectedEntity)
	{
		if (auto* settings = world.GetComponent<Phy_SettingsComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Physics Scene Settings", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##PhysicsSettingsRemove"))
				{
					world.RemoveComponent<Phy_SettingsComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (ImGui::Button("Apply Combat Defaults"))
				{
					CombatPhysicsLayers::ApplyDefaultCombatLayerMatrix(*settings);
					settings->filterRevision++;
					changed = true;
					g_SceneDirty = true;
				}

				// 기본 프로퍼티는 ReflectionUI로
				// PhysicsSettings 편집 이벤트 처리
				static EntityId lastEditedPhysicsSettingsEntity = InvalidEntityId;
				static JsonRttr::json physicsSettingsEditStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*settings, [](const std::string& name) {
					// layerCollideMatrix, layerQueryMatrix, layerNames는 커스텀 UI로 처리
					return name != "layerCollideMatrix" && name != "layerQueryMatrix" && name != "layerNames" &&
						name != "enableGroundPlane" && name != "groundStaticFriction" && name != "groundDynamicFriction" &&
						name != "groundRestitution" && name != "groundLayerBits" && name != "groundCollideMask" &&
						name != "groundQueryMask" && name != "groundIgnoreLayers" && name != "groundIsTrigger";
				});

				// 편집 시작
				if (event.activated && _selectedEntity != lastEditedPhysicsSettingsEntity)
				{
					rttr::instance inst = *settings;
					physicsSettingsEditStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedPhysicsSettingsEntity = _selectedEntity;
				}

				// 편집 종료
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedPhysicsSettingsEntity)
				{
					rttr::instance inst = *settings;
					JsonRttr::json physicsSettingsEditEndJson = JsonRttr::ToJsonObject(inst);

					if (physicsSettingsEditStartJson != physicsSettingsEditEndJson)
					{
						Phy_SettingsComponent oldSettings, newSettings;
						rttr::instance oldInst = oldSettings;
						rttr::instance newInst = newSettings;
						JsonRttr::FromJsonObject(oldInst, physicsSettingsEditStartJson);
						JsonRttr::FromJsonObject(newInst, physicsSettingsEditEndJson);
						PushCommand(std::make_unique<ComponentEditCommand<Phy_SettingsComponent>>(_selectedEntity, oldSettings, newSettings));
						g_SceneDirty = true;
					}

					lastEditedPhysicsSettingsEntity = InvalidEntityId;
				}

				changed |= event.changed;

				ImGui::Separator();
				ImGui::Text("Ground Plane (y=0)");
				changed |= ImGui::Checkbox("Enable Ground Plane", &settings->enableGroundPlane);

				if (settings->enableGroundPlane)
				{
					changed |= ImGui::DragFloat("Static Friction", &settings->groundStaticFriction, 0.01f, 0.0f, 10.0f);
					changed |= ImGui::DragFloat("Dynamic Friction", &settings->groundDynamicFriction, 0.01f, 0.0f, 10.0f);
					changed |= ImGui::DragFloat("Restitution", &settings->groundRestitution, 0.01f, 0.0f, 1.0f);
					changed |= ImGui::Checkbox("Trigger", &settings->groundIsTrigger);

					std::array<std::string, 32> layerNames = settings->layerNames;

					ImGui::Text("Layer");
					ImGui::Indent();
					{
						int currentLayer = -1;
						for (int i = 0; i < 16; ++i)
						{
							if ((settings->groundLayerBits & (1u << i)) != 0)
							{
								currentLayer = i;
								break;
							}
						}

						if (currentLayer == -1 && settings->groundLayerBits != 0)
						{
							settings->groundLayerBits = 0;
							changed = true;
						}

						std::string preview = (currentLayer >= 0) ?
							(layerNames[currentLayer].empty() ? ("Layer " + std::to_string(currentLayer)) : layerNames[currentLayer]) :
							"None";

						if (ImGui::BeginCombo("##GroundLayerBits", preview.c_str()))
						{
							if (ImGui::Selectable("None", currentLayer == -1))
							{
								settings->groundLayerBits = 0;
								changed = true;
							}
							for (int i = 0; i < 16; ++i)
							{
								std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
								bool isSelected = (currentLayer == i);
								if (ImGui::Selectable(layerName.c_str(), isSelected))
								{
									settings->groundLayerBits = (1u << i);
									changed = true;
								}
								if (isSelected)
									ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
					}
					ImGui::Unindent();

					changed |= DrawLayerMaskChipEditor("Ground Collide Mask", settings->groundCollideMask, layerNames);
					changed |= DrawLayerMaskChipEditor("Ground Query Mask", settings->groundQueryMask, layerNames);

					ImGui::Text("Ground Ignore Layers");
					ImGui::Indent();
					changed |= DrawIgnoreLayersChipEditor("GroundIgnoreLayers", settings->groundIgnoreLayers, layerNames);
					ImGui::Unindent();
				}

				ImGui::Separator();
				ImGui::Text("Layer Collision Matrix");
				ImGui::Text("(Collide Mask: Check = Collision enabled between layers)");

				// 레이어 충돌 매트릭스 편집
				// 주의: PhysX 엔진은 32개 레이어를 지원하지만, 에디터 UI는 편의상 16개만 표시합니다.
				ImGui::BeginChild("LayerCollideMatrix", ImVec2(0, 400), false, ImGuiWindowFlags_HorizontalScrollbar);

				// 행 단위로 표시: "00 | 00 [ ] 01 [ ] 02 [ ] 03 [ ]"
				for (int i = 0; i < 16; ++i)
				{
					// 행 번호 표시 (2자리로 포맷팅)
					char rowLabel[8];
					snprintf(rowLabel, sizeof(rowLabel), "%02d |", i);
					ImGui::Text("%s", rowLabel);
					ImGui::SameLine();

					// 해당 행의 모든 열에 대한 체크박스 표시
					for (int j = 0; j < 16; ++j)
					{
						bool collision = settings->layerCollideMatrix[i][j];
						char colLabel[8];
						snprintf(colLabel, sizeof(colLabel), "%02d", j);
						ImGui::PushID(i * 16 + j);
						if (ImGui::Checkbox(colLabel, &collision))
						{
							settings->layerCollideMatrix[i][j] = collision;
							settings->layerCollideMatrix[j][i] = collision; // 충돌은 대칭 (필수)
							settings->filterRevision++; // 필터 변경 감지용
							changed = true;
						}
						ImGui::PopID();
						ImGui::SameLine();
					}
					ImGui::NewLine();
				}

				ImGui::EndChild();

				ImGui::Separator();
				ImGui::Text("Layer Query Matrix");
				ImGui::Text("(Query Mask: Check = Query enabled between layers)");

				// 레이어 쿼리 매트릭스 편집
				// 주의: PhysX 엔진은 32개 레이어를 지원하지만, 에디터 UI는 편의상 16개만 표시합니다.
				ImGui::BeginChild("LayerQueryMatrix", ImVec2(0, 400), false, ImGuiWindowFlags_HorizontalScrollbar);

				// 행 단위로 표시: "00 | 00 [ ] 01 [ ] 02 [ ] 03 [ ]"
				for (int i = 0; i < 16; ++i)
				{
					// 행 번호 표시 (2자리로 포맷팅)
					char rowLabel[8];
					snprintf(rowLabel, sizeof(rowLabel), "%02d |", i);
					ImGui::Text("%s", rowLabel);
					ImGui::SameLine();

					// 해당 행의 모든 열에 대한 체크박스 표시
					for (int j = 0; j < 16; ++j)
					{
						bool query = settings->layerQueryMatrix[i][j];
						char colLabel[8];
						snprintf(colLabel, sizeof(colLabel), "%02d", j);
						ImGui::PushID(10000 + i * 16 + j);
						if (ImGui::Checkbox(colLabel, &query))
						{
							settings->layerQueryMatrix[i][j] = query;
							// 쿼리는 비대칭이 유용한 경우가 많으므로 대칭 적용 제거
							// (예: 카메라 레이는 특정 레이어만 보고, AI는 또 다르게 봄)
							settings->filterRevision++; // 필터 변경 감지용
							changed = true;
						}
						ImGui::PopID();
						ImGui::SameLine();
					}
					ImGui::NewLine();
				}

				ImGui::EndChild();

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorTerrainHeightField(World& world, const EntityId& _selectedEntity)
	{
		if (auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Terrain Height Field", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##TerrainRemove"))
				{
					world.RemoveComponent<Phy_TerrainHeightFieldComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				// 기본 프로퍼티는 ReflectionUI로
				// Terrain 편집 이벤트 처리
				static EntityId lastEditedTerrainEntity = InvalidEntityId;
				static JsonRttr::json terrainEditStartJson;

				ReflectionUI::UIEditEvent event = ReflectionUI::RenderInspector(*terrain, [](const std::string& name) {
					// layerBits, heightSamples는 커스텀 UI로 처리 (collideMask/queryMask는 레이어 매트릭스로만 결정)
					return name != "layerBits" && name != "heightSamples" && name != "physicsActorHandle";
				});

				// 편집 시작
				if (event.activated && _selectedEntity != lastEditedTerrainEntity)
				{
					rttr::instance inst = *terrain;
					terrainEditStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedTerrainEntity = _selectedEntity;
				}

				// 편집 종료
				if (event.deactivatedAfterEdit && _selectedEntity == lastEditedTerrainEntity)
				{
					rttr::instance inst = *terrain;
					JsonRttr::json terrainEditEndJson = JsonRttr::ToJsonObject(inst);

					if (terrainEditStartJson != terrainEditEndJson)
					{
						Phy_TerrainHeightFieldComponent oldTerrain, newTerrain;
						rttr::instance oldInst = oldTerrain;
						rttr::instance newInst = newTerrain;
						JsonRttr::FromJsonObject(oldInst, terrainEditStartJson);
						JsonRttr::FromJsonObject(newInst, terrainEditEndJson);
						PushCommand(std::make_unique<ComponentEditCommand<Phy_TerrainHeightFieldComponent>>(_selectedEntity, oldTerrain, newTerrain));
						g_SceneDirty = true;
					}

					lastEditedTerrainEntity = InvalidEntityId;
				}

				changed |= event.changed;

				// HeightSamples 상태 표시 및 생성 버튼
				ImGui::Separator();
				ImGui::Text("Height Samples");
				ImGui::Indent();
				{
					const size_t expectedSamples = static_cast<size_t>(terrain->numRows) * static_cast<size_t>(terrain->numCols);
					const bool isValid = (terrain->numRows >= 2 && terrain->numCols >= 2) &&
						(terrain->heightSamples.size() == expectedSamples);

					if (terrain->heightSamples.empty())
					{
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: Empty (requires data)");
					}
					else if (!isValid)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
							"Status: Invalid (size: %zu, expected: %zu)",
							terrain->heightSamples.size(), expectedSamples);
					}
					else
					{
						ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
							"Status: Valid (size: %zu)",
							terrain->heightSamples.size());
					}

					ImGui::Text("Grid: %u x %u (total: %zu samples)",
						terrain->numRows, terrain->numCols, expectedSamples);

					if (terrain->numRows >= 2 && terrain->numCols >= 2)
					{
						if (ImGui::Button("Generate Flat (0.0)"))
						{
							terrain->heightSamples.resize(expectedSamples, 0.0f);
							changed = true;
							g_SceneDirty = true;
						}
						ImGui::SameLine();
						ImGui::TextDisabled("(?)");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::Text("Generates a flat terrain with all heights set to 0.0");
							ImGui::EndTooltip();
						}
					}
					else
					{
						ImGui::TextDisabled("Set numRows and numCols (>= 2) to enable generation");
					}
				}
				ImGui::Unindent();

				// 레이어 마스크 편집
				ImGui::Separator();
				ImGui::Text("Layer Settings");

				// Phy_SettingsComponent에서 레이어 이름 가져오기
				std::array<std::string, 32> layerNames;
				for (int i = 0; i < 32; ++i)
					layerNames[i] = "Layer " + std::to_string(i);

				const auto& settingsMap = world.GetComponents<Phy_SettingsComponent>();
				if (!settingsMap.empty())
				{
					const auto& settings = settingsMap.begin()->second;
					layerNames = settings.layerNames;
				}

				// Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능
				// 주의: PhysX 엔진은 32개 레이어를 지원하지만, 에디터 UI는 편의상 16개만 표시합니다.
				ImGui::Text("Layer");
				ImGui::Indent();
				{
					// 현재 선택된 레이어 찾기
					int currentLayer = -1;
					for (int i = 0; i < 16; ++i) // 16개만 확인 (UI 제한)
					{
						if ((terrain->layerBits & (1u << i)) != 0)
						{
							currentLayer = i;
							break;
						}
					}

					// 16개 이상의 레이어가 선택되어 있으면 초기화
					if (currentLayer == -1 && terrain->layerBits != 0)
					{
						terrain->layerBits = 0;
						changed = true;
					}

					// ComboBox로 레이어 선택
					std::string preview = (currentLayer >= 0) ?
						(layerNames[currentLayer].empty() ? ("Layer " + std::to_string(currentLayer)) : layerNames[currentLayer]) :
						"None";

					if (ImGui::BeginCombo("##LayerBits", preview.c_str()))
					{
						if (ImGui::Selectable("None", currentLayer == -1))
						{
							terrain->layerBits = 0;
							changed = true;
						}
						for (int i = 0; i < 16; ++i) // 16개만 표시
						{
							std::string layerName = layerNames[i].empty() ? ("Layer " + std::to_string(i)) : layerNames[i];
							bool isSelected = (currentLayer == i);
							if (ImGui::Selectable(layerName.c_str(), isSelected))
							{
								terrain->layerBits = (1u << i); // 단일 레이어만 설정
								changed = true;
							}
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				ImGui::Unindent();

				// collideMask/queryMask는 레이어 매트릭스로만 결정됨 (컴포넌트에서 제거됨)
				ImGui::TextDisabled("(Collide/Query Mask는 Physics Scene Settings의 레이어 매트릭스로 결정됩니다)");

				// Ignore Layers (칩 UI)
				ImGui::Text("Ignore Layers");
				ImGui::Indent();
				changed |= DrawIgnoreLayersChipEditor("IgnoreLayers", terrain->ignoreLayers, layerNames);
				ImGui::Unindent();

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorJoint(World& world, const EntityId& _selectedEntity)
	{
		if (auto* joint = world.GetComponent<Phy_JointComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Joint", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##JointRemove"))
				{
					world.RemoveComponent<Phy_JointComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				const char* typeLabels[] = { "Fixed", "Revolute", "Prismatic", "Distance", "Spherical", "D6" };
				int typeIndex = static_cast<int>(joint->type);
				if (ImGui::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels)))
				{
					joint->type = static_cast<Phy_JointType>(typeIndex);
					changed = true;
				}

				// Target Entity 선택 (ComboBox)
				ImGui::Text("Target Entity");
				ImGui::Indent();
				{
					// 현재 타겟 엔티티 찾기
					EntityId currentTargetId = Alice::InvalidEntityId;
					std::string currentTargetName = joint->targetName;
					if (!currentTargetName.empty())
					{
						GameObject targetGo = world.FindGameObject(currentTargetName);
						if (targetGo.IsValid())
							currentTargetId = targetGo.id();
					}

					// Preview 텍스트 생성
					std::string preview = "None";
					if (currentTargetId != Alice::InvalidEntityId)
					{
						std::string name = world.GetEntityName(currentTargetId);
						if (name.empty())
							name = "Entity " + std::to_string((uint32_t)currentTargetId);
						preview = name;
					}
					else if (!currentTargetName.empty())
					{
						preview = currentTargetName + " (not found)";
					}

					if (ImGui::BeginCombo("##JointTarget", preview.c_str()))
					{
						// None 옵션
						if (ImGui::Selectable("None", currentTargetId == Alice::InvalidEntityId))
						{
							joint->targetName.clear();
							changed = true;
						}
						if (currentTargetId == Alice::InvalidEntityId)
							ImGui::SetItemDefaultFocus();

						// 모든 엔티티 나열
						auto transforms = world.GetComponents<TransformComponent>();
						for (const auto& [entityId, transform] : transforms)
						{
							std::string name = world.GetEntityName(entityId);
							if (name.empty())
								name = "Entity " + std::to_string((uint32_t)entityId);

							bool isSelected = (currentTargetId == entityId);
							if (ImGui::Selectable(name.c_str(), isSelected))
							{
								joint->targetName = name;
								changed = true;
							}
							if (isSelected)
								ImGui::SetItemDefaultFocus();
						}

						ImGui::EndCombo();
					}
				}
				ImGui::Unindent();

				ImGui::Separator();
				ImGui::Text("Common");
				changed |= ImGui::Checkbox("Collide Connected", &joint->collideConnected);
				changed |= ImGui::DragFloat("Break Force", &joint->breakForce, 1.0f, 0.0f);
				changed |= ImGui::DragFloat("Break Torque", &joint->breakTorque, 1.0f, 0.0f);

				auto drawFrame = [&](const char* label, Phy_JointFrame& frame) -> bool
				{
					bool frameChanged = false;
					if (ImGui::TreeNode(label))
					{
						frameChanged |= ImGui::DragFloat3("Position", &frame.position.x, 0.01f);
						frameChanged |= ImGui::DragFloat3("Rotation (Rad)", &frame.rotation.x, 0.01f);
						ImGui::TreePop();
					}
					return frameChanged;
				};

				changed |= drawFrame("Frame A", joint->frameA);
				changed |= drawFrame("Frame B", joint->frameB);

				ImGui::Separator();
				switch (joint->type)
				{
				case Phy_JointType::Fixed:
					ImGui::Text("Fixed Joint: no extra settings");
					break;
				case Phy_JointType::Revolute:
				{
					if (ImGui::TreeNode("Revolute Limit"))
					{
						changed |= ImGui::Checkbox("Enable Limit", &joint->revolute.enableLimit);
						changed |= ImGui::DragFloat("Lower Limit", &joint->revolute.lowerLimit, 0.01f);
						changed |= ImGui::DragFloat("Upper Limit", &joint->revolute.upperLimit, 0.01f);
						changed |= ImGui::DragFloat("Stiffness", &joint->revolute.limitStiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping", &joint->revolute.limitDamping, 0.01f);
						changed |= ImGui::DragFloat("Restitution", &joint->revolute.limitRestitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold", &joint->revolute.limitBounceThreshold, 0.01f);
						ImGui::TreePop();
					}
					if (ImGui::TreeNode("Revolute Drive"))
					{
						changed |= ImGui::Checkbox("Enable Drive", &joint->revolute.enableDrive);
						changed |= ImGui::DragFloat("Drive Velocity", &joint->revolute.driveVelocity, 0.01f);
						changed |= ImGui::DragFloat("Force Limit", &joint->revolute.driveForceLimit, 1.0f, 0.0f);
						changed |= ImGui::Checkbox("Free Spin", &joint->revolute.driveFreeSpin);
						changed |= ImGui::Checkbox("Drive Limits Are Forces", &joint->revolute.driveLimitsAreForces);
						ImGui::TreePop();
					}
					break;
				}
				case Phy_JointType::Prismatic:
				{
					if (ImGui::TreeNode("Prismatic Limit"))
					{
						changed |= ImGui::Checkbox("Enable Limit", &joint->prismatic.enableLimit);
						changed |= ImGui::DragFloat("Lower Limit", &joint->prismatic.lowerLimit, 0.01f);
						changed |= ImGui::DragFloat("Upper Limit", &joint->prismatic.upperLimit, 0.01f);
						changed |= ImGui::DragFloat("Stiffness", &joint->prismatic.limitStiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping", &joint->prismatic.limitDamping, 0.01f);
						changed |= ImGui::DragFloat("Restitution", &joint->prismatic.limitRestitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold", &joint->prismatic.limitBounceThreshold, 0.01f);
						ImGui::TreePop();
					}
					break;
				}
				case Phy_JointType::Distance:
				{
					if (ImGui::TreeNode("Distance"))
					{
						changed |= ImGui::DragFloat("Min Distance", &joint->distance.minDistance, 0.01f);
						changed |= ImGui::DragFloat("Max Distance", &joint->distance.maxDistance, 0.01f);
						changed |= ImGui::DragFloat("Tolerance", &joint->distance.tolerance, 0.01f);
						changed |= ImGui::Checkbox("Enable Min", &joint->distance.enableMinDistance);
						changed |= ImGui::Checkbox("Enable Max", &joint->distance.enableMaxDistance);
						changed |= ImGui::Checkbox("Enable Spring", &joint->distance.enableSpring);
						changed |= ImGui::DragFloat("Stiffness", &joint->distance.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping", &joint->distance.damping, 0.01f);
						ImGui::TreePop();
					}
					break;
				}
				case Phy_JointType::Spherical:
				{
					if (ImGui::TreeNode("Spherical Limit"))
					{
						changed |= ImGui::Checkbox("Enable Limit", &joint->spherical.enableLimit);
						changed |= ImGui::DragFloat("Y Limit Angle", &joint->spherical.yLimitAngle, 0.01f);
						changed |= ImGui::DragFloat("Z Limit Angle", &joint->spherical.zLimitAngle, 0.01f);
						changed |= ImGui::DragFloat("Stiffness", &joint->spherical.limitStiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping", &joint->spherical.limitDamping, 0.01f);
						changed |= ImGui::DragFloat("Restitution", &joint->spherical.limitRestitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold", &joint->spherical.limitBounceThreshold, 0.01f);
						ImGui::TreePop();
					}
					break;
				}
				case Phy_JointType::D6:
				{
					const char* motionLabels[] = { "Locked", "Limited", "Free" };
					auto drawMotion = [&](const char* label, Phy_D6Motion& m)
					{
						int idx = static_cast<int>(m);
						if (ImGui::Combo(label, &idx, motionLabels, IM_ARRAYSIZE(motionLabels)))
						{
							m = static_cast<Phy_D6Motion>(idx);
							changed = true;
						}
					};

					if (ImGui::TreeNode("Motions"))
					{
						drawMotion("Motion X", joint->d6.motionX);
						drawMotion("Motion Y", joint->d6.motionY);
						drawMotion("Motion Z", joint->d6.motionZ);
						drawMotion("Motion Twist", joint->d6.motionTwist);
						drawMotion("Motion Swing1", joint->d6.motionSwing1);
						drawMotion("Motion Swing2", joint->d6.motionSwing2);
						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Linear Limits"))
					{
						ImGui::Text("X");
						changed |= ImGui::DragFloat("Lower X", &joint->d6.linearLimitX.lower, 0.01f);
						changed |= ImGui::DragFloat("Upper X", &joint->d6.linearLimitX.upper, 0.01f);
						changed |= ImGui::DragFloat("Stiffness X", &joint->d6.linearLimitX.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping X", &joint->d6.linearLimitX.damping, 0.01f);
						changed |= ImGui::DragFloat("Restitution X", &joint->d6.linearLimitX.restitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold X", &joint->d6.linearLimitX.bounceThreshold, 0.01f);
						ImGui::Separator();

						ImGui::Text("Y");
						changed |= ImGui::DragFloat("Lower Y", &joint->d6.linearLimitY.lower, 0.01f);
						changed |= ImGui::DragFloat("Upper Y", &joint->d6.linearLimitY.upper, 0.01f);
						changed |= ImGui::DragFloat("Stiffness Y", &joint->d6.linearLimitY.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping Y", &joint->d6.linearLimitY.damping, 0.01f);
						changed |= ImGui::DragFloat("Restitution Y", &joint->d6.linearLimitY.restitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold Y", &joint->d6.linearLimitY.bounceThreshold, 0.01f);
						ImGui::Separator();

						ImGui::Text("Z");
						changed |= ImGui::DragFloat("Lower Z", &joint->d6.linearLimitZ.lower, 0.01f);
						changed |= ImGui::DragFloat("Upper Z", &joint->d6.linearLimitZ.upper, 0.01f);
						changed |= ImGui::DragFloat("Stiffness Z", &joint->d6.linearLimitZ.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping Z", &joint->d6.linearLimitZ.damping, 0.01f);
						changed |= ImGui::DragFloat("Restitution Z", &joint->d6.linearLimitZ.restitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold Z", &joint->d6.linearLimitZ.bounceThreshold, 0.01f);
						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Angular Limits"))
					{
						ImGui::Text("Twist");
						changed |= ImGui::DragFloat("Lower Twist", &joint->d6.twistLimit.lower, 0.01f);
						changed |= ImGui::DragFloat("Upper Twist", &joint->d6.twistLimit.upper, 0.01f);
						changed |= ImGui::DragFloat("Stiffness Twist", &joint->d6.twistLimit.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping Twist", &joint->d6.twistLimit.damping, 0.01f);
						changed |= ImGui::DragFloat("Restitution Twist", &joint->d6.twistLimit.restitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold Twist", &joint->d6.twistLimit.bounceThreshold, 0.01f);
						ImGui::Separator();

						ImGui::Text("Swing");
						changed |= ImGui::DragFloat("Swing Y", &joint->d6.swingLimit.yAngle, 0.01f);
						changed |= ImGui::DragFloat("Swing Z", &joint->d6.swingLimit.zAngle, 0.01f);
						changed |= ImGui::DragFloat("Stiffness Swing", &joint->d6.swingLimit.stiffness, 0.01f);
						changed |= ImGui::DragFloat("Damping Swing", &joint->d6.swingLimit.damping, 0.01f);
						changed |= ImGui::DragFloat("Restitution Swing", &joint->d6.swingLimit.restitution, 0.01f);
						changed |= ImGui::DragFloat("Bounce Threshold Swing", &joint->d6.swingLimit.bounceThreshold, 0.01f);
						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Drives"))
					{
						changed |= ImGui::Checkbox("Drive Limits Are Forces", &joint->d6.driveLimitsAreForces);

						auto drawDrive = [&](const char* label, Phy_D6JointDriveSettings& d)
						{
							if (ImGui::TreeNode(label))
							{
								changed |= ImGui::DragFloat("Stiffness", &d.stiffness, 0.01f);
								changed |= ImGui::DragFloat("Damping", &d.damping, 0.01f);
								changed |= ImGui::DragFloat("Force Limit", &d.forceLimit, 1.0f, 0.0f);
								changed |= ImGui::Checkbox("Acceleration", &d.isAcceleration);
								ImGui::TreePop();
							}
						};

						drawDrive("Drive X", joint->d6.driveX);
						drawDrive("Drive Y", joint->d6.driveY);
						drawDrive("Drive Z", joint->d6.driveZ);
						drawDrive("Drive Swing", joint->d6.driveSwing);
						drawDrive("Drive Twist", joint->d6.driveTwist);
						drawDrive("Drive Slerp", joint->d6.driveSlerp);
						ImGui::TreePop();
					}

					if (ImGui::TreeNode("Drive Target"))
					{
						changed |= drawFrame("Drive Pose", joint->d6.drivePose);
						changed |= ImGui::DragFloat3("Drive Linear Vel", &joint->d6.driveLinearVelocity.x, 0.01f);
						changed |= ImGui::DragFloat3("Drive Angular Vel", &joint->d6.driveAngularVelocity.x, 0.01f);
						ImGui::TreePop();
					}
					break;
				}
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	// =========================================================================================
	// Camera 컴포넌트 인스펙터 함수들
	// =========================================================================================

	void EditorCore::DrawInspectorCameraSpringArm(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraSpringArmComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Spring Arm", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ReflectionUI::RenderProperty(*comp, "enabled", "Enabled", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "enableCollision", "Enable Collision", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "enableZoom", "Enable Zoom", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "distance", "Distance", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "minDistance", "Min Distance", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "maxDistance", "Max Distance", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "zoomSpeed", "Zoom Speed", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "distanceDamping", "Distance Damping", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "probeRadius", "Probe Radius", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "probePadding", "Probe Padding", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "minHeight", "Min Height", &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCameraLookAt(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraLookAtComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Look At", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ReflectionUI::RenderProperty(*comp, "enabled", "Enabled", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "targetName", "Target Name", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "rotationDamping", "Rotation Damping", &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCameraFollow(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraFollowComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Follow", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				
				// 런타임 상태 필터 (직렬화/편집 대상 아님)
				auto filter = [](const std::string& propName) -> bool {
					// 런타임 상태 필드들은 인스펙터에서 제외
					return propName != "lockOnTargetId" && 
					       propName != "lockOnActive" && 
					       propName != "initialized" &&
					       propName != "smoothedPosition" &&
					       propName != "smoothedRotation";
				};
				
				changed |= ReflectionUI::RenderInspector(*comp, filter, &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCameraShake(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraShakeComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Shake", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ReflectionUI::RenderProperty(*comp, "enabled", "Enabled", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "amplitude", "Amplitude", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "frequency", "Frequency", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "duration", "Duration", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "decay", "Decay", &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCameraInput(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraInputComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Input", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ReflectionUI::RenderInspector(*comp, nullptr, &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorCameraBlend(World& world, const EntityId& _selectedEntity)
	{
		if (auto* comp = world.GetComponent<CameraBlendComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Camera Blend", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ReflectionUI::RenderProperty(*comp, "active", "Active", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "targetName", "Target Name", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "duration", "Duration", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "useSmoothStep", "Use Smooth Step", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "slowTriggerT", "Slow Trigger T", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "slowDuration", "Slow Duration", &world).changed;
				changed |= ReflectionUI::RenderProperty(*comp, "slowTimeScale", "Slow Time Scale", &world).changed;
				
				if (changed) g_SceneDirty = true;
			}
		}
	}

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


	void EditorCore::DrawInspectorAttackDriver(World& world, const EntityId& _selectedEntity)
	{
		if (auto* driver = world.GetComponent<AttackDriverComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Attack Driver", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##AttackDriverRemove"))
				{
					world.RemoveComponent<AttackDriverComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t traceGuid = driver->traceGuid;
				if (ImGui::InputScalar("Trace GUID", ImGuiDataType_U64, &traceGuid))
				{
					driver->traceGuid = traceGuid;
					driver->traceCached = InvalidEntityId;
					changed = true;
				}

				// Trace entity picker (WeaponTrace 보유 엔티티 위주)
				{
					EntityId resolved = (driver->traceGuid != 0) ? world.FindEntityByGuid(driver->traceGuid) : InvalidEntityId;
					std::string preview = "(self)";
					if (driver->traceGuid != 0)
					{
						if (resolved != InvalidEntityId)
						{
							std::string name = world.GetEntityName(resolved);
							if (name.empty()) name = "Entity " + std::to_string(resolved);
							preview = name + " (" + std::to_string(driver->traceGuid) + ")";
						}
						else
						{
							preview = std::to_string(driver->traceGuid);
						}
					}

					if (ImGui::BeginCombo("Trace (pick entity)", preview.c_str()))
					{
						const bool selSelf = (driver->traceGuid == 0);
						if (ImGui::Selectable("(self)", selSelf))
						{
							driver->traceGuid = 0;
							driver->traceCached = InvalidEntityId;
							changed = true;
						}
						if (selSelf)
							ImGui::SetItemDefaultFocus();

						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							if (!world.GetComponent<WeaponTraceComponent>(eid))
								continue;

							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == driver->traceGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								driver->traceGuid = idc.guid;
								driver->traceCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				// Clip picker (SkinnedMesh animation list)
				{
					std::vector<std::string> clipNames;
					const auto* animComp = world.GetComponent<AdvancedAnimationComponent>(_selectedEntity);
					if (m_skinnedRegistry)
					{
						if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity))
						{
							if (!skinned->meshAssetPath.empty())
							{
								std::shared_ptr<SkinnedMeshGPU> mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
								if (mesh && mesh->sourceModel)
									clipNames = mesh->sourceModel->GetAnimationNames();
							}
						}
					}

					ImGui::Separator();
					ImGui::Text("Clip Timings");
					if (ImGui::Button("+ Add Clip"))
					{
						AttackDriverClip newClip{};
						newClip.type = AttackDriverNotifyType::Attack;
						newClip.source = AttackDriverClipSource::Explicit;
						driver->clips.emplace_back(std::move(newClip));
						changed = true;
					}

					auto ResolveClipNameForUI = [&](const AttackDriverClip& clip) -> std::string {
						if (!animComp)
							return clip.clipName;

						switch (clip.source)
						{
						case AttackDriverClipSource::BaseA: return animComp->base.clipA;
						case AttackDriverClipSource::BaseB: return animComp->base.clipB;
						case AttackDriverClipSource::UpperA: return animComp->upper.clipA;
						case AttackDriverClipSource::UpperB: return animComp->upper.clipB;
						case AttackDriverClipSource::Additive: return animComp->additive.clip;
						case AttackDriverClipSource::Explicit:
						default: return clip.clipName;
						}
					};

					for (size_t i = 0; i < driver->clips.size(); ++i)
					{
						AttackDriverClip& clip = driver->clips[i];
						ImGui::PushID(static_cast<int>(i));

						const std::string resolvedName = ResolveClipNameForUI(clip);
						const char* clipPreview = resolvedName.empty() ? "(none)" : resolvedName.c_str();
						bool open = ImGui::TreeNode("Clip", "%s [%.2f - %.2f]", clipPreview, clip.startTimeSec, clip.endTimeSec);

						ImGui::SameLine();
						bool moveUp = ImGui::SmallButton("^");
						ImGui::SameLine();
						bool moveDown = ImGui::SmallButton("v");
						ImGui::SameLine();
						bool duplicate = ImGui::SmallButton("Dup");
						ImGui::SameLine();
						bool remove = ImGui::SmallButton("Remove");

						if (moveUp && i > 0)
						{
							std::swap(driver->clips[i - 1], driver->clips[i]);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (moveDown && (i + 1) < driver->clips.size())
						{
							std::swap(driver->clips[i + 1], driver->clips[i]);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (duplicate)
						{
							driver->clips.insert(driver->clips.begin() + static_cast<ptrdiff_t>(i + 1), clip);
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}
						if (remove)
						{
							driver->clips.erase(driver->clips.begin() + static_cast<ptrdiff_t>(i));
							changed = true;
							ImGui::PopID();
							if (open) ImGui::TreePop();
							continue;
						}

						if (open)
						{
							changed |= ImGui::Checkbox("Enabled", &clip.enabled);

							const char* typeLabels[] = { "Attack", "Dodge", "Guard" };
							int typeIndex = static_cast<int>(clip.type);
							if (ImGui::Combo("Type", &typeIndex, typeLabels, IM_ARRAYSIZE(typeLabels)))
							{
								clip.type = static_cast<AttackDriverNotifyType>(typeIndex);
								changed = true;
							}

							const char* sourceLabels[] = { "Explicit", "Base A", "Base B", "Upper A", "Upper B", "Additive" };
							int sourceIndex = static_cast<int>(clip.source);
							if (ImGui::Combo("Source", &sourceIndex, sourceLabels, IM_ARRAYSIZE(sourceLabels)))
							{
								clip.source = static_cast<AttackDriverClipSource>(sourceIndex);
								changed = true;
							}

							if (clip.source == AttackDriverClipSource::Explicit)
							{
								if (!clipNames.empty())
								{
									if (ImGui::BeginCombo("Clip", clip.clipName.empty() ? "(none)" : clip.clipName.c_str()))
									{
										const bool selNone = clip.clipName.empty();
										if (ImGui::Selectable("(none)", selNone))
										{
											clip.clipName.clear();
											changed = true;
										}
										if (selNone)
											ImGui::SetItemDefaultFocus();

										for (const auto& name : clipNames)
										{
											const bool sel = (clip.clipName == name);
											if (ImGui::Selectable(name.c_str(), sel))
											{
												clip.clipName = name;
												changed = true;
											}
											if (sel)
												ImGui::SetItemDefaultFocus();
										}
										ImGui::EndCombo();
									}
								}
								else
								{
									ImGui::TextDisabled("No animation clips available (SkinnedMesh/FBX not ready).");
								}
							}
							else
							{
								ImGui::Text("Clip: %s", resolvedName.empty() ? "(none)" : resolvedName.c_str());
							}

							changed |= ImGui::DragFloat("Start Time (sec)", &clip.startTimeSec, 0.01f, 0.0f, 60.0f);
							changed |= ImGui::DragFloat("End Time (sec)", &clip.endTimeSec, 0.01f, 0.0f, 60.0f);

							if (clip.endTimeSec < clip.startTimeSec)
							{
								clip.endTimeSec = clip.startTimeSec;
								changed = true;
								ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Warning: End < Start");
							}

							ImGui::TreePop();
						}

						ImGui::PopID();
					}
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorHurtbox(World& world, const EntityId& _selectedEntity)
	{
		if (auto* hb = world.GetComponent<HurtboxComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Hurtbox", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##HurtboxRemove"))
				{
					world.RemoveComponent<HurtboxComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t ownerGuid = hb->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					hb->ownerGuid = ownerGuid;
					hb->ownerCached = InvalidEntityId;

					EntityId resolved = (ownerGuid != 0) ? world.FindEntityByGuid(ownerGuid) : InvalidEntityId;
					if (resolved != InvalidEntityId)
						hb->ownerNameDebug = world.GetEntityName(resolved);
					changed = true;
				}

				if (hb->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> hurtbox will not resolve");

				// Owner picker
				{
					std::string preview = hb->ownerNameDebug.empty()
						? (hb->ownerGuid != 0 ? std::to_string(hb->ownerGuid) : "(none)")
						: hb->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == hb->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								hb->ownerGuid = idc.guid;
								hb->ownerNameDebug = world.GetEntityName(eid);
								hb->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				ImGui::Text("Owner Name: %s", hb->ownerNameDebug.empty() ? "(none)" : hb->ownerNameDebug.c_str());

				uint32_t teamId = hb->teamId;
				if (ImGui::InputScalar("Team Id", ImGuiDataType_U32, &teamId))
				{
					hb->teamId = teamId;
					changed = true;
				}

				uint32_t part = hb->part;
				if (ImGui::InputScalar("Part", ImGuiDataType_U32, &part))
				{
					hb->part = part;
					changed = true;
				}

				changed |= ImGui::DragFloat("Damage Scale", &hb->damageScale, 0.01f, 0.0f, 100.0f);

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorWeaponTrace(World& world, const EntityId& _selectedEntity)
	{
		if (auto* trace = world.GetComponent<WeaponTraceComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Weapon Trace", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##WeaponTraceRemove"))
				{
					world.RemoveComponent<WeaponTraceComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				std::uint64_t ownerGuid = trace->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					trace->ownerGuid = ownerGuid;
					trace->ownerCached = InvalidEntityId;
					EntityId resolved = (ownerGuid != 0) ? world.FindEntityByGuid(ownerGuid) : InvalidEntityId;
					if (resolved != InvalidEntityId)
						trace->ownerNameDebug = world.GetEntityName(resolved);
					changed = true;
				}

				if (trace->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> trace will not run");

				if (ImGui::Button("Pick from selected entity"))
				{
					if (const auto* idc = world.GetComponent<IDComponent>(_selectedEntity))
					{
						trace->ownerGuid = idc->guid;
						trace->ownerCached = _selectedEntity;
						trace->ownerNameDebug = world.GetEntityName(_selectedEntity);
						changed = true;
					}
				}

				// Owner picker
				{
					std::string preview = trace->ownerNameDebug.empty()
						? (trace->ownerGuid != 0 ? std::to_string(trace->ownerGuid) : "(none)")
						: trace->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == trace->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								trace->ownerGuid = idc.guid;
								trace->ownerNameDebug = world.GetEntityName(eid);
								trace->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				ImGui::Text("Owner Name: %s", trace->ownerNameDebug.empty() ? "(none)" : trace->ownerNameDebug.c_str());

				std::uint64_t basisGuid = trace->traceBasisGuid;
				if (ImGui::InputScalar("Trace Basis GUID", ImGuiDataType_U64, &basisGuid))
				{
					trace->traceBasisGuid = basisGuid;
					trace->traceBasisCached = InvalidEntityId;
					changed = true;
				}

				if (trace->traceBasisGuid == 0)
					ImGui::TextDisabled("Trace Basis GUID is 0 -> uses self");

				// Trace basis picker
				{
					std::string preview;
					if (trace->traceBasisGuid == 0)
					{
						preview = "(self)";
					}
					else
					{
						EntityId resolved = world.FindEntityByGuid(trace->traceBasisGuid);
						preview = (resolved != InvalidEntityId)
							? world.GetEntityName(resolved)
							: std::to_string(trace->traceBasisGuid);
						if (preview.empty())
							preview = std::to_string(trace->traceBasisGuid);
					}

					if (ImGui::BeginCombo("Trace Basis (pick entity)", preview.c_str()))
					{
						const bool selfSel = (trace->traceBasisGuid == 0);
						if (ImGui::Selectable("(self)", selfSel))
						{
							trace->traceBasisGuid = 0;
							trace->traceBasisCached = InvalidEntityId;
							changed = true;
						}
						if (selfSel)
							ImGui::SetItemDefaultFocus();

						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == trace->traceBasisGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								trace->traceBasisGuid = idc.guid;
								trace->traceBasisCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				changed |= ImGui::Checkbox("Active", &trace->active);
				changed |= ImGui::Checkbox("Debug Draw", &trace->debugDraw);
				changed |= ImGui::DragFloat("Base Damage", &trace->baseDamage, 0.1f, 0.0f, 100000.0f);

				uint32_t teamId = trace->teamId;
				if (ImGui::InputScalar("Team Id", ImGuiDataType_U32, &teamId))
				{
					trace->teamId = teamId;
					changed = true;
				}

				uint32_t attackId = trace->attackInstanceId;
				if (ImGui::InputScalar("Attack Instance Id", ImGuiDataType_U32, &attackId))
				{
					trace->attackInstanceId = attackId;
					changed = true;
				}

				uint32_t targetBits = trace->targetLayerBits;
				if (ImGui::InputScalar("Target Layer Bits", ImGuiDataType_U32, &targetBits))
				{
					trace->targetLayerBits = targetBits;
					changed = true;
				}

				uint32_t queryBits = trace->queryLayerBits;
				if (ImGui::InputScalar("Query Layer Bits", ImGuiDataType_U32, &queryBits))
				{
					trace->queryLayerBits = queryBits;
					changed = true;
				}

				uint32_t subSteps = trace->subSteps;
				if (ImGui::InputScalar("Sub Steps", ImGuiDataType_U32, &subSteps))
				{
					trace->subSteps = std::max(1u, subSteps);
					changed = true;
				}

				ImGui::Separator();
				ImGui::Text("Trace Shapes");
				if (ImGui::Button("Add Shape"))
				{
					trace->shapes.emplace_back();
					changed = true;
				}

				for (size_t i = 0; i < trace->shapes.size(); ++i)
				{
					WeaponTraceShape& shape = trace->shapes[i];
					ImGui::PushID(static_cast<int>(i));

					const char* typeName = (shape.type == WeaponTraceShapeType::Sphere)
						? "Sphere"
						: (shape.type == WeaponTraceShapeType::Capsule ? "Capsule" : "Box");
					const char* namePreview = shape.name.empty() ? "(unnamed)" : shape.name.c_str();
					bool open = ImGui::TreeNode("Shape", "%s [%s]", namePreview, typeName);

					ImGui::SameLine();
					bool moveUp = ImGui::SmallButton("^");
					ImGui::SameLine();
					bool moveDown = ImGui::SmallButton("v");
					ImGui::SameLine();
					bool duplicate = ImGui::SmallButton("Dup");
					ImGui::SameLine();
					bool remove = ImGui::SmallButton("Remove");

					if (moveUp && i > 0)
					{
						std::swap(trace->shapes[i - 1], trace->shapes[i]);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (moveDown && (i + 1) < trace->shapes.size())
					{
						std::swap(trace->shapes[i + 1], trace->shapes[i]);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (duplicate)
					{
						trace->shapes.insert(trace->shapes.begin() + static_cast<ptrdiff_t>(i + 1), shape);
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}
					if (remove)
					{
						trace->shapes.erase(trace->shapes.begin() + static_cast<ptrdiff_t>(i));
						changed = true;
						ImGui::PopID();
						if (open) ImGui::TreePop();
						continue;
					}

					if (open)
					{
						char nameBuf[256];
						std::snprintf(nameBuf, sizeof(nameBuf), "%.255s", shape.name.c_str());
						if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
						{
							shape.name = nameBuf;
							changed = true;
						}

						changed |= ImGui::Checkbox("Enabled", &shape.enabled);

						const char* typeItems[] = { "Sphere", "Capsule", "Box" };
						int typeIdx = static_cast<int>(shape.type);
						if (ImGui::Combo("Type", &typeIdx, typeItems, IM_ARRAYSIZE(typeItems)))
						{
							shape.type = static_cast<WeaponTraceShapeType>(typeIdx);
							changed = true;
						}

						changed |= ImGui::DragFloat3("Local Pos", &shape.localPos.x, 0.01f);
						changed |= ImGui::DragFloat3("Local Rot (deg)", &shape.localRotDeg.x, 0.5f);

						if (shape.type == WeaponTraceShapeType::Sphere)
						{
							changed |= ImGui::DragFloat("Radius", &shape.radius, 0.01f, 0.0f, 100.0f);
						}
						else if (shape.type == WeaponTraceShapeType::Capsule)
						{
							changed |= ImGui::DragFloat("Radius", &shape.radius, 0.01f, 0.0f, 100.0f);
							changed |= ImGui::DragFloat("Half Height", &shape.capsuleHalfHeight, 0.01f, 0.0f, 100.0f);
						}
						else if (shape.type == WeaponTraceShapeType::Box)
						{
							changed |= ImGui::DragFloat3("Half Extents", &shape.boxHalfExtents.x, 0.01f, 0.0f, 100.0f);
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorSocketAttachment(World& world, const EntityId& _selectedEntity)
	{
		if (auto* att = world.GetComponent<SocketAttachmentComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Socket Attachment", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##SocketAttachmentRemove"))
				{
					world.RemoveComponent<SocketAttachmentComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				// Owner GUID
				std::uint64_t ownerGuid = att->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					att->ownerGuid = ownerGuid;
					att->ownerCached = InvalidEntityId;
					changed = true;
				}

				if (att->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> attachment will not run");

				// Set owner from list (dropdown of all entities with IDComponent)
				{
					std::string preview = att->ownerNameDebug.empty()
						? (att->ownerGuid != 0 ? std::to_string(att->ownerGuid) : "(none)")
						: att->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == att->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								att->ownerGuid = idc.guid;
								att->ownerNameDebug = world.GetEntityName(eid);
								att->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				// Resolve owner and build socket option list (name + parentBone for fallback match)
				EntityId resolvedOwner = (att->ownerGuid != 0) ? world.FindEntityByGuid(att->ownerGuid) : InvalidEntityId;
				std::vector<std::string> socketOptions;
				if (resolvedOwner != InvalidEntityId)
				{
					auto addOption = [&socketOptions](const std::string& value) {
						if (value.empty()) return;
						if (std::find(socketOptions.begin(), socketOptions.end(), value) == socketOptions.end())
							socketOptions.push_back(value);
					};
					if (const auto* sc = world.GetComponent<SocketComponent>(resolvedOwner))
					{
						for (const auto& s : sc->sockets)
						{
							addOption(s.name);
							if (!s.parentBone.empty() && s.parentBone != s.name)
								addOption(s.parentBone);
						}
					}
					// Auto-select: if socket name is empty and owner has sockets, set to first option
					if (att->socketName.empty() && !socketOptions.empty())
					{
						att->socketName = socketOptions[0];
						changed = true;
					}
				}

				// Socket: 목록에서 선택만 (문자열 직접 입력 제거)
				if (!socketOptions.empty())
				{
					const char* preview = att->socketName.c_str();
					if (preview[0] == '\0') preview = "(선택)";
					if (ImGui::BeginCombo("Socket", preview))
					{
						for (const auto& opt : socketOptions)
						{
							const bool sel = (att->socketName == opt);
							if (ImGui::Selectable(opt.c_str(), sel))
							{
								att->socketName = opt;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("오너 소켓 목록에서 선택 (이름 또는 parentBone)");
				}
				else if (resolvedOwner != InvalidEntityId)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner has no sockets (SocketComponent.sockets 추가)");
					// 목록 없을 때만 현재값 표시 (읽기 전용)
					ImGui::Text("Socket Name: %s", att->socketName.empty() ? "(none)" : att->socketName.c_str());
				}
				else
				{
					// Owner 미지정 시: 안내 + 현재값 표시
					ImGui::TextDisabled("Owner를 먼저 선택하면 Socket 목록에서 고를 수 있습니다.");
					ImGui::Text("Socket Name: %s", att->socketName.empty() ? "(none)" : att->socketName.c_str());
				}

				changed |= ImGui::Checkbox("Follow Scale", &att->followScale);
				changed |= ImGui::DragFloat3("Extra Pos", &att->extraPos.x, 0.01f);
				changed |= ImGui::DragFloat3("Extra Rot (rad)", &att->extraRotRad.x, 0.01f);
				changed |= ImGui::DragFloat3("Extra Scale", &att->extraScale.x, 0.01f);

				ImGui::Separator();
				ImGui::Text("Debug: Resolved Owner EntityId = %llu", static_cast<unsigned long long>(resolvedOwner == InvalidEntityId ? 0 : resolvedOwner));

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorSocketComponent(World& world, const EntityId& _selectedEntity)
	{
		auto* comp = world.GetComponent<SocketComponent>(_selectedEntity);
		if (!comp)
			return;

		if (!ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		bool changed = false;

		// 본 이름 목록 (동일 엔티티의 SkinnedMesh에서)
		std::vector<std::string> boneNames;
		if (m_skinnedRegistry)
		{
			if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity))
			{
				if (!skinned->meshAssetPath.empty())
				{
					std::shared_ptr<SkinnedMeshGPU> mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
					if (mesh && mesh->sourceModel)
						boneNames = mesh->sourceModel->GetBoneNames();
				}
			}
		}

		// 소켓 전용 UI: 본 드롭다운, 이름/위치/회전/스케일, + 추가 / 항목별 삭제
		const size_t size = comp->sockets.size();
		for (size_t i = 0; i < size; ++i)
		{
			SocketDef& s = comp->sockets[i];
			ImGui::PushID(static_cast<int>(i));

			bool open = ImGui::TreeNode("Socket", "%s [%s]", s.name.empty() ? "(unnamed)" : s.name.c_str(), s.parentBone.empty() ? "?" : s.parentBone.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
			{
				comp->sockets.erase(comp->sockets.begin() + static_cast<ptrdiff_t>(i));
				changed = true;
				ImGui::PopID();
				if (open) ImGui::TreePop();
				break;
			}
			if (open)
			{
				// Name
				char nameBuf[256];
				std::snprintf(nameBuf, sizeof(nameBuf), "%.255s", s.name.c_str());
				if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
				{
					s.name = nameBuf;
					changed = true;
				}

				// Parent Bone: 드롭다운 (본 목록)
				int currentBoneIndex = -1;
				if (!boneNames.empty())
				{
					for (size_t k = 0; k < boneNames.size(); ++k)
						if (boneNames[k] == s.parentBone) { currentBoneIndex = static_cast<int>(k); break; }

					const char* preview = (currentBoneIndex >= 0 && currentBoneIndex < static_cast<int>(boneNames.size()))
						? boneNames[static_cast<size_t>(currentBoneIndex)].c_str()
						: (s.parentBone.empty() ? "(선택)" : s.parentBone.c_str());
					if (ImGui::BeginCombo("Parent Bone", preview))
					{
						for (size_t k = 0; k < boneNames.size(); ++k)
						{
							const bool sel = (currentBoneIndex == static_cast<int>(k));
							if (ImGui::Selectable(boneNames[k].c_str(), sel))
							{
								s.parentBone = boneNames[k];
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					char boneBuf[256];
					std::snprintf(boneBuf, sizeof(boneBuf), "%.255s", s.parentBone.c_str());
					if (ImGui::InputText("Parent Bone", boneBuf, sizeof(boneBuf)))
					{
						s.parentBone = boneBuf;
						changed = true;
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("SkinnedMesh가 없으면 본 이름을 직접 입력");
				}

				changed |= ImGui::DragFloat3("Position", &s.position.x, 0.01f);
				changed |= ImGui::DragFloat3("Rotation (deg)", &s.rotation.x, 1.0f);
				changed |= ImGui::DragFloat3("Scale", &s.scale.x, 0.01f);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (ImGui::Button("+ Add Socket"))
		{
			comp->sockets.push_back(SocketDef{});
			changed = true;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("이 오브젝트에 소켓을 추가합니다.");

		if (changed)
			g_SceneDirty = true;
	}
}
