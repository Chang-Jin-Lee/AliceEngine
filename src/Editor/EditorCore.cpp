#include "Editor/EditorCore.h"

#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Core/ImGuiEx.h"
#include "Core/ScriptHotReload.h"
#include "Game/FbxImporter.h"
#include "Core/Logger.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <fstream>
#include <atomic>
#include <thread>
#include <Core/Prefab.h>
#include <Core/Script.h>
#include <Core/Material.h>
#include <Core/SceneFile.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ShlObj.h>   // 폴더 선택 다이얼로그 (SHBrowseForFolderW)
#include <Game/FbxAsset.h>

// 텍스처 로딩용 DirectXTK
#include <DirectXTK/WICTextureLoader.h>

using namespace DirectX;

namespace Alice
{
    namespace
    {
        /// 에디터 Reload Scripts 버튼에서 호출하는 헬퍼입니다.
        /// - ScriptsBuild CMake 프로젝트를 configure/build 해서 AliceScripts.dll 을 만들고
        ///   현재 실행 중인 exe 옆으로 복사한 뒤 ScriptHotReload_Reload 를 호출합니다.
        void ReloadScripts_FromButton()
        {
            using namespace std::filesystem;

            // 1) 실행 파일 위치 기준으로 프로젝트 루트 / ScriptsBuild 경로 계산
            wchar_t exePathW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
            path exePath = exePathW;
            path exeDir  = exePath.parent_path();
            path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
            path scriptsRoot = projectRoot / "ScriptsBuild";
            path scriptsBuildDir = scriptsRoot / "build";

            if (!exists(scriptsRoot / "CMakeLists.txt"))
            {
                ALICE_LOG_ERRORF("Reload Scripts: ScriptsBuild/CMakeLists.txt not found. path=\"%s\"",
                                 (scriptsRoot / "CMakeLists.txt").string().c_str());
                return;
            }

#ifdef _DEBUG
            constexpr const wchar_t* kConfig = L"Debug";
#else
            constexpr const wchar_t* kConfig = L"Release";
#endif

            // 2) 매번 ScriptsBuild 를 cmake -S . -B build 로 갱신해서
            //    새로 추가된 C++ 스크립트 파일들도 CMake 타겟에 포함되도록 합니다.
            {
                std::wstring cmdConfig = L"cmake -S . -B build";

                STARTUPINFOW        si{};
                PROCESS_INFORMATION pi{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;

                BOOL okCfg = CreateProcessW(
                    nullptr,
                    cmdConfig.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    0,
                    nullptr,
                    scriptsRoot.wstring().c_str(),
                    &si,
                    &pi);

                if (!okCfg)
                {
                    ALICE_LOG_ERRORF("Reload Scripts: failed to start CMake configure process for ScriptsBuild.");
                    return;
                }

                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exitCodeCfg = 0;
                GetExitCodeProcess(pi.hProcess, &exitCodeCfg);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                ALICE_LOG_INFO("Reload Scripts: CMake configure finished with exitCode=%lu",
                               static_cast<unsigned long>(exitCodeCfg));

                if (exitCodeCfg != 0)
                {
                    ALICE_LOG_ERRORF("Reload Scripts: CMake configure failed for ScriptsBuild (exitCode=%lu).",
                                     static_cast<unsigned long>(exitCodeCfg));
                    return;
                }
            }

            // 3) ScriptsBuild 프로젝트에서 AliceScripts 타겟만 빌드
            std::wstring cmdBuild = L"cmake --build build --config ";
            cmdBuild += kConfig;
            cmdBuild += L" --target AliceScripts";

            {
                STARTUPINFOW        si{};
                PROCESS_INFORMATION pi{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;

                BOOL okBuild = CreateProcessW(
                    nullptr,
                    cmdBuild.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    0,
                    nullptr,
                    scriptsRoot.wstring().c_str(),
                    &si,
                    &pi);

                if (!okBuild)
                {
                    ALICE_LOG_ERRORF("Reload Scripts: failed to start CMake build process for ScriptsBuild.");
                    return;
                }

                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD exitCode = 0;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                ALICE_LOG_INFO("Reload Scripts: CMake build finished with exitCode=%lu",
                               static_cast<unsigned long>(exitCode));

                if (exitCode != 0)
                {
                    ALICE_LOG_ERRORF("Reload Scripts: CMake build failed for ScriptsBuild (exitCode=%lu).",
                                     static_cast<unsigned long>(exitCode));
                    return;
                }
            }

            // 4) ScriptsBuild/build/<Config>/AliceScripts.dll 을 실행 파일 옆으로 복사
            path builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
            if (!exists(builtDll))
            {
                ALICE_LOG_ERRORF("Reload Scripts: built DLL not found: \"%s\"",
                                 builtDll.string().c_str());
                return;
            }

            // 새 DLL 이 정상 빌드된 것이 확인되었으므로, 이제서야 기존 DLL 을 언로드합니다.
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
                return;
            }

            ALICE_LOG_INFO("Reload Scripts: copied \"%s\" -> \"%s\"",
                           builtDll.string().c_str(),
                           targetDll.string().c_str());

            // 6) 새 DLL 로드
            ScriptHotReload_Reload();
        }
    }

    namespace
    {
        // 현재 씬이 수정되었는지 여부 (저장 필요 여부)
        bool                     g_SceneDirty           = false;
        bool                     g_HasCurrentScenePath  = false;
        std::filesystem::path    g_CurrentScenePath;

        // 간단한 게임 빌드 UI 상태
        bool                     g_ShowBuildGameWindow  = false;

        // Build Game 진행 상황 (간단한 멀티스레드 + atomic 사용)
        std::atomic<bool>        g_BuildInProgress { false };
        std::atomic<float>       g_BuildProgress   { 0.0f };   // 0.0 ~ 1.0
        std::atomic<long>        g_BuildExitCode   { -1 };     // -1: 아직 없음

        // 다른 씬을 로드하기 위해 대기 중인 경로
        bool                     g_RequestSceneLoad     = false;
        std::filesystem::path    g_NextScenePath;

        // 단일 머티리얼 에셋 편집기 상태
        bool                     g_MaterialEditorOpen   = false;
        std::filesystem::path    g_MaterialEditorPath;
        MaterialComponent        g_MaterialEditorData;
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
        io.FontDefault = io.Fonts->AddFontFromFileTTF(
            "../Resource/Fonts/NotoSansKR-Regular.ttf",
            18.0f,
            &baseConfig,
            io.Fonts->GetGlyphRangesKorean());

        ImFontConfig jpConfig{};
        jpConfig.MergeMode = true;
        jpConfig.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF(
            "../Resource/Fonts/meiryo.ttc",
            18.0f,
            &jpConfig,
            io.Fonts->GetGlyphRangesJapanese());

        m_hwnd         = hwnd;
        m_renderDevice = &renderDevice;

        auto* d3dDevice  = renderDevice.GetDevice();
        auto* d3dContext = renderDevice.GetImmediateContext();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(d3dDevice, d3dContext);

        m_initialized = true;
        return true;
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
    }

    void EditorCore::RenderDrawData()
    {
        if (!m_initialized)
            return;

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorCore::DrawEditorUI(World& world,
                                  Camera& camera,
                                  ForwardRenderSystem& forward,
                                  SceneManager* sceneManager,
                                  float deltaTime,
                                  float fps,
                                  bool& isPlaying,
                                  int& shadingMode,
                                  bool& useFillLight,
                                  EntityId& selectedEntity,
                                  ViewportPicker& picker,
                                  float& cameraMoveSpeed)
    {
        // 메인 뷰포트 전체를 도킹 스페이스로 사용합니다.
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(viewport->ID, viewport);

        // 첫 프레임에만 기본 도킹 레이아웃을 구성합니다.
        static bool s_dockInitialized = false;
        if (!s_dockInitialized)
        {
            s_dockInitialized = true;

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain     = dockspaceId;
            ImGuiID dockLeft     = 0;
            ImGuiID dockRight    = 0;
            ImGuiID dockCenter   = 0;
            ImGuiID dockRightCol = 0;

            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockRight);
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Right, 0.26f, &dockRightCol, &dockCenter);

            ImGuiID dockCenterTop    = 0;
            ImGuiID dockCenterBottom = 0;
            ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.30f, &dockCenterBottom, &dockCenterTop);

            ImGuiID dockLeftTop    = 0;
            ImGuiID dockLeftBottom = 0;
            ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.55f, &dockLeftBottom, &dockLeftTop);

            ImGuiID dockRightTop    = 0;
            ImGuiID dockRightBottom = 0;
            ImGui::DockBuilderSplitNode(dockRightCol, ImGuiDir_Down, 0.55f, &dockRightBottom, &dockRightTop);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeftTop);
            ImGui::DockBuilderDockWindow("Project",   dockLeftBottom);
            ImGui::DockBuilderDockWindow("Game",      dockCenterTop);
            ImGui::DockBuilderDockWindow("Camera",    dockCenterBottom);
            ImGui::DockBuilderDockWindow("Inspector", dockRightTop);
            ImGui::DockBuilderDockWindow("Lighting",  dockRightBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        // === Toolbar ===
        if (ImGui::BeginMainMenuBar())
        {
            ImGui::Text("AliceRenderer");
            ImGui::Separator();

            // Play / Stop 토글 버튼
            if (!isPlaying)
            {
                if (ImGui::Button("Play"))
                {
                    isPlaying = true;
                }
            }
            else
            {
                if (ImGui::Button("Stop"))
                {
                    isPlaying = false;
                }
            }

            ImGui::Separator();

            // 오브젝트 생성 메뉴 버튼
            if (ImGui::Button("Create"))
            {
                ImGui::OpenPopup("CreateObjectPopup");
            }
            if (ImGui::BeginPopup("CreateObjectPopup"))
            {
                if (ImGui::MenuItem("Cube"))
                {
                    EntityId e = world.CreateEntity();
                    auto& t = world.AddTransform(e);
                    t.SetPosition(0.0f, 0.0f, 0.0f)
                     .SetScale(1.0f, 1.0f, 1.0f);
                    // 기본 회색 머티리얼을 함께 추가합니다.
                    DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                    world.AddMaterial(e, defaultColor);
                    selectedEntity = e;
                    g_SceneDirty   = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            // 스크립트 핫 리로드 버튼 (C++ 스크립트 DLL 재빌드 + 재로드)
            if (ImGui::Button("Reload Scripts"))
            {
                // ImGui Begin/End 짝을 깨지 않기 위해,
                // 실제 빌드/복사/리로드 로직은 별도 헬퍼 함수에서 처리합니다.
                ReloadScripts_FromButton();
            }

            ImGui::Separator();
            // FBX 임포트 버튼
            if (ImGui::Button("Load FBX"))
            {
                wchar_t fileBuffer[MAX_PATH] = {};
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner   = m_hwnd;
                ofn.lpstrFilter = L"FBX Files\0*.fbx\0All Files\0*.*\0";
                ofn.lpstrFile   = fileBuffer;
                ofn.nMaxFile    = MAX_PATH;
                ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                if (GetOpenFileNameW(&ofn))
                {
                    if (m_resources && m_renderDevice)
                    {
                        std::filesystem::path fbxPath = fileBuffer;

                        // 간단한 FBX 임포트 옵션
                        FbxImportOptions opt{};
                        FbxImporter importer(*m_resources, m_skinnedRegistry);

                        auto* d3dDevice = m_renderDevice->GetDevice();
                        FbxImportResult result = importer.Import(d3dDevice, fbxPath, opt);

                        // 1) 인스턴스 에셋(.fbxasset)이 생성되었으면, 프로젝트 뷰에서 활용할 수 있습니다.
                        // 2) 월드에 기본 인스턴스 하나를 바로 생성해 줍니다. (언리얼의 "씬에 배치" 느낌)
                        if (!result.meshAssetPath.empty())
                        {
                            EntityId e = world.CreateEntity();
                            TransformComponent& t = world.AddTransform(e);
                            t.position = { 0.0f, 0.0f, 0.0f };
                            t.scale    = { 1.0f, 1.0f, 1.0f };
                            t.rotation = { 0.0f, 0.0f, 0.0f };

                            // 스키닝 메시 컴포넌트 등록
                            SkinnedMeshComponent& skinned = world.AddSkinnedMesh(e, result.meshAssetPath);
                            skinned.instanceAssetPath     = result.instanceAssetPath;

                            // (임시) 본 행렬이 아직 없으므로, 1개짜리 항등 행렬 팔레트를 사용합니다.
                            //  - 나중에 FbxModel/FbxAnimation 연동 시 실제 본 팔레트로 교체됩니다.
                            static DirectX::XMFLOAT4X4 s_identityBone =
                                DirectX::XMFLOAT4X4(1,0,0,0,
                                                    0,1,0,0,
                                                    0,0,1,0,
                                                    0,0,0,1);
                            skinned.boneMatrices = &s_identityBone;
                            skinned.boneCount    = 1;

                            // 첫 번째 머티리얼이 있으면 기본 머티리얼로 할당
                            if (!result.materialAssetPaths.empty())
                            {
                                DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                                MaterialComponent& mat = world.AddMaterial(e, defaultColor);
                                mat.assetPath = result.materialAssetPaths.front();
                                MaterialFile::Load(mat.assetPath, mat);
                            }

                            selectedEntity = e;
                            g_SceneDirty   = true;
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
            ImGui::Text("DeltaTime: %.3f  FPS: %.1f", deltaTime, fps);

            ImGui::EndMainMenuBar();
        }

        // === Build Game 창 (씬 선택 + 간단한 해상도 옵션) ===
        if (g_ShowBuildGameWindow)
        {
            if (ImGui::Begin("Build Game", &g_ShowBuildGameWindow))
            {
                namespace fs = std::filesystem;

                static int   s_Width  = 1280;
                static int   s_Height = 720;
                static bool  s_ScanScenesOnce = true;
                static std::vector<fs::path> s_ScenePaths;
                static std::vector<bool>     s_SceneSelected;
                static int   s_DefaultScene  = -1;       // 기본으로 실행될 씬 인덱스
                static char  s_ExportPath[260] = "../Build/Export"; // 배포용 출력 경로

                ImGui::Text("Output Resolution");
                ImGui::InputInt("Width",  &s_Width);
                ImGui::InputInt("Height", &s_Height);
                if (s_Width < 320)  s_Width  = 320;
                if (s_Height < 240) s_Height = 240;

                ImGui::Separator();
                ImGui::Text("Scenes to Build");

                if (s_ScanScenesOnce)
                {
                    s_ScanScenesOnce = false;
                    s_ScenePaths.clear();
                    s_SceneSelected.clear();
                    s_DefaultScene = -1;

                    const fs::path assetsRoot = "../Assets";
                    if (fs::exists(assetsRoot))
                    {
                        for (const auto& entry : fs::recursive_directory_iterator(assetsRoot))
                        {
                            if (!entry.is_regular_file())
                                continue;
                            if (entry.path().extension() != ".scene")
                                continue;

                            s_ScenePaths.push_back(entry.path());
                            s_SceneSelected.push_back(true);
                        }
                    }
                }

                if (s_ScenePaths.empty())
                {
                    ImGui::TextDisabled("No .scene files found under Assets.");
                }
                else
                {
                    for (std::size_t i = 0; i < s_ScenePaths.size(); ++i)
                    {
                        bool selected = s_SceneSelected[i];
                        ImGui::Checkbox(s_ScenePaths[i].filename().string().c_str(), &selected);
                        s_SceneSelected[i] = selected;

                        ImGui::SameLine();
                        bool isDefault = (static_cast<int>(i) == s_DefaultScene);
                        std::string label = "Default##" + std::to_string(i);
                        if (ImGui::RadioButton(label.c_str(), isDefault))
                        {
                            s_DefaultScene = static_cast<int>(i);
                        }
                    }
                }

                ImGui::Separator();

                // 배포용 출력 경로 입력 + 폴더 선택 버튼
                ImGui::Text("Export Path (relative to project root or absolute)");
                ImGui::InputText("##ExportPath", s_ExportPath, IM_ARRAYSIZE(s_ExportPath));
                ImGui::SameLine();
                if (ImGui::Button("Browse..."))
                {
                    BROWSEINFOW bi{};
                    bi.hwndOwner = m_hwnd;
                    bi.lpszTitle = L"Select export folder";
                    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

                    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                    if (pidl)
                    {
                        wchar_t folderW[MAX_PATH] = {};
                        if (SHGetPathFromIDListW(pidl, folderW))
                        {
                            std::filesystem::path p = folderW;
                            std::string utf8 = p.string();
                            // 선택한 경로를 그대로 ExportPath 로 사용 (필요하면 나중에 상대 경로로 변환 가능)
                            strncpy_s(s_ExportPath, utf8.c_str(), _TRUNCATE);
                        }
                        CoTaskMemFree(pidl);
                    }
                }

                // 빌드 진행 상황 표시
                if (g_BuildInProgress.load())
                {
                    ImGui::Text("Building AliceGame (Release)...");
                    float p = g_BuildProgress.load();
                    ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f));
                }
                else
                {
                    long exitCode = g_BuildExitCode.load();
                    if (exitCode == 0)
                    {
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Last build: Success");
                    }
                    else if (exitCode > 0)
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Last build: Failed (code=%ld)", exitCode);
                    }
                }

                if (!g_BuildInProgress.load())
                {
                    if (ImGui::Button("Build Game"))
                    {
                        // 1) 빌드 설정 파일 저장 (간단한 텍스트 포맷)
                        wchar_t exePathW[MAX_PATH] = {};
                        GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
                        fs::path exePath = exePathW;
                        fs::path exeDir  = exePath.parent_path();
                        fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트

                        fs::path buildDir = projectRoot / "Build";
                        std::error_code fec;
                        fs::create_directories(buildDir, fec);

                        fs::path cfgPath = buildDir / "BuildSettings.txt";
                        {
                            std::ofstream ofs(cfgPath);
                            if (ofs.is_open())
                            {
                                ofs << "# AliceRenderer build settings\n";
                                ofs << "width: "  << s_Width  << "\n";
                                ofs << "height: " << s_Height << "\n";

                                // 포함할 씬 목록
                                ofs << "scenes:\n";
                                std::vector<fs::path> includedScenes;
                                includedScenes.reserve(s_ScenePaths.size());
                                for (std::size_t i = 0; i < s_ScenePaths.size(); ++i)
                                {
                                    if (i >= s_SceneSelected.size()) continue;
                                    if (!s_SceneSelected[i]) continue;

                                    //ofs << "  - " << s_ScenePaths[i].string() << "\n";
									fs::path relScene = fs::relative(s_ScenePaths[i], projectRoot);  // 프로젝트 루트 기준으로 상대 경로 (예: "Assets/Stage1/Stage1.scene")
									ofs << "  - " << relScene.string() << "\n";

                                    includedScenes.push_back(relScene);
                                }

                                // 기본(default) 씬 선택
                                fs::path defaultScenePath;
								bool validIndex =
									s_DefaultScene >= 0 &&
									static_cast<size_t>(s_DefaultScene) < s_ScenePaths.size() &&
									s_DefaultScene < static_cast<int>(s_SceneSelected.size()) &&
									s_SceneSelected[s_DefaultScene];

                                if (validIndex)
                                {
                                    defaultScenePath = fs::relative(s_ScenePaths[s_DefaultScene], projectRoot); // 상대 경로로 가져오자. ../Assts를 Assets로 바꾸는 것
                                }
                                else if (!includedScenes.empty())
                                {
                                    defaultScenePath = includedScenes.front();
                                }

                                if (!defaultScenePath.empty())
                                {
                                    ofs << "default: " << defaultScenePath.string() << "\n";
                                }
                            }
                        }

                        ALICE_LOG_INFO("BuildSettings saved to \"%s\"", cfgPath.string().c_str());

                        // 2) 별도 스레드에서 CMake 빌드 + 리소스 복사 실행
                        g_BuildInProgress.store(true);
                        g_BuildProgress.store(0.0f);
                        g_BuildExitCode.store(-1);

                        // Export 경로 문자열은 스레드 시작 시점에 복사해 둡니다.
                        std::string exportPathStr = s_ExportPath;

                        std::thread([projectRoot, cfgPath, exportPathStr]()
                        {
                            // CMake 빌드 프로세스 시작
                            std::wstring cmd = L"cmake --build build --config Release --target AlicePlayer";

                            STARTUPINFOW        si{};
                            PROCESS_INFORMATION pi{};
                            si.cb = sizeof(si);
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_HIDE;

                            BOOL ok = CreateProcessW(
                                nullptr,
                                cmd.data(),
                                nullptr,
                                nullptr,
                                FALSE,
                                CREATE_NO_WINDOW,
                                nullptr,
                                projectRoot.wstring().c_str(),
                                &si,
                                &pi);

                            if (!ok)
                            {
                                ALICE_LOG_ERRORF("Build Game: failed to start CMake process.");
                                g_BuildInProgress.store(false);
                                g_BuildProgress.store(0.0f);
                                g_BuildExitCode.store(1);
                                return;
                            }

                            // 프로세스가 끝날 때까지 기다리면서 간단한 진행률 애니메이션
                            float p = 0.0f;
                            for (;;)
                            {
                                DWORD wait = WaitForSingleObject(pi.hProcess, 50);
                                if (wait == WAIT_TIMEOUT)
                                {
                                    p += 0.005f;
                                    if (p > 0.9f) p = 0.9f;
                                    g_BuildProgress.store(p);
                                }
                                else
                                {
                                    break;
                                }
                            }

                            DWORD exitCode = 0;
                            GetExitCodeProcess(pi.hProcess, &exitCode);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);

                            ALICE_LOG_INFO("Build Game: CMake build finished with exitCode=%lu",
                                           static_cast<unsigned long>(exitCode));

                            if (exitCode == 0)
                            {
                                // 3) Release 실행 파일 폴더로 필요한 리소스 디렉터리 복사
                                namespace fs2 = std::filesystem;
                                fs2::path releaseBinDir = projectRoot / "build/bin/Release";
                                fs2::path binParent     = releaseBinDir.parent_path(); // build/bin

                                auto copyDirIfExists = [](const fs2::path& src, const fs2::path& dst)
                                {
                                    if (!fs2::exists(src)) return;
                                    std::error_code ec;
                                    fs2::create_directories(dst, ec);
                                    fs2::copy(src, dst,
                                              fs2::copy_options::recursive | fs2::copy_options::overwrite_existing,
                                              ec);
                                };

                                copyDirIfExists(projectRoot / "Assets",   binParent / "Assets");
                                copyDirIfExists(projectRoot / "Resource", binParent / "Resource");
                                copyDirIfExists(projectRoot / "Cooked",   binParent / "Cooked");

                                // BuildSettings 도 Release 폴더에 복사
                                std::error_code ec;
                                fs2::copy_file(cfgPath, releaseBinDir / "BuildSettings.txt",
                                               fs2::copy_options::overwrite_existing, ec);

                                ALICE_LOG_INFO("Build Game: copied Assets/Resource/Cooked to \"%s\"",
                                               binParent.string().c_str());

                                // 4) 사용자가 지정한 Export 폴더로 배포용 파일 복사
                                fs2::path exportRoot = exportPathStr;
                                if (!exportRoot.is_absolute())
                                {
                                    exportRoot = projectRoot / exportRoot;
                                }

                                std::error_code ec2;
                                fs2::create_directories(exportRoot, ec2);

                                // 실행 파일 복사
                                fs2::copy_file(releaseBinDir / "AlicePlayer.exe",
                                               exportRoot / "AlicePlayer.exe",
                                               fs2::copy_options::overwrite_existing,
                                               ec2);

                                // 필요한 DLL 들 복사 (assimp, AliceScripts 등)
                                if (fs2::exists(releaseBinDir))
                                {
                                    for (const auto& entry : fs2::directory_iterator(releaseBinDir))
                                    {
                                        if (!entry.is_regular_file())
                                            continue;

                                        if (entry.path().extension() == ".dll")
                                        {
                                            std::error_code ecDll;
                                            fs2::copy_file(entry.path(),
                                                           exportRoot / entry.path().filename(),
                                                           fs2::copy_options::overwrite_existing,
                                                           ecDll);
                                        }
                                    }
                                }

                                // 리소스 디렉터리 복사
                                copyDirIfExists(binParent / "Assets",   exportRoot / "Assets");
                                copyDirIfExists(binParent / "Resource", exportRoot / "Resource");
                                copyDirIfExists(binParent / "Cooked",   exportRoot / "Cooked");

                                // 최종 빌드에는 원본 이미지(Resource/Image)는 포함하지 않습니다.
                                fs2::path exportedImageDir = exportRoot / "Resource/Image";
                                if (fs2::exists(exportedImageDir))
                                {
                                    std::error_code ecRemove;
                                    fs2::remove_all(exportedImageDir, ecRemove);
                                }

                                // BuildSettings 복사
                                fs2::copy_file(releaseBinDir / "BuildSettings.txt",
                                               exportRoot / "BuildSettings.txt",
                                               fs2::copy_options::overwrite_existing,
                                               ec2);

                                ALICE_LOG_INFO("Build Game: exported player to \"%s\"",
                                               exportRoot.string().c_str());

                                g_BuildProgress.store(1.0f);
                            }
                            else
                            {
                                g_BuildProgress.store(1.0f);
                            }

                            g_BuildExitCode.store(static_cast<long>(exitCode));
                            g_BuildInProgress.store(false);
                        }).detach();
                    }
                }
            }
            ImGui::End();
        }

        // === Hierarchy ===
        if (ImGui::Begin("Hierarchy"))
        {
            Alice::ImGuiText(L"엔티티 목록");
            ImGui::Separator();

            const auto& transforms = world.GetTransforms();
            if (transforms.empty())
            {
                Alice::ImGuiText(L"생성된 엔티티가 없습니다.");
            }
            else
            {
                EntityId entityToDelete = InvalidEntityId;

                for (const auto& [entityId, transform] : transforms)
                {
                    (void)transform;

                    const bool isSelected = (selectedEntity == entityId);
                    const std::string label = "Entity " + std::to_string(static_cast<std::uint32_t>(entityId));

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        selectedEntity = entityId;
                    }

                    // 항목 우클릭 시 컨텍스트 메뉴 표시
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Delete"))
                        {
                            entityToDelete = entityId;
                        }

                        // 현재 게임 오브젝트를 프리팹으로 저장하는 기능
                        if (ImGui::MenuItem("Save as Prefab"))
                        {
                            // Assets/Prefabs 폴더 아래에 간단한 이름으로 저장합니다.
                            namespace fs = std::filesystem;
                            const fs::path prefabDir = "../Assets/Prefabs";
                            if (!fs::exists(prefabDir))
                            {
                                fs::create_directories(prefabDir);
                            }

                            // Entity_<id>.prefab 형태의 기본 이름 사용
                            std::string baseName = "Entity_" + std::to_string(static_cast<std::uint32_t>(entityId)) + ".prefab";
                            fs::path prefabPath = prefabDir / baseName;

                            int index = 1;
                            while (fs::exists(prefabPath))
                            {
                                baseName = "Entity_" + std::to_string(static_cast<std::uint32_t>(entityId)) + "_" + std::to_string(index) + ".prefab";
                                prefabPath = prefabDir / baseName;
                                ++index;
                            }

                            Prefab::SaveToFile(world, entityId, prefabPath);
                        }

                        ImGui::EndPopup();
                    }
                }

                // 루프가 끝난 뒤에 실제 삭제를 수행합니다. (반복 중 컨테이너 수정 방지)
                if (entityToDelete != InvalidEntityId)
                {
                    world.DestroyEntity(entityToDelete);
                    if (selectedEntity == entityToDelete)
                    {
                        selectedEntity = InvalidEntityId;
                    }
                    g_SceneDirty = true;
                }
            }
        }
        ImGui::End();

        // === Inspector ===
        if (ImGui::Begin("Inspector"))
        {
            if (selectedEntity == InvalidEntityId)
            {
                Alice::ImGuiText(L"선택된 엔티티가 없습니다.");
            }
            else
            {
                ImGui::Text("Entity %u", static_cast<std::uint32_t>(selectedEntity));
                ImGui::Separator();

                // Transform 편집
                if (auto* transform = world.GetTransform(selectedEntity))
                {
                    ImGui::Text("Transform");
                    ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                    ImGui::DragFloat3("Rotation (rad)", &transform->rotation.x, 0.01f);
                    ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
                    g_SceneDirty = true;
                }
                else
                {
                    Alice::ImGuiText(L"Transform 컴포넌트가 없습니다.");
                }

                ImGui::Separator();

                // Script 컴포넌트 섹션
                ImGui::Text("Scripts");

                ScriptComponent* script = world.GetScript(selectedEntity);
                if (!script)
                {
                    Alice::ImGuiText(L"스크립트가 없습니다.");

                    // 등록된 스크립트 목록에서 하나를 선택해 추가할 수 있게 합니다.
                    std::vector<std::string> scriptNames = ScriptFactory::GetRegisteredScriptNames();
                    // 중복 이름이 있을 수 있으므로 정렬 + unique 로 정리합니다.
                    std::sort(scriptNames.begin(), scriptNames.end());
                    scriptNames.erase(std::unique(scriptNames.begin(), scriptNames.end()), scriptNames.end());
                    if (!scriptNames.empty())
                    {
                        static int selectedIndex = 0;
                        selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(scriptNames.size()) - 1);

                        if (ImGui::BeginCombo("Add Script", scriptNames[selectedIndex].c_str()))
                        {
                            for (int i = 0; i < static_cast<int>(scriptNames.size()); ++i)
                            {
                                bool isSelected = (i == selectedIndex);
                                if (ImGui::Selectable(scriptNames[i].c_str(), isSelected))
                                {
                                    selectedIndex = i;
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        if (ImGui::Button("Attach Script") && !scriptNames.empty())
                        {
                            world.AddScript(selectedEntity, scriptNames[selectedIndex]);
                            SaveScene(world);
                        }
                    }
                    else
                    {
                        Alice::ImGuiText(L"등록된 스크립트 타입이 없습니다.");
                    }
                }
                else
                {
                    ImGui::Text("Attached Script: %s", script->scriptName.c_str());

                    if (ImGui::Button("Remove Script"))
                    {
                        world.RemoveScript(selectedEntity);
                    }
                }

                ImGui::Separator();

                // Material 컴포넌트 섹션
                ImGui::Text("Material");
                if (MaterialComponent* mat = world.GetMaterial(selectedEntity))
                {
                    const bool hasAsset = !mat->assetPath.empty();
                    if (hasAsset)
                    {
                        ImGui::Text("Asset: %s", mat->assetPath.c_str());
                    }

                    bool changed = false;

                    // 인스턴스 또는 에셋 색 편집
                    changed |= ImGui::ColorEdit3("Base Color", &mat->color.x);

                    // PBR 파라미터 (0~1 범위)
                    changed |= ImGui::SliderFloat("Roughness", &mat->roughness, 0.0f, 1.0f);
                    changed |= ImGui::SliderFloat("Metalness", &mat->metalness, 0.0f, 1.0f);

                    // 알베도 텍스처 경로 표시 & 선택
                    ImGui::Separator();
                    ImGui::Text("Albedo Texture");
                    if (!mat->albedoTexturePath.empty())
                    {
                        ImGui::TextWrapped("%s", mat->albedoTexturePath.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("None");
                    }
                    if (ImGui::Button("Browse Texture..."))
                    {
                        // 간단한 파일 열기 대화상자 (이미지 선택)
                        wchar_t fileBuffer[MAX_PATH] = {};
                        OPENFILENAMEW ofn{};
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner   = m_hwnd;
                        ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
                        ofn.lpstrFile   = fileBuffer;
                        ofn.nMaxFile    = MAX_PATH;
                        ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                        if (GetOpenFileNameW(&ofn))
                        {
                            std::filesystem::path src = fileBuffer;
                            // 아주 단순하게: 원본 이미지를 그대로 경로로 사용합니다.
                            // (필요하다면 ResourceManager 를 통해 .abtex 로 쿠킹하는 것으로 확장 가능)
                            mat->albedoTexturePath = src.string();
                            changed = true;

                            char buf[256] = {};
                            std::snprintf(buf, sizeof(buf),
                                          "[Editor] Material albedo set from Inspector: \"%s\"\n",
                                          mat->albedoTexturePath.c_str());
                            OutputDebugStringA(buf);
                        }
                    }

                    if (changed)
                    {
                        if (hasAsset)
                        {
                            // 1) 에셋 파일 저장
                            MaterialFile::Save(mat->assetPath, *mat);

                            // 2) 같은 에셋을 참조하는 모든 엔티티의 머티리얼을 갱신
                            const std::string targetPath = mat->assetPath;
                            const auto& allMats = world.GetMaterials();
                            for (const auto& [id, matConst] : allMats)
                            {
                                (void)matConst;
                                MaterialComponent* other = world.GetMaterial(id);
                                if (!other) continue;
                                if (other->assetPath == targetPath)
                                {
                                    other->color             = mat->color;
                                    other->roughness         = mat->roughness;
                                    other->metalness         = mat->metalness;
                                    other->albedoTexturePath = mat->albedoTexturePath;
                                }
                            }
                        }

                        g_SceneDirty = true;
                    }

                    // Assets 폴더에서 커스텀 머티리얼 선택
                    if (ImGui::Button("Assign From Asset..."))
                    {
                        ImGui::OpenPopup("SelectMaterialAssetPopup");
                    }

                    if (ImGui::BeginPopup("SelectMaterialAssetPopup"))
                    {
                        namespace fs = std::filesystem;
                        const fs::path assetsRoot = "../Assets";

                        if (fs::exists(assetsRoot))
                        {
                            for (const auto& entry : fs::recursive_directory_iterator(assetsRoot))
                            {
                                if (!entry.is_regular_file())
                                    continue;

                                if (entry.path().extension() != ".mat")
                                    continue;

                                const std::string name = entry.path().filename().string();
                                if (ImGui::Selectable(name.c_str()))
                                {
                                    if (MaterialFile::Load(entry.path(), *mat))
                                    {
                                        mat->assetPath = entry.path().string();
                                        g_SceneDirty   = true;
                                    }
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::Button("Remove Material"))
                    {
                        world.RemoveMaterial(selectedEntity);
                        g_SceneDirty = true;
                    }
                }

                // Skinned Mesh / Bone 정보 + 서브메시 텍스처
                if (SkinnedMeshComponent* skinned = world.GetSkinnedMesh(selectedEntity))
                {
                    ImGui::Separator();
                    ImGui::Text("Skinned Mesh");
                    ImGui::Text("Mesh Key: %s", skinned->meshAssetPath.c_str());

                    std::shared_ptr<SkinnedMeshGPU> mesh;
                    if (m_skinnedRegistry)
                    {
                        mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
                    }

                    // 본 트리 정보
                    if (mesh && !mesh->skeletonText.empty())
                    {
                        static bool s_showBoneDetails = true;
                        ImGui::Checkbox("Show Bone Details", &s_showBoneDetails);
                        if (s_showBoneDetails)
                        {
                            ImGui::BeginChild("BoneCard", ImVec2(0, 160), true, ImGuiWindowFlags_HorizontalScrollbar);
                            ImGui::TextUnformatted(mesh->skeletonText.c_str());
                            ImGui::EndChild();
                        }
                    }

                    // 서브메시별 텍스처 / 머티리얼 슬롯 선택 (언리얼의 Element 리스트 느낌)
                    if (mesh && !mesh->subsets.empty())
                    {
                        ImGui::Separator();
                        ImGui::Text("Submesh / Material Slots");
                        ImGui::Text("Submeshes: %zu", mesh->subsets.size());

                        static int s_selectedSubset = 0;
                        if (s_selectedSubset < 0) s_selectedSubset = 0;
                        if (s_selectedSubset >= (int)mesh->subsets.size())
                            s_selectedSubset = (int)mesh->subsets.size() - 1;

                        ImGui::BeginChild("SubmeshList", ImVec2(0, 120), true);
                        for (int i = 0; i < (int)mesh->subsets.size(); ++i)
                        {
                            const FbxSubset& subset = mesh->subsets[(std::size_t)i];
                            char label[128] = {};
                            std::snprintf(label, sizeof(label), "Subset %d (Mat %u)", i, subset.materialIndex);

                            const bool isSelected = (i == s_selectedSubset);
                            if (ImGui::Selectable(label, isSelected))
                            {
                                s_selectedSubset = i;
                            }
                        }
                        ImGui::EndChild();

                        const FbxSubset& subset = mesh->subsets[(std::size_t)s_selectedSubset];
                        ImGui::Separator();
                        ImGui::Text("Selected Subset %d", s_selectedSubset);
                        ImGui::Text("  startIndex : %u", subset.startIndex);
                        ImGui::Text("  indexCount : %u", subset.indexCount);
                        ImGui::Text("  materialIndex : %u", subset.materialIndex);

                        const std::size_t matIndex = (std::size_t)subset.materialIndex;
                        if (matIndex < mesh->materialOverridePaths.size())
                        {
                            ImGui::Separator();
                            ImGui::Text("Albedo Texture (Instance Override)");

                            const std::string& texPath = mesh->materialOverridePaths[matIndex];
                            if (!texPath.empty())
                            {
                                ImGui::TextWrapped("%s", texPath.c_str());
                            }
                            else
                            {
                                ImGui::TextDisabled("FBX Original (no override)");
                            }

                            if (ImGui::Button("Browse Texture for This Slot"))
                            {
                                wchar_t fileBuffer[MAX_PATH] = {};
                                OPENFILENAMEW ofn{};
                                ofn.lStructSize = sizeof(ofn);
                                ofn.hwndOwner   = m_hwnd;
                                ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
                                ofn.lpstrFile   = fileBuffer;
                                ofn.nMaxFile    = MAX_PATH;
                                ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                                if (GetOpenFileNameW(&ofn))
                                {
                                    std::filesystem::path src = fileBuffer;

                                    // 서브메시용 인스턴스 텍스처를 GPU 에 로드
                                    Microsoft::WRL::ComPtr<ID3D11Resource> tex;
                                    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
                                    HRESULT hr = DirectX::CreateWICTextureFromFile(
                                        m_renderDevice->GetDevice(),
                                        src.c_str(),
                                        tex.GetAddressOf(),
                                        srv.GetAddressOf());

                                    if (SUCCEEDED(hr) && srv)
                                    {
                                        if (matIndex < mesh->materialSRVs.size())
                                        {
                                            mesh->materialSRVs[matIndex] = srv;
                                        }
                                        if (matIndex < mesh->materialOverridePaths.size())
                                        {
                                            mesh->materialOverridePaths[matIndex] = src.string();
                                        }

                                        char buf[256] = {};
                                        std::snprintf(buf, sizeof(buf),
                                                      "[Editor] Submesh texture override: mesh=\"%s\" subset=%d matIndex=%zu path=\"%s\"\n",
                                                      skinned->meshAssetPath.c_str(),
                                                      s_selectedSubset,
                                                      matIndex,
                                                      mesh->materialOverridePaths[matIndex].c_str());
                                        OutputDebugStringA(buf);
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    Alice::ImGuiText(L"머티리얼이 없습니다.");
                    if (ImGui::Button("Add Default Material"))
                    {
                        DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                        world.AddMaterial(selectedEntity, defaultColor);
                        g_SceneDirty = true;
                    }
                }
            }
        }
        ImGui::End();

        // === Project ===
        if (ImGui::Begin("Project"))
        {
            Alice::ImGuiText(L"Assets 폴더");
            ImGui::Separator();

            // Unity 스타일로 프로젝트 루트 하위의 Assets 폴더를 기준으로 디렉터리를 보여줍니다.
            const std::filesystem::path assetsRoot = "../Assets";
            if (!std::filesystem::exists(assetsRoot))
            {
                // 폴더가 없다면 한 번만 생성해 둡니다.
                std::filesystem::create_directories(assetsRoot);
            }

            DrawDirectoryNode(world, selectedEntity, assetsRoot);
        }
        ImGui::End();

        // === Game ===
        if (ImGui::Begin("Game"))
        {
            Alice::ImGuiText(L"게임 상태");
            ImGui::Separator();
            ImGui::Text("Play State : %s", isPlaying ? "Playing" : "Stopped");

            if (ID3D11ShaderResourceView* sceneSRV = forward.GetSceneColorSRV())
            {
                const float sceneWidth  = static_cast<float>(forward.GetSceneWidth());
                const float sceneHeight = static_cast<float>(forward.GetSceneHeight());

                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImVec2 size  = avail;

                if (sceneWidth > 0.0f && sceneHeight > 0.0f)
                {
                    const float aspectScene  = sceneWidth / sceneHeight;
                    const float aspectAvail  = (avail.y > 0.0f) ? (avail.x / avail.y) : aspectScene;

                    if (aspectAvail > aspectScene)
                    {
                        size.x = avail.y * aspectScene;
                        size.y = avail.y;
                    }
                    else
                    {
                        size.x = avail.x;
                        size.y = avail.x / aspectScene;
                    }
                }

                ImVec2 imagePos = ImGui::GetCursorScreenPos();
                ImGui::Image(sceneSRV, size);

                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    ImGuiIO& io = ImGui::GetIO();
                    ImVec2 mousePos = io.MousePos;

                    const float localX = mousePos.x - imagePos.x;
                    const float localY = mousePos.y - imagePos.y;

                    if (localX >= 0.0f && localX <= size.x &&
                        localY >= 0.0f && localY <= size.y)
                    {
                        const float u = (size.x > 0.0f) ? (localX / size.x) : 0.0f;
                        const float v = (size.y > 0.0f) ? (localY / size.y) : 0.0f;
                        EntityId hit = picker.Pick(world, camera, u, v);
                        selectedEntity = hit;
                    }
                }
            }
            else
            {
                Alice::ImGuiText("씬 텍스처가 아직 준비되지 않았습니다.");
            }
        }
        ImGui::End();

        // === Camera ===
        if (ImGui::Begin("Camera"))
        {
            Alice::ImGuiText(L"카메라 정보");
            ImGui::Separator();

            XMFLOAT3 camPos = camera.GetPosition();
            ImGui::Text("Position : (%.2f, %.2f, %.2f)",
                        camPos.x, camPos.y, camPos.z);

            ImGui::Separator();
            Alice::ImGuiText(L"카메라 설정");

            // FOV / near / far 는 Camera 내부 상태를 그대로 읽어와서 수정합니다.
            float fovDeg = XMConvertToDegrees(camera.GetFovYRadians());
            float nearPlane = camera.GetNearPlane();
            float farPlane  = camera.GetFarPlane();

            bool changed = false;
            changed |= ImGui::SliderFloat("FOV (deg)", &fovDeg, 20.0f, 120.0f);
            changed |= ImGui::DragFloat("Near Plane",  &nearPlane, 0.01f, 0.01f, 10.0f, "%.3f");
            changed |= ImGui::DragFloat("Far Plane",   &farPlane,  1.0f,  10.0f, 5000.0f, "%.1f");

            // 카메라 이동 속도 (엔진에서 사용하는 값)
            ImGui::SliderFloat("Move Speed", &cameraMoveSpeed, 0.1f, 50.0f, "%.2f");

            if (changed)
            {
                // 값이 바뀐 경우, 기존 종횡비를 유지한 채로 투영 행렬을 재설정합니다.
                float fovRad  = XMConvertToRadians(fovDeg);
                float aspect  = camera.GetAspectRatio();
                // near/far 가 뒤집히지 않도록 간단히 보정
                nearPlane = (std::max)(nearPlane, 0.01f);
                farPlane  = (std::max)(farPlane,  nearPlane + 0.1f);
                camera.SetPerspective(fovRad, aspect, nearPlane, farPlane);
            }
        }
        ImGui::End();

        // === Lighting ===
        if (ImGui::Begin("Lighting"))
        {
            int mode = shadingMode;
            if (ImGui::RadioButton("Lambert", mode == 0))   mode = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("Phong", mode == 1))     mode = 1;
            ImGui::SameLine();
            if (ImGui::RadioButton("Blinn-Phong", mode == 2)) mode = 2;
            ImGui::SameLine();
            if (ImGui::RadioButton("Toon", mode == 3))      mode = 3;
            ImGui::SameLine();
            if (ImGui::RadioButton("PBR", mode == 4))       mode = 4;
            shadingMode = mode;

            Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

            auto& lighting = forward.GetLightingParameters();
            Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
                                    &lighting.keyIntensity,
                                    0.0f,
                                    3.0f);
            Alice::ImGuiSliderFloat(L"Fill Intensity (보조광)",
                                    &lighting.fillIntensity,
                                    0.0f,
                                    3.0f);
            ImGui::SliderFloat("Shininess",      &lighting.shininess,     2.0f, 128.0f);
            ImGui::ColorEdit3("Diffuse Color",  &lighting.diffuseColor.x);
            ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);

            Alice::ImGuiSliderFloat3(L"Key Direction (주광)",
                                     &lighting.keyDirection.x,
                                     -1.0f,
                                     1.0f);
            Alice::ImGuiSliderFloat3(L"Fill Direction (보조광)",
                                     &lighting.fillDirection.x,
                                     -1.0f,
                                     1.0f);
        }
        ImGui::End();

        // === Material Asset Editor (.mat 더블클릭 시) ===
        if (g_MaterialEditorOpen)
        {
            if (ImGui::Begin("Material Asset Editor", &g_MaterialEditorOpen))
            {
                ImGui::Text("Asset: %s", g_MaterialEditorPath.string().c_str());
                ImGui::Separator();

                bool changed = false;
                changed |= ImGui::ColorEdit3("Base Color", &g_MaterialEditorData.color.x);
                changed |= ImGui::SliderFloat("Roughness", &g_MaterialEditorData.roughness, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Metalness", &g_MaterialEditorData.metalness, 0.0f, 1.0f);

                ImGui::Separator();
                ImGui::Text("Albedo Texture");
                if (!g_MaterialEditorData.albedoTexturePath.empty())
                {
                    ImGui::TextWrapped("%s", g_MaterialEditorData.albedoTexturePath.c_str());
                }
                else
                {
                    ImGui::TextDisabled("None");
                }
                if (ImGui::Button("Browse Texture##Mat"))
                {
                    wchar_t fileBuffer[MAX_PATH] = {};
                    OPENFILENAMEW ofn{};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = m_hwnd;
                    ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
                    ofn.lpstrFile   = fileBuffer;
                    ofn.nMaxFile    = MAX_PATH;
                    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

                    if (GetOpenFileNameW(&ofn))
                    {
                        std::filesystem::path src = fileBuffer;
                        g_MaterialEditorData.albedoTexturePath = src.string();
                        changed = true;

                        char buf[256] = {};
                        std::snprintf(buf, sizeof(buf),
                                      "[Editor] Material albedo set from MatEditor: \"%s\"\n",
                                      g_MaterialEditorData.albedoTexturePath.c_str());
                        OutputDebugStringA(buf);
                    }
                }

                if (changed)
                {
                    // 1) 에셋 파일에 저장
                    MaterialFile::Save(g_MaterialEditorPath, g_MaterialEditorData);

                    // 2) 이 에셋을 참조하는 모든 엔티티의 MaterialComponent 를 갱신
                    const std::string targetPath = g_MaterialEditorPath.string();
                    const auto& allMats = world.GetMaterials();
                    for (const auto& [id, matConst] : allMats)
                    {
                        MaterialComponent* mat = world.GetMaterial(id);
                        if (!mat) continue;
                        if (mat->assetPath == targetPath)
                        {
                            mat->color     = g_MaterialEditorData.color;
                            mat->roughness = g_MaterialEditorData.roughness;
                            mat->metalness = g_MaterialEditorData.metalness;
                        }
                    }

                    g_SceneDirty = true;
                }
            }
            ImGui::End();
        }

        // === 씬 변경사항 저장 확인 모달 ===
        if (g_RequestSceneLoad)
        {
            // 현재 씬이 존재하고 변경사항이 있을 때만 확인 모달을 띄웁니다.
            if (g_HasCurrentScenePath && g_SceneDirty)
            {
                ImGui::OpenPopup("SaveSceneBeforeLoad");
            }
            else
            {
                // 저장할 필요가 없으면 바로 로드
                {
                    char buf[256] = {};
                    std::snprintf(buf, sizeof(buf),
                                  "[Editor] SceneFile::Load (no-save path): \"%s\"\n",
                                  g_NextScenePath.u8string().c_str());
                    OutputDebugStringA(buf);
                }
                SceneFile::Load(world, g_NextScenePath);
                EnsureSkinnedMeshesRegistered(world);
                selectedEntity       = InvalidEntityId;
                g_CurrentScenePath   = g_NextScenePath;
                g_HasCurrentScenePath = true;
                g_SceneDirty         = false;
            }
            g_RequestSceneLoad = false;
        }

        if (ImGui::BeginPopupModal("SaveSceneBeforeLoad", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            Alice::ImGuiText(L"현재 씬의 변경 내용을 저장하시겠습니까?");
            ImGui::Separator();

            if (ImGui::Button("Save"))
            {
                SaveScene(world);
                LoadScene(world);
                selectedEntity = InvalidEntityId;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Don't Save"))
            {
                {
                    char buf[256] = {};
                    std::snprintf(buf, sizeof(buf),
                                  "[Editor] SceneFile::Load (dont-save): \"%s\"\n",
                                  g_NextScenePath.u8string().c_str());
                    OutputDebugStringA(buf);
                }
                SceneFile::Load(world, g_NextScenePath);
                EnsureSkinnedMeshesRegistered(world);
                selectedEntity        = InvalidEntityId;
                g_CurrentScenePath    = g_NextScenePath;
                g_HasCurrentScenePath = true;
                g_SceneDirty          = false;

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                // 아무것도 하지 않고 씬 로드를 취소합니다.
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorCore::DrawDirectoryNode(World& world,
                                       EntityId& selectedEntity,
                                       const std::filesystem::path& path)
    {
        namespace fs = std::filesystem;
        if (!fs::exists(path)) return;

        const bool        isDirectory = fs::is_directory(path);
        const std::string label       = path.filename().string();

        ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth;

        // 파일/폴더 이름 변경 상태를 관리하는 간단한 정적 상태입니다.
        static bool                 s_renaming      = false;
        static std::filesystem::path s_renamingPath;
        static char                 s_renameBuffer[260] = {};
        static bool                 s_renameFocus   = false;

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
                        s_renaming     = true;
                        s_renamingPath = path;
                        s_renameFocus  = true;
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
                            hfs << "#include \"Core/Script.h\"\n\n";
                            hfs << "namespace Alice\n";
                            hfs << "{\n";
                            hfs << "    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.\n";
                            hfs << "    class " << className << " : public IScript\n";
                            hfs << "    {\n";
                            hfs << "    public:\n";
                            hfs << "        const char* GetName() const override { return \"" << className << "\"; }\n\n";
                            hfs << "        void OnCreate(World& world, EntityId entity) override;\n";
                            hfs << "        void OnUpdate(World& world, EntityId entity, float deltaTime) override;\n";
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
                            cfs << "#include \"Core/World.h\"\n\n";
                            cfs << "namespace Alice\n";
                            cfs << "{\n";
                            cfs << "    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.\n";
                            cfs << "    REGISTER_SCRIPT(" << className << ");\n\n";
                            cfs << "    void " << className << "::OnCreate(World& world, EntityId entity)\n";
                            cfs << "    {\n";
                            cfs << "        // 초기화 로직을 여기에 작성하세요.\n";
                            cfs << "    }\n\n";
                            cfs << "    void " << className << "::OnUpdate(World& world, EntityId entity, float deltaTime)\n";
                            cfs << "    {\n";
                            cfs << "        // 매 프레임 호출되는 로직을 여기에 작성하세요.\n";
                            cfs << "    }\n";
                            cfs << "}\n";
                        }
                    }
                }

                if (ImGui::MenuItem("Create Prefab"))
                {
                    // 아주 단순한 기본 프리팹 파일 생성 (.prefab)
                    fs::path newPath = path / "NewPrefab.prefab";
                    int index = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = path / ("NewPrefab" + std::to_string(index) + ".prefab");
                        ++index;
                    }

                    std::ofstream ofs(newPath);
                    if (ofs.is_open())
                    {
                        ofs << "name: NewPrefab\n";
                        ofs << "position: 0 0 0\n";
                        ofs << "rotation: 0 0 0\n";
                        ofs << "scale: 1 1 1\n";
                        ofs << "script: \n";
                    }
                }

                if (ImGui::MenuItem("Create Material"))
                {
                    fs::path newPath = path / "NewMaterial.mat";
                    int index = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = path / ("NewMaterial" + std::to_string(index) + ".mat");
                        ++index;
                    }

                    std::ofstream ofs(newPath);
                    if (ofs.is_open())
                    {
                        ofs << "name: " << newPath.stem().string() << "\n";
                        ofs << "color: 0.7 0.7 0.7\n";
                        ofs << "roughness: 0.5\n";
                        ofs << "metalness: 0.0\n";
                    }
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

                    std::ofstream ofs(newPath);
                    if (ofs.is_open())
                    {
                        ofs << "# AliceRenderer scene\n";
                    }
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
            }
            ImGui::PopID();

            // 파일 노드를 더블클릭하면 파일 형식에 따라 동작합니다.
            if (!isRenamingThis &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx")
                {
                    fs::path absPath = fs::absolute(path);
                    std::wstring wpath = absPath.wstring();
                    ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                else if (ext == ".scene")
                {
                    // 씬 파일을 더블클릭하면, 필요한 경우 저장 여부를 물은 뒤 로드합니다.
                    g_NextScenePath    = path;
                    g_RequestSceneLoad = true;
                }
                else if (ext == ".mat")
                {
                    // 머티리얼 에셋 전용 편집 창을 엽니다.
                    g_MaterialEditorPath = path;
                    g_MaterialEditorData = {};
                    // 파일에서 값을 불러옵니다. 실패하면 기본 값으로 남겨둡니다.
                    MaterialFile::Load(path, g_MaterialEditorData);
                    g_MaterialEditorData.assetPath = path.string();
                    g_MaterialEditorOpen = true;
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
                    s_renaming      = true;
                    s_renamingPath  = path;
                    s_renameFocus   = true;
                    // 바로 인라인 입력 박스를 보여주기 위해 팝업을 닫습니다.
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::MenuItem("Delete"))
                {
                    std::error_code ec;
                    fs::remove(path, ec);
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
                            g_SceneDirty   = true;
                        }
                    }
                }

                // 머티리얼 파일에 대한 간단한 적용 기능
                if (ext == ".mat")
                {
                    if (ImGui::MenuItem("Assign To Selected Entity") &&
                        selectedEntity != InvalidEntityId &&
                        world.GetTransform(selectedEntity))
                    {
                        MaterialComponent* mat = world.GetMaterial(selectedEntity);
                        if (!mat)
                        {
                            DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                            mat = &world.AddMaterial(selectedEntity, defaultColor);
                        }

                        if (mat)
                        {
                            MaterialFile::Load(path, *mat);
                            mat->assetPath = path.string();
                            g_SceneDirty   = true;
                        }
                    }
                }

                // 씬 파일 저장/로드
                if (ext == ".scene")
                {
                    if (ImGui::MenuItem("Load Scene"))
                    {
                        g_NextScenePath      = path;
                        g_RequestSceneLoad   = true;
                    }
                    if (ImGui::MenuItem("Save Current Scene"))
                    {
                        SceneFile::Save(world, path);
                        g_CurrentScenePath    = path;
                        g_HasCurrentScenePath = true;
                        g_SceneDirty          = false;
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
                            {
                                char buf[512] = {};
                                std::snprintf(buf, sizeof(buf),
                                              "[Editor] Instantiate FBX: assetPath=\"%s\" sourceFbx=\"%s\" meshKey=\"%s\" mats=%zu\n",
                                              path.u8string().c_str(),
                                              asset.sourceFbx.c_str(),
                                              asset.meshAssetPath.c_str(),
                                              asset.materialAssetPaths.size());
                                OutputDebugStringA(buf);
                            }

                            // 레지스트리에 GPU 메시가 없다면, 원본 FBX 를 다시 임포트해서 등록합니다.
                            if (m_skinnedRegistry && m_resources && m_renderDevice)
                            {
                                if (!m_skinnedRegistry->Find(asset.meshAssetPath))
                                {
                                    FbxImportOptions opt{};
                                    FbxImporter importer(*m_resources, m_skinnedRegistry);
                                    auto* device = m_renderDevice->GetDevice();
                                    // 원본 FBX 경로는 .fbxasset 안의 source_fbx 에 저장되어 있습니다.
                                    std::filesystem::path srcFbxPath = asset.sourceFbx;
                                    importer.Import(device, srcFbxPath, opt);

                                    OutputDebugStringA("[Editor] Instantiate FBX: mesh was not in registry, re-imported FBX\n");
                                }
                                else
                                {
                                    OutputDebugStringA("[Editor] Instantiate FBX: mesh already in registry\n");
                                }
                            }

                            EntityId e = world.CreateEntity();
                            TransformComponent& t = world.AddTransform(e);
                            t.position = { 0.0f, 0.0f, 0.0f };
                            t.scale    = { 1.0f, 1.0f, 1.0f };
                            t.rotation = { 0.0f, 0.0f, 0.0f };

                            SkinnedMeshComponent& skinned = world.AddSkinnedMesh(e, asset.meshAssetPath);
                            skinned.instanceAssetPath     = path.string();
                            static DirectX::XMFLOAT4X4 s_identityBone =
                                DirectX::XMFLOAT4X4(1,0,0,0,
                                                    0,1,0,0,
                                                    0,0,1,0,
                                                    0,0,0,1);
                            skinned.boneMatrices = &s_identityBone;
                            skinned.boneCount    = 1;

                            {
                                char buf[256] = {};
                                std::snprintf(buf, sizeof(buf),
                                              "[Editor] Instantiate FBX: created entity=%u, boneCount=%u\n",
                                              static_cast<unsigned>(e),
                                              skinned.boneCount);
                                OutputDebugStringA(buf);
                            }

                            if (!asset.materialAssetPaths.empty())
                            {
                                DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                                MaterialComponent& mat = world.AddMaterial(e, defaultColor);
                                mat.assetPath = asset.materialAssetPaths.front();
                                MaterialFile::Load(mat.assetPath, mat);
                            }

                            selectedEntity = e;
                            g_SceneDirty   = true;
                        }
                    }
                }

                ImGui::EndPopup();
            }
        }
    }

    void EditorCore::EnsureSkinnedMeshesRegistered(World& world)
    {
        if (!m_skinnedRegistry || !m_resources || !m_renderDevice)
            return;

        const auto& skinnedMap = world.GetSkinnedMeshes();
        if (skinnedMap.empty())
            return;

        auto* device = m_renderDevice->GetDevice();

        for (const auto& [entityId, comp] : skinnedMap)
        {
            if (comp.meshAssetPath.empty())
                continue;

            if (m_skinnedRegistry->Find(comp.meshAssetPath))
                continue; // 이미 등록됨

            // .fbxasset 경로를 우선 사용, 없으면 관례적으로 Assets/Fbx/<mesh>.fbxasset 시도
            std::filesystem::path fbxAssetPath;
            if (!comp.instanceAssetPath.empty())
            {
                fbxAssetPath = comp.instanceAssetPath;
            }
            else
            {
                fbxAssetPath = std::filesystem::path("../Assets/Fbx")
                             / (comp.meshAssetPath + ".fbxasset");
            }

            Alice::FbxInstanceAsset instance{};
            if (!Alice::LoadFbxInstanceAsset(fbxAssetPath, instance))
            {
                char buf[256] = {};
                std::snprintf(buf, sizeof(buf),
                              "[Editor] EnsureSkinnedMeshesRegistered: failed to load .fbxasset \"%s\" for meshKey=\"%s\"\n",
                              fbxAssetPath.u8string().c_str(),
                              comp.meshAssetPath.c_str());
                OutputDebugStringA(buf);
                continue;
            }

            if (instance.sourceFbx.empty())
            {
                char buf[256] = {};
                std::snprintf(buf, sizeof(buf),
                              "[Editor] EnsureSkinnedMeshesRegistered: .fbxasset has empty source_fbx for \"%s\"\n",
                              fbxAssetPath.u8string().c_str());
                OutputDebugStringA(buf);
                continue;
            }

            // 원본 FBX 를 다시 임포트해서 SkinnedMeshRegistry 에 등록
            FbxImportOptions opt{};
            FbxImporter importer(*m_resources, m_skinnedRegistry);

            std::filesystem::path srcFbxPath = instance.sourceFbx;
            FbxImportResult result = importer.Import(device, srcFbxPath, opt);

            char buf[512] = {};
            std::snprintf(buf, sizeof(buf),
                          "[Editor] EnsureSkinnedMeshesRegistered: re-import FBX \"%s\" -> meshKey=\"%s\" result.mesh=\"%s\"\n",
                          srcFbxPath.u8string().c_str(),
                          comp.meshAssetPath.c_str(),
                          result.meshAssetPath.c_str());
            OutputDebugStringA(buf);
        }
    }
    void EditorCore::SaveScene(World& world)
    {
		std::filesystem::path savePath = g_CurrentScenePath;
		if (savePath.empty())
		{
			savePath = "../Assets/AutoSaved.scene";
		}
		{
			char buf[256] = {};
			std::snprintf(buf, sizeof(buf),
				"[Editor] SceneFile::Save: \"%s\"\n",
				savePath.u8string().c_str());
			OutputDebugStringA(buf);
		}
		SceneFile::Save(world, savePath);
		g_CurrentScenePath = savePath;
		g_HasCurrentScenePath = true;
		g_SceneDirty = false;

    }
    void EditorCore::LoadScene(World& world)
    {
		{
			char buf[256] = {};
			std::snprintf(buf, sizeof(buf),
				"[Editor] SceneFile::Load (after save): \"%s\"\n",
				g_NextScenePath.u8string().c_str());
			OutputDebugStringA(buf);
		}
		SceneFile::Load(world, g_NextScenePath);
		EnsureSkinnedMeshesRegistered(world);
		g_CurrentScenePath = g_NextScenePath;
		g_HasCurrentScenePath = true;
		g_SceneDirty = false;

    }
}



