#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "ThirdParty/json/json.hpp"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <commdlg.h>

namespace Alice
{
	namespace
	{
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

	void EditorCore::DrawMainMenuBar(World& world,
		float deltaTime,
		float fps,
		bool& isPlaying,
		EntityId& selectedEntity,
		bool& useForwardRendering,
		bool& isDebugDraw)
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		ImGui::Text("AliceRenderer");
		ImGui::Separator();

		// Play / Stop 토글 버튼
		if (!isPlaying)
		{
			if (ImGui::Button("Play"))
			{
				// 플레이 전에 스크립트 리로드 실행
				ALICE_LOG_INFO("Play button pressed: Starting script reload...");
				bool reloadSuccess = ReloadScripts_FromButton(world);
				if (!reloadSuccess)
				{
					// 스크립트 리로드 실패 시 경고 표시 및 게임 실행 중단
					ALICE_LOG_ERRORF("Play button: Script reload failed. Game will NOT start.");
					ImGui::OpenPopup("ScriptReloadFailed");
					// isPlaying은 설정하지 않음 (게임 실행 안 함)
				}
				else
				{
					ALICE_LOG_INFO("Play button: Script reload succeeded. Starting game...");
					isPlaying = true;
				}
			}
		}
		else
		{
			if (ImGui::Button("Stop"))
			{
				isPlaying = false;
			}
		}

		// 스크립트 리로드 실패 경고 팝업
		if (ImGui::BeginPopupModal("ScriptReloadFailed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Script Reload Failed!");
			ImGui::Separator();
			ImGui::Text("Failed to reload scripts before starting the game.");
			ImGui::Text("Please check the console for error details.");
			ImGui::Text("The game will not start until scripts are reloaded successfully.");
			ImGui::Separator();
			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 오브젝트 생성 메뉴 버튼
		if (ImGui::Button("Create"))
		{
			ImGui::OpenPopup("CreateObjectPopup");
		}
		if (ImGui::BeginPopup("CreateObjectPopup"))
		{
			if (ImGui::MenuItem("Empty"))
			{
				EntityId e = world.CreateEmpty();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Empty"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			// FBX Primitives 메뉴
			if (ImGui::BeginMenu("FBX Primitives"))
			{
				// 프리미티브 폴더 스캔해서 자동으로 메뉴 채우기
				static std::vector<std::filesystem::path> cached;
				static bool cachedOnce = false;

				if (!cachedOnce)
				{
					cachedOnce = true;

					auto dirAbs = ResourceManager::Get().Resolve("Assets/Fbx");
					if (std::filesystem::exists(dirAbs))
					{
						for (auto& it : std::filesystem::directory_iterator(dirAbs))
						{
							if (!it.is_regular_file()) continue;
							auto p = it.path();
							auto ext = p.extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(),
								[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							if (ext == ".fbxasset")
								cached.push_back(p);
						}
						std::sort(cached.begin(), cached.end());
					}
				}

				// 고정 프리미티브 목록 (폴더에 없어도 표시)
				struct Prim { const char* label; const char* path; };
				static Prim prims[] = {
					{"IcoSphere", "../Assets/Fbx/IcoSphere.fbxasset"},
					{"Torus",     "../Assets/Fbx/Torus.fbxasset"},
					{"Monkey",    "../Assets/Fbx/Monkey.fbxasset"},
					//{"Box",       "../Assets/Fbx/Box.fbxasset"},
					{"Cube(FBX)", "../Assets/Fbx/Cube.fbxasset"},
					{"Sphere(FBX)", "../Assets/Fbx/Sphere.fbxasset"},
					{"Quad(FBX)", "../Assets/Fbx/Quad.fbxasset"},
					{"Corn(FBX)", "../Assets/Fbx/Corn.fbxasset"}
				};

				// 고정 목록 표시
				for (auto& p : prims)
				{
					if (ImGui::MenuItem(p.label))
					{
						EntityId e = InstantiateFbxAssetToWorld(world, p.path, p.label);
						if (e != InvalidEntityId)
						{
							PushCommand(std::make_unique<CreateEntityCommand>(e, p.label));
							selectedEntity = e;
						}
						ImGui::CloseCurrentPopup();
					}
				}

				// 폴더에서 스캔한 추가 FBX들 표시
				if (!cached.empty())
				{
					ImGui::Separator();
					for (auto& abs : cached)
					{
						std::string label = abs.stem().string();

						// 이미 고정 목록에 있는 건 스킵
						bool skip = false;
						for (auto& p : prims)
						{
							if (label == p.label || label == "Cube" && std::string(p.label) == "Cube(FBX)")
							{
								skip = true;
								break;
							}
						}
						if (skip) continue;

						if (ImGui::MenuItem(label.c_str()))
						{
							EntityId e = InstantiateFbxAssetToWorld(world, abs, label);
							if (e != InvalidEntityId)
							{
								PushCommand(std::make_unique<CreateEntityCommand>(e, label));
								selectedEntity = e;
							}
							ImGui::CloseCurrentPopup();
						}
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Camera"))
			{
				EntityId e = world.CreateCamera();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Camera"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Point Light"))
			{
				EntityId e = world.CreatePointLight();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Point Light"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Spot Light"))
			{
				EntityId e = world.CreateSpotLight();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Spot Light"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::MenuItem("Rect Light"))
			{
				EntityId e = world.CreateRectLight();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Rect Light"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::BeginMenu("AliceUI"))
			{
				if (ImGui::MenuItem("Screen Image"))
				{
					EntityId e = CreateAliceUIImage(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Image"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Text"))
				{
					EntityId e = CreateAliceUIText(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Text"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Button"))
				{
					EntityId e = CreateAliceUIButton(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Button"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Gauge"))
				{
					EntityId e = CreateAliceUIGauge(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Gauge"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("World Image"))
				{
					EntityId e = CreateAliceUIWorldImage(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "World UI Image"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();

		// 스크립트 핫 리로드 버튼 (C++ 스크립트 DLL 재빌드 + 재로드)
		if (ImGui::Button("Reload Scripts"))
		{
			// ImGui Begin/End 짝을 깨지 않기 위해,
			// 실제 빌드/복사/리로드 로직은 별도 헬퍼 함수에서 처리합니다.
			ReloadScripts_FromButton(world);
			m_scriptBuilded = true;
		}

		ImGui::Separator();
		// FBX 임포트 버튼
		if (ImGui::Button("Load FBX"))
		{
			wchar_t fileBuffer[MAX_PATH] = {};
			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = m_hwnd;
			ofn.lpstrFilter = L"FBX Files\0*.fbx\0All Files\0*.*\0";
			ofn.lpstrFile = fileBuffer;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

			if (GetOpenFileNameW(&ofn))
			{
				if (m_renderDevice)
				{
					std::filesystem::path fbxPath = fileBuffer;

					wchar_t exePathW[MAX_PATH] = {};
					GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
					std::filesystem::path exePath = exePathW;
					std::filesystem::path exeDir = exePath.parent_path();
					std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트

					fbxPath = std::filesystem::relative(fbxPath, projectRoot);

					// 간단한 FBX 임포트 옵션
					FbxImportOptions opt{};
					FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);

					auto* d3dDevice = m_renderDevice->GetDevice();
					FbxImportResult result = importer.Import(d3dDevice, fbxPath, opt);

					// 1) 인스턴스 에셋(.fbxasset)이 생성되었으면, 프로젝트 뷰에서 활용할 수 있습니다.
					// 2) 월드에 기본 인스턴스 하나를 바로 생성해 줍니다. (언리얼의 "씬에 배치" 느낌)
					if (!result.meshAssetPath.empty())
					{
						EntityId e = world.CreateEntity();
						TransformComponent& t = world.AddComponent<TransformComponent>(e);
						t.position = { 0.0f, 0.0f, 0.0f };
						t.scale = { 1.0f, 1.0f, 1.0f };
						t.rotation = { 0.0f, 0.0f, 0.0f };

						// 스키닝 메시 컴포넌트 등록
						SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, result.meshAssetPath);
						skinned.instanceAssetPath = result.instanceAssetPath;

						// (임시) 본 행렬이 아직 없으므로, 1개짜리 항등 행렬 팔레트를 사용합니다.
						//  - 나중에 FbxModel/FbxAnimation 연동 시 실제 본 팔레트로 교체됩니다.
						static DirectX::XMFLOAT4X4 s_identityBone =
							DirectX::XMFLOAT4X4(1, 0, 0, 0,
								0, 1, 0, 0,
								0, 0, 1, 0,
								0, 0, 0, 1);
						skinned.boneMatrices = &s_identityBone;
						skinned.boneCount = 1;

						// 첫 번째 머티리얼이 있으면 기본 머티리얼로 할당
						// 원래 있는 경우 없는 경우 나눠서 있는 경우는 서브 메테리얼을 만들어야 하는데, 일단은 둘다 생기도록 함.
						// TODO : 여기서 서브 메테리얼을 각각 다르게 설정할 수 있게 해야함 
						if (!result.materialAssetPaths.empty())
						{
							DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
							mat.assetPath = result.materialAssetPaths.front();
							MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
						}
						else
						{
							//DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							//MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
							//mat.assetPath = "fbx has no material. default material";
							//MaterialFile::Load(mat.assetPath, mat);
						}

						selectedEntity = e;
						g_SceneDirty = true;
					}
				}
			}
		}

		ImGui::Separator();

		// 게임 빌드 버튼 (간단한 1차 버전)
		if (ImGui::Button("Build"))
		{
			g_ShowBuildGameWindow = true;
		}

		ImGui::Separator();
		// PVD 설정 버튼
		if (ImGui::Button("PVD Settings"))
		{
			g_ShowPvdSettingsWindow = true;
		}

		ImGui::Separator();
		ImGui::Text("DeltaTime: %.3f  FPS: %.1f", deltaTime, fps);

		ImGui::Separator();
		// 렌더링 시스템 선택 체크박스
		ImGui::Checkbox("Show DebugDraw", &isDebugDraw);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("체크: 디버그 라인 켜기\n해제: 디버그 라인 끄기");
		}

		ImGui::Separator();
		// 렌더링 시스템 선택 체크박스
		ImGui::Checkbox("Forward Rendering", &useForwardRendering);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("체크: Forward Rendering\n해제: Deferred Rendering");
		}

		ImGui::EndMainMenuBar();
	}
}

