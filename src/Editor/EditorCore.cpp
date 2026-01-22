#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Editor/EditorCore.h"

#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Rendering/DeferredRenderSystem.h"
#include "Rendering/ForwardRenderSystem.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "Core/ImGuiEx.h"
#include "Core/ScriptHotReload.h"
#include "Core/ResourceManager.h"
#include "Core/GameObject.h"
#include "Game/FbxImporter.h"
#include "3Dmodel/FbxModel.h"
#include "Core/Logger.h"
#include "Core/ReflectionUI.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"
#include <set>
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"

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
#include <Core/Prefab.h>
#include <Core/IScript.h>
#include <Core/ScriptSystem.h>
#include <Core/ScriptFactory.h>
#include <Core/Material.h>
#include <Core/SceneFile.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ShlObj.h>   // 폴더 선택 다이얼로그 (SHBrowseForFolderW)
#include <Game/FbxAsset.h>
#include "json/json.hpp"

// 텍스처 로딩용 DirectXTK
#include <DirectXTK/WICTextureLoader.h>

using namespace DirectX;

namespace Alice
{
    namespace
    {
        // Build Game 진행 상황 전역 (아래쪽에서 정의됨)
        extern std::atomic<bool>  g_BuildInProgress;
        extern std::atomic<float> g_BuildProgress;
        extern std::atomic<long>  g_BuildExitCode;

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

        void ReloadScripts_FromButton(World& world)
        {
            using namespace std::filesystem;

            // 1) 실행 파일 위치 기준으로 프로젝트 루트 / ScriptsBuild 경로 계산
            wchar_t exePathW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
            path exePath = exePathW;
            path exeDir  = exePath.parent_path();
            path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
            path scriptsRoot = projectRoot / "ScriptsBuild";
			path scriptsCMakePath = scriptsRoot / "CMakeLists.txt";
            path scriptsBuildDir = scriptsRoot / "build";

            if (!exists(scriptsCMakePath))
            {
                ALICE_LOG_ERRORF("Reload Scripts: ScriptsBuild/CMakeLists.txt not found. path=\"%s\"",
                                 (scriptsCMakePath).string().c_str());
                return;
            }

#ifdef _DEBUG
			constexpr const wchar_t* kConfig = L"Debug";
#else
			constexpr const wchar_t* kConfig = L"Release";
#endif

			// ----------------------------------------------------------------------
			// 1: Configure 명령어 수정
			// cmd /C "cmake -S "..." -B "..." || pause"
			// ----------------------------------------------------------------------
			std::wstring cmdConfig = L"cmd /C \"cmake -S \"";
			cmdConfig += scriptsRoot.wstring();
			cmdConfig += L"\" -B \"";
			cmdConfig += scriptsBuildDir.wstring();
			cmdConfig += L"\" || pause\""; 

			// Configure 실행
			if (ExecuteCommandWithConsole(cmdConfig.c_str()) != 0)
			{
				ALICE_LOG_ERRORF("Reload Scripts: CMake Configure failed.");
				return;
			}

			// ----------------------------------------------------------------------
			// 2: Build 명령어 수정
			// cmd /C "cmake --build "..." --config ... || pause"
			// ----------------------------------------------------------------------
			std::wstring cmdBuild = L"cmd /C \"cmake --build \"";
			cmdBuild += scriptsBuildDir.wstring();
			cmdBuild += L"\" --config ";
			cmdBuild += kConfig;
			cmdBuild += L" --target AliceScripts || pause\"";

			// Build 실행
			if (ExecuteCommandWithConsole(cmdBuild.c_str()) != 0)
			{
				ALICE_LOG_ERRORF("Reload Scripts: CMake Build failed.");
				return;
			}

            // 4) ScriptsBuild/build/<Config>/AliceScripts.dll 을 실행 파일 옆으로 복사
            path builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
            if (!exists(builtDll))
            {
                ALICE_LOG_ERRORF("Reload Scripts: built DLL not found: \"%s\"",
                                 builtDll.string().c_str());
                return;
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
                return;
            }

            ALICE_LOG_INFO("Reload Scripts: copied \"%s\" -> \"%s\"",
                           builtDll.string().c_str(),
                           targetDll.string().c_str());

            // 6) 새 DLL 로드
            ScriptHotReload_Reload();

            // 새 DLL의 vtable/RTTR이 준비된 뒤에 인스턴스를 다시 만듭니다.
            RestoreScripts(world, snaps);
        }

        // 빌드/배포용 간단 파일 유틸 (에러는 로그로 남기고, 실패는 false 반환)
        bool CopyDirTree(const std::filesystem::path& src, const std::filesystem::path& dst)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(src, ec) || ec) return true; // 없는 건 스킵
            fs::create_directories(dst, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("BuildGame: create_directories failed. dst=\"%s\" (%s)",
                                 dst.string().c_str(), ec.message().c_str());
                return false;
            }
            fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("BuildGame: copy dir failed. \"%s\" -> \"%s\" (%s)",
                                 src.string().c_str(), dst.string().c_str(), ec.message().c_str());
                return false;
            }
            return true;
        }

        bool CopyFileOver(const std::filesystem::path& src, const std::filesystem::path& dst)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(dst.parent_path(), ec);
            ec.clear();
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("BuildGame: copy file failed. \"%s\" -> \"%s\" (%s)",
                                 src.string().c_str(), dst.string().c_str(), ec.message().c_str());
                return false;
            }
            return true;
        }

        bool MakeCleanDir(const std::filesystem::path& dir)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (fs::exists(dir, ec))
            {
                ec.clear();
                fs::remove_all(dir, ec);
            }
            ec.clear();
            fs::create_directories(dir, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("BuildGame: create clean dir failed. \"%s\" (%s)",
                                 dir.string().c_str(), ec.message().c_str());
                return false;
            }
            return true;
        }

        // srcRoot의 모든 파일을 dstCookedRoot/<rel>.alice 로 "암호화 저장"합니다(폴더 구조 유지, 확장자는 .alice로 통일).
        // - 이미 암호화된 .alice 는 그대로 복사합니다(중복 암호화 방지).
        // - excludePrefixRel(예: "Resource/")로 시작하는 rel 경로는 스킵할 수 있습니다.
        bool CookAllIntoCookedRoot(const std::filesystem::path& srcRoot,
                                   const std::filesystem::path& dstCookedRoot,
                                   const std::string& excludePrefixRel = {})
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(srcRoot, ec) || ec) return true; // 없는 건 스킵
            if (!fs::is_directory(srcRoot, ec) || ec) return true;

            // 싱글스레드로 도는 버전임. 오류나면 이걸로 빌드ㄱ
            //Alice::ResourceManager rm;
			//if (alreadyEncrypted)
			//{
			//	// .alice → .alice 로 그대로 복사 (경로/파일명은 rel 기준으로 새로 배치)
			//	if (!CopyFileOver(inPath, outPath))
			//		return false;
			//}
			//else
			//{
			//	// 디버그: 어떤 파일이 어떤 경로로 cook 되는지 전부 로그로 남깁니다.
			//	ALICE_LOG_INFO("CookFile: in=\"%s\" -> out=\"%s\"",
			//		inPath.string().c_str(),
			//		outPath.string().c_str());
			//	if (!rm.CookAndSave(inPath, outPath))
			//	{
			//		ALICE_LOG_ERRORF("BuildGame: CookAndSave failed. in=\"%s\" out=\"%s\"",
			//			inPath.string().c_str(), outPath.string().c_str());
			//		return false;
			//	}
			//}

            // 빌드할때 Cook으로 변환할때 쓸 멀티쓰레드 잡임
            // 모든 작업을 벡터에 수집
            struct Job
            {
                fs::path inPath;
                fs::path outPath;
                bool alreadyEncrypted;
            };
            std::vector<Job> jobs;

            for (fs::recursive_directory_iterator it(srcRoot, ec), end; it != end; it.increment(ec))
            {
                if (ec) { ec.clear(); continue; }
                if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

                const fs::path inPath = it->path();
                fs::path rel = fs::relative(inPath, srcRoot, ec);
                if (ec) { ec.clear(); continue; }

                const std::string relStr = rel.generic_string();
                if (!excludePrefixRel.empty() && relStr.rfind(excludePrefixRel, 0) == 0)
                    continue;

                fs::path outPath = dstCookedRoot / rel;
                outPath.replace_extension(".alice");
                const std::string ext = inPath.extension().string();
                const bool alreadyEncrypted = (_stricmp(ext.c_str(), ".alice") == 0);

                jobs.push_back({ inPath, outPath, alreadyEncrypted });
            }

            if (jobs.empty()) return true;

            // 2단계: 멀티스레드 병렬 처리
            std::atomic<size_t> nextIdx = 0;
            std::atomic<bool> success = true;
            std::mutex logMutex;

            const size_t numThreads = std::max(1u, std::thread::hardware_concurrency());
            std::vector<std::thread> workers;

            struct WorkerCtx
            {
                std::vector<Job>* jobs{};
                std::atomic<size_t>* nextIdx{};
                std::atomic<bool>* success{};
                std::mutex* logMutex{};
            };

            struct WorkerProc
            {
                static void Run(WorkerCtx ctx)
                {
                    Alice::ResourceManager rm;
                    while (true)
                    {
                        const size_t idx = ctx.nextIdx->fetch_add(1);
                        if (idx >= ctx.jobs->size())
                            break;

                        const Job& job = (*ctx.jobs)[idx];
                        bool ok = false;

                        if (job.alreadyEncrypted)
                        {
                            ok = CopyFileOver(job.inPath, job.outPath);
                        }
                        else
                        {
                            {
                                std::lock_guard<std::mutex> lock(*ctx.logMutex);
                                ALICE_LOG_INFO("CookFile: in=\"%s\" -> out=\"%s\"",
                                               job.inPath.string().c_str(),
                                               job.outPath.string().c_str());
                            }
                            ok = rm.CookAndSave(job.inPath, job.outPath);
                            if (!ok)
                            {
                                std::lock_guard<std::mutex> lock(*ctx.logMutex);
                                ALICE_LOG_ERRORF("BuildGame: CookAndSave failed. in=\"%s\" out=\"%s\"",
                                                 job.inPath.string().c_str(), job.outPath.string().c_str());
                            }
                        }

                        if (!ok)
                            ctx.success->store(false);
                    }
                }
            };

            const WorkerCtx ctx{ &jobs, &nextIdx, &success, &logMutex };

            for (size_t i = 0; i < numThreads; ++i)
                workers.emplace_back(&WorkerProc::Run, ctx);

            for (auto& t : workers)
                t.join();

            return success.load();
        }

        void CopyAllDlls(const std::filesystem::path& fromDir, const std::filesystem::path& toDir)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(fromDir, ec) || ec) return;
            fs::create_directories(toDir, ec);
            ec.clear();

            // 1단계: 모든 DLL 파일 경로 수집
            std::vector<fs::path> dllFiles;
            for (fs::directory_iterator it(fromDir, ec), end; it != end; it.increment(ec))
            {
                if (ec) { ec.clear(); continue; }
                if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
                const fs::path p = it->path();
                if (p.extension() == ".dll")
                {
                    // 이 부분에서 dllFiles을 푸시해서 멀티스레드 도는  건데, 
                    // 만약 오류가 생긴다면 바로 CopyFileOver로 여기서 싱글스레드로 할것.
                    //CopyFileOver(p, toDir / p.filename());
                    dllFiles.push_back(p);
                }
            }

            if (dllFiles.empty()) return;

            // 2단계: 멀티스레드 병렬 복사
            std::atomic<size_t> nextIdx = 0;
            const size_t numThreads = std::max(1u, std::thread::hardware_concurrency());
            std::vector<std::thread> workers;

            struct CopyCtx
            {
                std::vector<fs::path>* dllFiles{};
                std::atomic<size_t>* nextIdx{};
                fs::path toDir{};
            };

            struct CopyProc
            {
                static void Run(CopyCtx ctx)
                {
                    while (true)
                    {
                        const size_t idx = ctx.nextIdx->fetch_add(1);
                        if (idx >= ctx.dllFiles->size())
                            break;
                        CopyFileOver((*ctx.dllFiles)[idx], ctx.toDir / (*ctx.dllFiles)[idx].filename());
                    }
                }
            };

            const CopyCtx ctx{ &dllFiles, &nextIdx, toDir };

            for (size_t i = 0; i < numThreads; ++i)
                workers.emplace_back(&CopyProc::Run, ctx);

            for (auto& t : workers)
                t.join();
        }

        struct BuildGameTaskArgs
        {
            std::filesystem::path projectRoot;
            std::filesystem::path cfgPath;
            std::string           exportPathStr;
        };

        struct BuildGameTask
        {
            static void Run(BuildGameTaskArgs args)
            {
                namespace fs2 = std::filesystem;

#ifdef _DEBUG
                const std::wstring cmd = L"cmake --build build --config Debug --target AlicePlayer";
                const fs2::path releaseBinDir = args.projectRoot / "build/bin/Debug";
#else
                const std::wstring cmd = L"cmake --build build --config Release --target AlicePlayer";
                const fs2::path releaseBinDir = args.projectRoot / "build/bin/Release";
#endif

                STARTUPINFOW        si{};
                PROCESS_INFORMATION pi{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;

                BOOL ok = CreateProcessW(
                    nullptr,
                    const_cast<wchar_t*>(cmd.c_str()),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    args.projectRoot.wstring().c_str(),
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

                ScopedHandle hProcess(pi.hProcess);
                ScopedHandle hThread(pi.hThread);

                float p = 0.0f;
                for (;;)
                {
                    DWORD wait = WaitForSingleObject(hProcess.h, 50);
                    if (wait == WAIT_TIMEOUT)
                    {
                        p += 0.005f;
                        if (p > 0.9f) p = 0.9f;
                        g_BuildProgress.store(p);
                        continue;
                    }
                    break;
                }

                DWORD exitCode = 0;
                GetExitCodeProcess(hProcess.h, &exitCode);
                ALICE_LOG_INFO("Build Game: CMake build finished with exitCode=%lu",
                               static_cast<unsigned long>(exitCode));

                if (exitCode != 0)
                {
                    g_BuildProgress.store(1.0f);
                    g_BuildExitCode.store(static_cast<long>(exitCode));
                    g_BuildInProgress.store(false);
                    return;
                }

                // (1) Metas: Assets를 청크로 패킹 (폴더구조 숨김, 256KB)
                const fs2::path stageMetas = releaseBinDir / "Metas";
                if (!MakeCleanDir(stageMetas))
                {
                    g_BuildExitCode.store(2);
                    g_BuildInProgress.store(false);
                    return;
                }
                {
                    Alice::ResourceManager rm;
                    if (!rm.CookResourceToChunkStore(args.projectRoot / "Assets", stageMetas, 256 * 1024))
                    {
                        ALICE_LOG_ERRORF("Build Game: failed to cook Assets -> Metas/Chunks.");
                        g_BuildExitCode.store(3);
                        g_BuildInProgress.store(false);
                        return;
                    }
                }

                // (2) Cooked: 항상 새로 생성 + 전부 .alice 암호화
                const fs2::path stageCooked = releaseBinDir / "Cooked";
                if (!MakeCleanDir(stageCooked))
                {
                    g_BuildExitCode.store(4);
                    g_BuildInProgress.store(false);
                    return;
                }
                if (!CookAllIntoCookedRoot(args.projectRoot / "Cooked", stageCooked, "Resource/"))
                {
                    g_BuildExitCode.store(5);
                    g_BuildInProgress.store(false);
                    return;
                }

                // (3) Resource: 원본 폴더를 넣지 않고 Cooked/Chunks로 패킹
                {
                    Alice::ResourceManager rm;
                    if (!rm.CookResourceToChunkStore(args.projectRoot / "Resource", stageCooked))
                    {
                        ALICE_LOG_ERRORF("Build Game: failed to cook Resource -> Cooked/Chunks (stage).");
                        g_BuildExitCode.store(6);
                        g_BuildInProgress.store(false);
                        return;
                    }
                }

                // (4) BuildSettings 복사 (exe 옆)
                if (!CopyFileOver(args.cfgPath, releaseBinDir / "BuildSettings.json"))
                {
                    g_BuildExitCode.store(7);
                    g_BuildInProgress.store(false);
                    return;
                }

                // (5) Export: Bin 아래로 정리 (exe/dll/buildsettings/cooked/metas)
                fs2::path exportRoot = args.exportPathStr;
                if (!exportRoot.is_absolute())
                    exportRoot = args.projectRoot / exportRoot;

                const fs2::path exportBin = exportRoot / "Bin";
                if (!MakeCleanDir(exportBin))
                {
                    g_BuildExitCode.store(8);
                    g_BuildInProgress.store(false);
                    return;
                }

                if (!CopyFileOver(releaseBinDir / "AlicePlayer.exe", exportBin / "AlicePlayer.exe"))
                {
                    g_BuildExitCode.store(9);
                    g_BuildInProgress.store(false);
                    return;
                }

                CopyAllDlls(releaseBinDir, exportBin);

                if (!CopyDirTree(releaseBinDir / "Cooked", exportBin / "Cooked") ||
                    !CopyDirTree(releaseBinDir / "Metas", exportBin / "Metas"))
                {
                    g_BuildExitCode.store(10);
                    g_BuildInProgress.store(false);
                    return;
                }

                if (!CopyFileOver(releaseBinDir / "BuildSettings.json", exportBin / "BuildSettings.json"))
                {
                    g_BuildExitCode.store(11);
                    g_BuildInProgress.store(false);
                    return;
                }

                ALICE_LOG_INFO("Build Game: exported to \"%s\" (run: Bin/AlicePlayer.exe)",
                               exportRoot.string().c_str());

                g_BuildProgress.store(1.0f);
                g_BuildExitCode.store(static_cast<long>(exitCode));
                g_BuildInProgress.store(false);
            }
        };
    }

    namespace
    {
        // 현재 씬이 수정되었는지 여부 (저장 필요 여부)
        bool                     g_SceneDirty           = false;
        bool                     g_HasCurrentScenePath  = false;
        std::filesystem::path    g_CurrentScenePath;

        // 간단한 게임 빌드 UI 상태
        bool                     g_ShowBuildGameWindow  = false;
        bool                     g_ShowPvdSettingsWindow = false;

        // Build Game 진행 상황 (간단한 멀티스레드 + atomic 사용)
        std::atomic<bool>        g_BuildInProgress { false };
        std::atomic<float>       g_BuildProgress   { 0.0f };   // 0.0 ~ 1.0
        std::atomic<long>        g_BuildExitCode   { -1 };     // -1: 아직 없음

        // 다른 씬을 로드하기 위해 대기 중인 경로
        bool                     g_RequestSceneLoad     = false;
        std::filesystem::path    g_NextScenePath;
        bool                     g_ShowSceneLoadError   = false;
        std::string              g_SceneLoadErrorMsg;

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
        const std::wstring fontKr =
            (m_resources ? m_resources->Resolve("Resource/Fonts/NotoSansKR-Regular.ttf").wstring()
                         : std::wstring(L"Resource/Fonts/NotoSansKR-Regular.ttf"));
        io.FontDefault = io.Fonts->AddFontFromFileTTF(
            Utf8FromWString(fontKr).c_str(),
            18.0f,
            &baseConfig,
            io.Fonts->GetGlyphRangesKorean());

        ImFontConfig jpConfig{};
        jpConfig.MergeMode = true;
        jpConfig.PixelSnapH = true;
        const std::wstring fontJp =
            (m_resources ? m_resources->Resolve("Resource/Fonts/meiryo.ttc").wstring()
                         : std::wstring(L"Resource/Fonts/meiryo.ttc"));
        io.Fonts->AddFontFromFileTTF(
            Utf8FromWString(fontJp).c_str(),
            18.0f,
            &jpConfig,
            io.Fonts->GetGlyphRangesJapanese());

        m_hwnd         = hwnd;
        m_renderDevice = &renderDevice;

        auto* d3dDevice  = renderDevice.GetDevice();
        auto* d3dContext = renderDevice.GetImmediateContext();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(d3dDevice, d3dContext);

        // ImGuizmo 스타일 설정
        ImGuizmo::Style& style = ImGuizmo::GetStyle();
        style.RotationLineThickness = 3.0f;
        style.RotationOuterLineThickness = 2.0f;

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
        ImGuizmo::BeginFrame();
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
                                  int& pvdPort)
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
                if (ImGui::MenuItem("Empty"))
                {
                    EntityId e = world.CreateEmpty();
                    selectedEntity = e;
                    g_SceneDirty   = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Cube"))
                {
                    EntityId e = world.CreateCube();
                    selectedEntity = e;
                    g_SceneDirty   = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Camera"))
                {
                    EntityId e = world.CreateCamera();
                    selectedEntity = e;
                    g_SceneDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Point Light"))
                {
                    EntityId e = world.CreatePointLight();
                    selectedEntity = e;
                    g_SceneDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Spot Light"))
                {
                    EntityId e = world.CreateSpotLight();
                    selectedEntity = e;
                    g_SceneDirty = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Rect Light"))
                {
                    EntityId e = world.CreateRectLight();
                    selectedEntity = e;
                    g_SceneDirty = true;
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

						wchar_t exePathW[MAX_PATH] = {};
						GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
						std::filesystem::path exePath = exePathW;
						std::filesystem::path exeDir = exePath.parent_path();
                        std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트

                        fbxPath = std::filesystem::relative(fbxPath, projectRoot);

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
							TransformComponent& t = world.AddComponent<TransformComponent>(e);
                            t.position = { 0.0f, 0.0f, 0.0f };
                            t.scale    = { 1.0f, 1.0f, 1.0f };
                            t.rotation = { 0.0f, 0.0f, 0.0f };

                            // 스키닝 메시 컴포넌트 등록
							SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, result.meshAssetPath);
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
                            // 원래 있는 경우 없는 경우 나눠서 있는 경우는 서브 메테리얼을 만들어야 하는데, 일단은 둘다 생기도록 함.
                            // TODO : 여기서 서브 메테리얼을 각각 다르게 설정할 수 있게 해야함 
                            if (!result.materialAssetPaths.empty())
                            {
                                DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                                MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
                                mat.assetPath = result.materialAssetPaths.front();
                                MaterialFile::Load(mat.assetPath, mat, m_resources);
                            }
                            else
                            {
								//DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
								//MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
								//mat.assetPath = "fbx has no material. default material";
								//MaterialFile::Load(mat.assetPath, mat);
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
            // PVD 설정 버튼
            if (ImGui::Button("PVD Settings"))
            {
                g_ShowPvdSettingsWindow = true;
            }

            ImGui::Separator();
            ImGui::Text("DeltaTime: %.3f  FPS: %.1f", deltaTime, fps);

            ImGui::Separator();
            // 렌더링 시스템 선택 체크박스
            ImGui::Checkbox("Forward Rendering", &useForwardRendering);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("체크: Forward Rendering\n해제: Deferred Rendering");
            }

            ImGui::EndMainMenuBar();
        }

        // === PVD Settings 창 ===
        if (g_ShowPvdSettingsWindow)
        {
            static bool s_wasOpen = false;
            bool isOpen = g_ShowPvdSettingsWindow;
            
            if (ImGui::Begin("PVD Settings", &g_ShowPvdSettingsWindow))
            {
                ImGui::Text("PhysX Visual Debugger Settings");
                ImGui::Separator();
                
                // PVD 활성화 체크박스
                ImGui::Checkbox("Enable PVD", &pvdEnabled);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Enable PhysX Visual Debugger.\n"
                                     "Note: Requires restart to apply changes.\n"
                                     "Make sure PVD is running on the target host/port.");
                }

                // PVD 설정 (비활성화 상태에서도 표시)
                ImGui::BeginDisabled(!pvdEnabled);
                
                // PVD Host 입력
                static char pvdHostBuf[256] = {};
                static bool s_hostBufInitialized = false;
                if (!s_hostBufInitialized || !s_wasOpen)
                {
                    strncpy_s(pvdHostBuf, pvdHost.c_str(), 255);
                    pvdHostBuf[255] = '\0';
                    s_hostBufInitialized = true;
                }
                ImGui::Text("Host:");
                ImGui::SameLine();
                if (ImGui::InputText("##PvdHost", pvdHostBuf, sizeof(pvdHostBuf)))
                {
                    pvdHost = pvdHostBuf;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("PVD server host (default: 127.0.0.1)");
                }

                // PVD Port 입력
                ImGui::Text("Port:");
                ImGui::SameLine();
                if (ImGui::InputInt("##PvdPort", &pvdPort))
                {
                    if (pvdPort < 1) pvdPort = 1;
                    if (pvdPort > 65535) pvdPort = 65535;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("PVD server port (default: 5425)");
                }

                ImGui::EndDisabled();

                ImGui::Separator();
                
                // 상태 표시
                ImGui::Text("Status:");
                ImGui::SameLine();
                if (pvdEnabled)
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Enabled");
                    ImGui::Text("PVD will be enabled on next restart.");
                    ImGui::Text("Connection: %s:%d", pvdHost.c_str(), pvdPort);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Disabled");
                }

                ImGui::Separator();
                ImGui::TextWrapped("Note: PVD settings are saved automatically when the engine shuts down.\n"
                                  "Restart the engine to apply changes.");
            }
            
            // 창이 닫힐 때 설정 저장 (이전에 열려있었고 지금 닫힌 경우)
            if (s_wasOpen && !g_ShowPvdSettingsWindow)
            {
                // 엔진 종료 시 자동 저장되므로 여기서는 선택적
                // 필요시 여기서도 저장 가능
            }
            s_wasOpen = isOpen;
            
            ImGui::End();
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
                ImGui::InputInt("Width (min : 320)",  &s_Width);
                ImGui::InputInt("Height (min : 240)", &s_Height);
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

                    const fs::path assetsRoot =
                        (m_resources ? m_resources->Resolve("Assets")
                                     : fs::path("Assets"));
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
                        // 1) 빌드 설정 파일 저장 (JSON)
                        wchar_t exePathW[MAX_PATH] = {};
                        GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
                        fs::path exePath = exePathW;
                        fs::path exeDir  = exePath.parent_path();
                        fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트

                        fs::path buildDir = projectRoot / "Build";
                        std::error_code fec;
                        fs::create_directories(buildDir, fec);

                        fs::path cfgPath = buildDir / "BuildSettings.json";
                        {
                            std::ofstream ofs(cfgPath);
                            if (ofs.is_open())
                            {
                                nlohmann::json j;
                                j["width"] = s_Width;
                                j["height"] = s_Height;

                                std::vector<fs::path> includedScenes;
                                includedScenes.reserve(s_ScenePaths.size());
                                for (std::size_t i = 0; i < s_ScenePaths.size(); ++i)
                                {
                                    if (i >= s_SceneSelected.size()) continue;
                                    if (!s_SceneSelected[i]) continue;

									fs::path relScene = fs::relative(s_ScenePaths[i], projectRoot);  // 프로젝트 루트 기준으로 상대 경로 (예: "Assets/Stage1/Stage1.scene")
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
                                    j["default"] = defaultScenePath.string();
                                }

                                std::vector<std::string> sceneStrings;
                                sceneStrings.reserve(includedScenes.size());
                                for (const auto& p : includedScenes)
                                    sceneStrings.push_back(p.string());
                                j["scenes"] = sceneStrings;

                                ofs << j.dump(4);
                            }
                        }

                        ALICE_LOG_INFO("BuildSettings saved to \"%s\"", cfgPath.string().c_str());

                        // 2) 별도 스레드에서 CMake 빌드 + 리소스 복사 실행
                        g_BuildInProgress.store(true);
                        g_BuildProgress.store(0.0f);
                        g_BuildExitCode.store(-1);

                        // Export 경로 문자열은 스레드 시작 시점에 복사해 둡니다.
                        std::string exportPathStr = s_ExportPath;

                        const BuildGameTaskArgs args{ projectRoot, cfgPath, exportPathStr };
                        std::thread(&BuildGameTask::Run, args).detach();
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

            const auto& transforms = world.GetComponents<TransformComponent>();
            if (transforms.empty())
            {
                Alice::ImGuiText(L"생성된 엔티티가 없습니다.");
            }
            else
            {
                EntityId entityToDelete = InvalidEntityId;
                static EntityId s_renameTarget = InvalidEntityId;
                static char s_renameBuf[128]{};
                bool openRenamePopup = false;

                for (const auto& [entityId, transform] : transforms)
                {
                    (void)transform;
                    const bool isSelected = (selectedEntity == entityId);
                    const std::string name = world.GetEntityName(entityId);
                    const std::string label = !name.empty()
                        ? name
                        : ("Entity " + std::to_string(static_cast<std::uint32_t>(entityId)));

                    ImGui::PushID((int)entityId);
                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        selectedEntity = entityId;
                    }

                    // 항목 우클릭 시 컨텍스트 메뉴 표시
                    if (ImGui::BeginPopupContextItem())
                    {
                        if (ImGui::MenuItem("Change Name"))
                        {
                            s_renameTarget = entityId;
                            openRenamePopup = true;

                            const std::string cur = world.GetEntityName(entityId);
                            const std::string init = cur.empty()
                                ? ("Entity " + std::to_string((std::uint32_t)entityId))
                                : cur;
                            std::memset(s_renameBuf, 0, sizeof(s_renameBuf));
                            strncpy_s(s_renameBuf, init.c_str(), sizeof(s_renameBuf) - 1);
                        }

                        if (ImGui::MenuItem("Delete"))
                        {
                            entityToDelete = entityId;
                        }

                        // 현재 게임 오브젝트를 프리팹으로 저장하는 기능
                        if (ImGui::MenuItem("Save as Prefab"))
                        {
                            // Assets/Prefabs 폴더 아래에 간단한 이름으로 저장합니다.
                            namespace fs = std::filesystem;
                            const fs::path prefabDir =
                                (m_resources ? m_resources->Resolve("Assets/Prefabs")
                                             : fs::path("Assets/Prefabs"));
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
                    ImGui::PopID();
                }

                if (openRenamePopup)
                    ImGui::OpenPopup("Change Name");

                if (ImGui::BeginPopupModal("Change Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::InputText("Name", s_renameBuf, sizeof(s_renameBuf));
                    if (ImGui::Button("OK"))
                    {
                        world.SetEntityName(s_renameTarget, s_renameBuf);
                        g_SceneDirty = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
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
        if (ImGui::Begin("Inspector")) {
            if (selectedEntity == InvalidEntityId) {
                Alice::ImGuiText(L"선택된 엔티티가 없습니다.");
            }
            else {
                ImGui::Text("Entity %u", static_cast<uint32_t>(selectedEntity));
                ImGui::Separator();

                // 1. Transform
                DrawInspectorTransform(world, selectedEntity);
                ImGui::Separator();

                // 2. Scripts
                ImGui::Text("Scripts");
                DrawInspectorScripts(world, selectedEntity);
                ImGui::Separator();

                // 3. Material
                DrawInspectorMaterial(world, selectedEntity);
                ImGui::Separator();

                // 3-2. Lights
                DrawInspectorPointLight(world, selectedEntity);
                DrawInspectorSpotLight(world, selectedEntity);
                DrawInspectorRectLight(world, selectedEntity);
                ImGui::Separator();

                // 4. Skinned Mesh (Condensed)
                if (auto* skinned =
                    world.GetComponent<SkinnedMeshComponent>(selectedEntity)) {
                    ImGui::Separator();
                    ImGui::Text("Skinned Mesh: %s", skinned->meshAssetPath.c_str());
                    // Details omitted for brevity
                }
            }
        }
        ImGui::End();

        // === Project ===
        if (ImGui::Begin("Project"))
        {
            Alice::ImGuiText(L"Assets 폴더");
            ImGui::Separator();

            // Assets 폴더는 논리 경로로만 다루고, 실제 위치는 ResourceManager 가 해석합니다.
            const std::filesystem::path assetsRoot =
                (m_resources ? m_resources->Resolve("Assets")
                             : std::filesystem::path("Assets"));
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
            // Gizmo 컨트롤 UI (static 변수로 상태 유지)
            static ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
            static ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD; // 기본값: WORLD 모드
            static bool gizmoSnap = false;
            static XMFLOAT3 snapTranslation = XMFLOAT3(1.0f, 1.0f, 1.0f);
            static float snapRotation = 15.0f; // degrees
            static float snapScale = 1.0f;

            // 키보드 단축키로 Gizmo 모드 변경 (InputSystem 사용)
            if (m_inputSystem)
            {
                using namespace DirectX;
                if (m_inputSystem->IsKeyPressed(Keyboard::Keys::W)) gizmoOp = ImGuizmo::TRANSLATE;
                if (m_inputSystem->IsKeyPressed(Keyboard::Keys::E)) gizmoOp = ImGuizmo::ROTATE;
                if (m_inputSystem->IsKeyPressed(Keyboard::Keys::R)) gizmoOp = ImGuizmo::SCALE;
                if (m_inputSystem->IsKeyPressed(Keyboard::Keys::X))
                {
                    gizmoMode = (gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                }
            }

            // Gizmo Operation 선택 버튼
            if (ImGui::RadioButton("Translate (W)", gizmoOp == ImGuizmo::TRANSLATE))
                gizmoOp = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate (E)", gizmoOp == ImGuizmo::ROTATE))
                gizmoOp = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale (R)", gizmoOp == ImGuizmo::SCALE))
                gizmoOp = ImGuizmo::SCALE;

            // Gizmo Mode 선택 (Scale 모드에서는 World만 지원)
            if (gizmoOp != ImGuizmo::SCALE)
            {
                ImGui::SameLine();
                if (ImGui::RadioButton("Local (X)", gizmoMode == ImGuizmo::LOCAL))
                    gizmoMode = ImGuizmo::LOCAL;
                ImGui::SameLine();
                if (ImGui::RadioButton("World (X)", gizmoMode == ImGuizmo::WORLD))
                    gizmoMode = ImGuizmo::WORLD;
            }
            else
            {
                gizmoMode = ImGuizmo::LOCAL; // Scale은 항상 Local
            }

            // Snap 토글
            ImGui::SameLine();
            if (ImGui::Checkbox("Snap (Ctrl)", &gizmoSnap))
            {
                // Snap 체크박스 클릭 시 토글
            }

            // Snap 값 설정 (접을 수 있는 섹션)
            if (gizmoSnap)
            {
                ImGui::Indent();
                switch (gizmoOp)
                {
                case ImGuizmo::TRANSLATE:
                    ImGui::DragFloat3("Snap Translation", &snapTranslation.x, 0.1f, 0.01f, 100.0f);
                    break;
                case ImGuizmo::ROTATE:
                    ImGui::DragFloat("Snap Rotation (deg)", &snapRotation, 1.0f, 1.0f, 90.0f);
                    break;
                case ImGuizmo::SCALE:
                    ImGui::DragFloat("Snap Scale", &snapScale, 0.1f, 0.1f, 10.0f);
                    break;
                default:
                    break;
                }
                ImGui::Unindent();
            }

            ImGui::Separator();
            Alice::ImGuiText(L"게임 상태");
            ImGui::Separator();
            ImGui::Text("Play State : %s", isPlaying ? "Playing" : "Stopped");

            // 에디터 뷰포트는 톤매핑 완료(LDR) 텍스처를 표시해야 정상 색감이 나옵니다.
            ID3D11ShaderResourceView* sceneSRV = nullptr;
            float sceneWidth = 0.0f;
            float sceneHeight = 0.0f;

            if (useForwardRendering)
            {
                sceneSRV = forward.GetViewportSRV();
                sceneWidth  = static_cast<float>(forward.GetSceneWidth());
                sceneHeight = static_cast<float>(forward.GetSceneHeight());
            }
            else
            {
                sceneSRV = deferred.GetViewportSRV();
                sceneWidth  = static_cast<float>(deferred.GetSceneWidth());
                sceneHeight = static_cast<float>(deferred.GetSceneHeight());
            }

            if (sceneSRV)
            {
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

                // Image를 그린다
                ImGui::Image(sceneSRV, size);

                // 이미지가 화면에 그려진 사각형(픽셀) - Image 호출 직후에만 유효
                ImVec2 imgMin  = ImGui::GetItemRectMin();
                ImVec2 imgMax  = ImGui::GetItemRectMax();
                ImVec2 imgSize = ImGui::GetItemRectSize();

                // ImGuizmo를 사용하여 선택된 엔티티 조작 (재생 중이 아닐 때만)
                if (!isPlaying && selectedEntity != InvalidEntityId)
                {
                    if (TransformComponent* transform = world.GetComponent<TransformComponent>(selectedEntity))
                    {
                        // View/Proj 행렬 준비 (XMFLOAT4X4로 변환)
                        XMMATRIX viewXM = camera.GetViewMatrix();
                        XMMATRIX projXM = camera.GetProjectionMatrix();
                        
                        XMFLOAT4X4 viewMatrix, projMatrix;
                        XMStoreFloat4x4(&viewMatrix, viewXM);
                        XMStoreFloat4x4(&projMatrix, projXM);

                        // [핵심 수정] ImGuizmo의 RecomposeMatrixFromComponents를 사용하여 행렬 생성
                        // 이렇게 하면 DecomposeMatrixToComponents와 알고리즘이 일치하여 떨림이 사라집니다
                        // ImGuizmo는 Degree(도) 단위를 사용하므로 변환 필요
                        float matrixTranslation[3], matrixRotation[3], matrixScale[3];

                        matrixTranslation[0] = transform->position.x;
                        matrixTranslation[1] = transform->position.y;
                        matrixTranslation[2] = transform->position.z;

                        // Radian을 Degree로 변환
                        matrixRotation[1] = XMConvertToDegrees(transform->rotation.x);
                        matrixRotation[0] = XMConvertToDegrees(transform->rotation.y);
                        matrixRotation[2] = XMConvertToDegrees(transform->rotation.z);

                        matrixScale[0] = transform->scale.x;
                        matrixScale[1] = transform->scale.y;
                        matrixScale[2] = transform->scale.z;

                        // ImGuizmo 방식으로 행렬을 재조립 (Recompose)
                        // 이렇게 하면 나중에 Decompose할 때의 알고리즘과 대칭이 되어 떨림이 사라집니다
                        float worldMatrix[16];
                        ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, worldMatrix);

                        // ImGuizmo에 직접 포인터 전달
                        const float* viewMat = reinterpret_cast<const float*>(viewMatrix.m);
                        const float* projMat = reinterpret_cast<const float*>(projMatrix.m);
                        float* objMat = worldMatrix;

                        // ImGuizmo 설정
                        ImGuizmo::SetOrthographic(false);
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        ImGuizmo::SetDrawlist(drawList);
                        // SetRect는 실제 이미지가 그려진 사각형(픽셀)을 사용
                        // sceneWidth/sceneHeight는 GPU 렌더 타겟 해상도이므로 화면 픽셀과 다를 수 있음
                        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

                        // Snap 값 준비
                        float* snap = nullptr;
                        float snapValue[3] = { 0, 0, 0 }; // Snap 값을 받을 임시 배열
                        bool forceSnap = false;
                        if (m_inputSystem)
                        {
                            using namespace DirectX;
                            forceSnap = m_inputSystem->IsKeyDown(Keyboard::Keys::LeftControl) || 
                                       m_inputSystem->IsKeyDown(Keyboard::Keys::RightControl);
                        }
                        if (gizmoSnap || forceSnap)
                        {
                            if (gizmoOp == ImGuizmo::TRANSLATE)
                            {
                                snapValue[0] = snapValue[1] = snapValue[2] = snapTranslation.x;
                                snap = snapValue;
                            }
                            else if (gizmoOp == ImGuizmo::ROTATE)
                            {
                                snapValue[0] = snapRotation;
                                snap = snapValue;
                            }
                            else if (gizmoOp == ImGuizmo::SCALE)
                            {
                                snapValue[0] = snapScale;
                                snap = snapValue;
                            }
                        }

                        // Gizmo 조작 (worldMatrix 배열을 직접 넘겨주어 수정되게 함)
                        bool manipulated = ImGuizmo::Manipulate(viewMat, projMat, gizmoOp, gizmoMode, objMat, nullptr, snap);

                        if (manipulated)
                        {
                            // [핵심 수정] ImGuizmo로 조립했으므로 분해(Decompose)도 안정적으로 동작함
                            // Recompose와 Decompose의 알고리즘이 일치하여 떨림이 사라집니다
                            ImGuizmo::DecomposeMatrixToComponents(worldMatrix, matrixTranslation, matrixRotation, matrixScale);

                            // Transform 컴포넌트 업데이트 (다시 Radian으로 변환하여 저장)
                            transform->position = XMFLOAT3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]);
                            
                            transform->rotation = XMFLOAT3(
                                XMConvertToRadians(matrixRotation[1]),  // pitch (x)
                                XMConvertToRadians(matrixRotation[0]),  // yaw (y)
                                XMConvertToRadians(matrixRotation[2])   // roll (z)
                            );
                            
                            transform->scale = XMFLOAT3(matrixScale[0], matrixScale[1], matrixScale[2]);
                            
                            // ImGuizmo로 Transform이 변경되었고 물리 컴포넌트가 있으면 텔레포트 자동 활성화
                            if (auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(selectedEntity))
                            {
                                rigidBody->teleport = true;
                            }
                            if (auto* cct = world.GetComponent<Phy_CCTComponent>(selectedEntity))
                            {
                                cct->teleport = true;
                            }
                            g_SceneDirty = true;
                        }
                    }
                }

                // 엔티티 선택 (Gizmo 위에 있지 않을 때만)
                // 최종 빌드(Release)에서는 뷰포트 피커가 작동하지 않도록 함
#ifdef _DEBUG
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    // Gizmo 위에 있지 않고 사용 중이 아닐 때만 선택 처리
                    if (!ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
                    {
                        const ImVec2 mousePos = ImGui::GetIO().MousePos;

                        //피킹은 실제 이미지 사각형(imgMin, imgSize)을 기준으로 계산
                        // imagePos나 size를 사용하면 레터박스/패딩 때문에 위치가 어긋남
                        const float localX = mousePos.x - imgMin.x;
                        const float localY = mousePos.y - imgMin.y;

                        if (localX >= 0.0f && localX <= imgSize.x &&
                            localY >= 0.0f && localY <= imgSize.y)
                        {
                            // UV 좌표를 실제 이미지 크기 기준으로 계산
                            const float u = (imgSize.x > 0.0f) ? (localX / imgSize.x) : 0.0f;
                            const float v = (imgSize.y > 0.0f) ? (localY / imgSize.y) : 0.0f;
                            EntityId hit = picker.Pick(world, camera, m_skinnedRegistry, u, v);
                            selectedEntity = hit;
                        }
                    }
                }
#endif // _DEBUG
            }
            else
            {
                Alice::ImGuiText("씬 텍스처가 아직 준비되지 않았습니다.");
            }
        }
        ImGui::End();

        // === Camera / Animation (같은 영역, 탭) ===
        if (ImGui::Begin("Camera"))
        {
            if (ImGui::BeginTabBar("##CameraTabs"))
            {
                if (ImGui::BeginTabItem("Camera"))
                {
                    Alice::ImGuiText(L"카메라 정보");
                    ImGui::Separator();

                    XMFLOAT3 camPos = camera.GetPosition();
                    ImGui::Text("Position : (%.2f, %.2f, %.2f)",
                                camPos.x, camPos.y, camPos.z);

                    ImGui::Separator();
                    Alice::ImGuiText(L"카메라 설정");

                    float fovDeg = XMConvertToDegrees(camera.GetFovYRadians());
                    float nearPlane = camera.GetNearPlane();
                    float farPlane  = camera.GetFarPlane();

                    bool changed = false;
                    changed |= ImGui::SliderFloat("FOV (deg)", &fovDeg, 20.0f, 120.0f);
                    changed |= ImGui::DragFloat("Near Plane",  &nearPlane, 0.01f, 0.01f, 10.0f, "%.3f");
                    changed |= ImGui::DragFloat("Far Plane",   &farPlane,  1.0f,  10.0f, 5000.0f, "%.1f");
                    ImGui::SliderFloat("Move Speed", &cameraMoveSpeed, 0.1f, 50.0f, "%.2f");

                    if (changed)
                    {
                        float fovRad  = XMConvertToRadians(fovDeg);
                        float aspect  = camera.GetAspectRatio();
                        nearPlane = (std::max)(nearPlane, 0.01f);
                        farPlane  = (std::max)(farPlane,  nearPlane + 0.1f);
                        camera.SetPerspective(fovRad, aspect, nearPlane, farPlane);
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Animation"))
                {
                    if (selectedEntity == InvalidEntityId)
                    {
                        ImGui::TextUnformatted("No entity selected.");
                    }
                    else
                    {
                        SkinnedMeshComponent* skinned = world.GetComponent<SkinnedMeshComponent>(selectedEntity);
                        if (!skinned || skinned->meshAssetPath.empty())
                        {
                            ImGui::TextUnformatted("Selected entity has no SkinnedMesh.");
                        }
                        else
                        {
                            std::shared_ptr<SkinnedMeshGPU> mesh;
                            if (m_skinnedRegistry)
                                mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);

                            ImGui::Text("Entity: %u", (unsigned)selectedEntity);
                            ImGui::Text("Mesh : %s", skinned->meshAssetPath.c_str());

                            if (!mesh || !mesh->sourceModel)
                            {
                                ImGui::Separator();
                                ImGui::TextUnformatted("Animation data is not ready (re-import FBX once).");
                                if (ImGui::Button("Re-import Skinned Meshes"))
                                    EnsureSkinnedMeshesRegistered(world);
                            }
                            else
                            {
                                const auto& names = mesh->sourceModel->GetAnimationNames();
                                if (names.empty())
                                {
                                    ImGui::TextUnformatted("This mesh has no animations.");
                                }
                                else
                                {
                                    auto* anim = world.GetComponent<SkinnedAnimationComponent>(selectedEntity);
                                    if (!anim) anim = &world.AddComponent<SkinnedAnimationComponent>(selectedEntity);

                                    ImGui::Separator();
                                    ImGui::Checkbox("Playing", &anim->playing);
                                    ImGui::SliderFloat("Speed", &anim->speed, 0.0f, 3.0f, "%.2f");

                                    int clip = anim->clipIndex;
                                    if (clip < 0) clip = 0;
                                    if (clip >= (int)names.size()) clip = (int)names.size() - 1;

                                    if (ImGui::BeginCombo("Clip", names[(size_t)clip].c_str()))
                                    {
                                        for (int i = 0; i < (int)names.size(); ++i)
                                        {
                                            const bool sel = (i == clip);
                                            if (ImGui::Selectable(names[(size_t)i].c_str(), sel))
                                            {
                                                clip = i;
                                                anim->clipIndex = i;
                                                anim->timeSec = 0.0;
                                            }
                                            if (sel) ImGui::SetItemDefaultFocus();
                                        }
                                        ImGui::EndCombo();
                                    }
                                    anim->clipIndex = clip;

                                    const double dur = mesh->sourceModel->GetClipDurationSec(anim->clipIndex);
                                    float timeSec = (float)anim->timeSec;
                                    float durF = (dur > 0.0) ? (float)dur : 0.0f;

                                    ImGui::BeginDisabled(durF <= 0.0f);
                                    if (ImGui::SliderFloat("Time (sec)", &timeSec, 0.0f, durF, "%.3f"))
                                        anim->timeSec = (double)timeSec;
                                    ImGui::EndDisabled();

                                    if (ImGui::Button("Stop"))
                                    {
                                        anim->playing = false;
                                        anim->timeSec = 0.0;
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("<<"))
                                    {
                                        anim->playing = false;
                                        anim->timeSec = (std::max)(0.0, anim->timeSec - 0.1);
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button(">>"))
                                    {
                                        anim->playing = false;
                                        anim->timeSec = anim->timeSec + 0.1;
                                    }
                                }
                            }
                        }
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
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
            ImGui::SameLine();
            if (ImGui::RadioButton("ToonPBR", mode == 5))   mode = 5;
            shadingMode = mode;

            Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

            // Forward/Deferred 모드에 따라 조명 파라미터를 각 렌더러에 반영합니다.
			//auto& lighting = useForwardRendering ? forward.GetLightingParameters() : deferred.GetLightingParameters();
			//auto& lighting = forward.GetLightingParameters();
			auto& lighting = deferred.GetLightingParameters();
            
            // PBR 모드일 때 PBR 파라미터 표시
            if (mode == 4 || mode == 5)
            {
                ImGui::Separator();
                ImGui::Text("PBR Material Parameters");
                ImGui::ColorEdit3("Base Color", &lighting.baseColor.x);
                ImGui::SliderFloat("Metalness", &lighting.metalness, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &lighting.roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Ambient Occlusion", &lighting.ambientOcclusion, 0.0f, 1.0f);
                ImGui::Separator();
            }
            else
            {
                // 레거시 쉐이더 파라미터
                ImGui::SliderFloat("Shininess", &lighting.shininess, 2.0f, 128.0f);
                ImGui::ColorEdit3("Diffuse Color", &lighting.diffuseColor.x);
                ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);
            }

            // 공통 조명 파라미터
            Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
                                    &lighting.keyIntensity,
                                    0.0f,
                                    3.0f);
            Alice::ImGuiSliderFloat(L"Fill Intensity (보조광)",
                                    &lighting.fillIntensity,
                                    0.0f,
                                    3.0f);

            Alice::ImGuiSliderFloat3(L"Key Direction (주광)",
                                     &lighting.keyDirection.x,
                                     -1.0f,
                                     1.0f);
            Alice::ImGuiSliderFloat3(L"Fill Direction (보조광)",
                                     &lighting.fillDirection.x,
                                     -1.0f,
                                     1.0f);

            // === Skybox 선택 ===
            ImGui::Separator();
            ImGui::Text("Skybox");
            
            // 스카이박스 선택 상태를 저장할 변수 (static으로 유지)
            static int skyboxChoice = 3; // 기본값: Baker (Sample) - 인덱스 3
            const char* skyboxItems[] = { "Off", "Bridge", "Indoor", "Baker" };
            
            if (useForwardRendering)
            {
                if (ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems)))
                {
                    // 스카이박스 변경
                    if (skyboxChoice == 0) // Off
                    {
                        // 스카이박스 비활성화
                        forward.SetSkyboxEnabled(false);
                    }
                    else
                    {
                        // 스카이박스 활성화 및 IBL 세트 로드
                        forward.SetSkyboxEnabled(true);
                        switch (skyboxChoice)
                        {
                        case 1: // Bridge
                            forward.SetIblSet("Bridge", "bridge");
                            break;
                        case 2: // Indoor
                            forward.SetIblSet("Indoor", "indoor");
                            break;
                        case 3: // Baker (Sample)
                            forward.SetIblSet("Sample", "BakerSample");
                            break;
                        default:
                            break;
                        }
                    }
                }
                // Off일 때만 배경색 편집
                if (skyboxChoice == 0)
                {
                    DirectX::XMFLOAT4 bgColor = forward.GetBackgroundColor();
                    if (ImGui::ColorEdit4("Background Color", &bgColor.x))
                    {
                        forward.SetBackgroundColor(bgColor);
                    }
                }
            }
            else
            {
                if (ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems)))
                {
                    // 스카이박스 변경
                    if (skyboxChoice == 0) // Off
                    {
                        // 스카이박스 비활성화
                        deferred.SetSkyboxEnabled(false);
                    }
                    else
                    {
                        // 스카이박스 활성화 및 IBL 세트 로드
                        deferred.SetSkyboxEnabled(true);
                        switch (skyboxChoice)
                        {
                        case 1: // Bridge
                            deferred.SetIblSet("Bridge", "bridge");
                            break;
                        case 2: // Indoor
                            deferred.SetIblSet("Indoor", "indoor");
                            break;
                        case 3: // Baker (Sample)
                            deferred.SetIblSet("Sample", "BakerSample");
                            break;
                        default:
                            break;
                        }
                    }
                }
                // Off일 때만 배경색 편집
                if (skyboxChoice == 0)
                {
                    DirectX::XMFLOAT4 bgColor = deferred.GetBackgroundColor();
                    if (ImGui::ColorEdit4("Background Color", &bgColor.x))
                    {
                        deferred.SetBackgroundColor(bgColor);
                    }
                }
            }
            
            // === Post-Process 파라미터 (Exposure, Max HDR Nits) ===
            ImGui::Separator();
            ImGui::Text("Post-Process");
            ImGui::Separator();
            
            float exposure = 0.0f;
            float maxHDRNits = 1000.0f;
            
            if (useForwardRendering)
            {
                forward.GetPostProcessParams(exposure, maxHDRNits);
                
                if (ImGui::SliderFloat("Exposure", &exposure, -3.0f, 3.0f, "%.2f"))
                {
                    forward.SetPostProcessParams(exposure, maxHDRNits);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Exposure 값: -3.0 (어두움) ~ 3.0 (밝음)\n0.0 = 1.0배 (기본값)");
                }
                
                if (ImGui::SliderFloat("Max HDR Nits", &maxHDRNits, 100.0f, 10000.0f, "%.0f nits"))
                {
                    forward.SetPostProcessParams(exposure, maxHDRNits);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("HDR 모니터 최대 밝기 (nits)\n일반 모니터: 100-300 nits\nHDR 모니터: 1000-10000 nits");
                }
            }
            else
            {
                deferred.GetPostProcessParams(exposure, maxHDRNits);
                
                if (ImGui::SliderFloat("Exposure", &exposure, -3.0f, 3.0f, "%.2f"))
                {
                    deferred.SetPostProcessParams(exposure, maxHDRNits);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Exposure 값: -3.0 (어두움) ~ 3.0 (밝음)\n0.0 = 1.0배 (기본값)");
                }
                
                if (ImGui::SliderFloat("Max HDR Nits", &maxHDRNits, 100.0f, 10000.0f, "%.0f nits"))
                {
                    deferred.SetPostProcessParams(exposure, maxHDRNits);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("HDR 모니터 최대 밝기 (nits)\n일반 모니터: 100-300 nits\nHDR 모니터: 1000-10000 nits");
                }

                // === Bloom 파라미터 ===
                ImGui::Separator();
                ImGui::Text("Bloom");
                ImGui::Separator();

                BloomSettings bloomSettings = deferred.GetBloomSettings();
                bool bloomChanged = false;

                // Bloom 활성화 체크박스
                if (ImGui::Checkbox("Enable Bloom", &bloomSettings.enabled))
                {
                    bloomChanged = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Bloom 효과 활성화/비활성화");
                }

                // Bloom ConstantBuffer 파라미터들 (enabled일 때만 표시)
                if (bloomSettings.enabled)
                {
                    // Intensity (합성 강도)
                    if (ImGui::SliderFloat("Intensity", &bloomSettings.intensity, 0.0f, 5.0f, "%.2f"))
                    {
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Bloom 합성 강도 (0.0 ~ 5.0)\n값이 클수록 더 밝게 합성됩니다");
                    }

                    // Threshold (밝기 추출 기준)
                    if (ImGui::SliderFloat("Threshold", &bloomSettings.threshold, 0.0f, 5.0f, "%.2f"))
                    {
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("밝기 추출 기준 (0.0 ~ 5.0)\n이 값보다 밝은 픽셀만 Bloom 효과가 적용됩니다");
                    }

                    // Knee (Soft threshold)
                    if (ImGui::SliderFloat("Knee", &bloomSettings.knee, 0.0f, 1.0f, "%.2f"))
                    {
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Soft threshold (0.0 ~ 1.0)\nBloom 경계를 부드럽게 만드는 값");
                    }

                    // Radius (Blur 크기)
                    if (ImGui::SliderFloat("Radius", &bloomSettings.radius, 0.0f, 20.0f, "%.1f"))
                    {
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Blur 크기 (0.0 ~ 20.0)\n값이 클수록 Bloom이 더 넓게 퍼집니다");
                    }

                    // Downsample (다운샘플링) - 1/64까지 지원
                    const char* downsampleItems[] = { "1x (원본)", "2x (1/2)", "4x (1/4)", "8x (1/8)", "16x (1/16)", "32x (1/32)", "64x (1/64)" };
                    int downsampleValues[] = { 1, 2, 4, 8, 16, 32, 64 };
                    int downsampleIdx = 0;
                    for (int i = 0; i < 7; ++i)
                    {
                        if (bloomSettings.downsample == downsampleValues[i])
                        {
                            downsampleIdx = i;
                            break;
                        }
                    }
                    if (ImGui::Combo("Downsample", &downsampleIdx, downsampleItems, IM_ARRAYSIZE(downsampleItems)))
                    {
                        bloomSettings.downsample = downsampleValues[downsampleIdx];
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Bloom 텍스처 다운샘플링 비율 (1x ~ 64x)\n낮을수록 고품질, 높을수록 성능 향상\n64x는 매우 작은 텍스처로 인해 품질이 낮을 수 있습니다");
                    }

                    // Clamp (Bloom 값 상한)
                    if (ImGui::SliderFloat("Clamp", &bloomSettings.clamp, 1.0f, 20.0f, "%.1f"))
                    {
                        bloomChanged = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Bloom 값 상한 (1.0 ~ 20.0)\n과도한 Bloom을 제한합니다");
                    }
                }

                // 설정 변경 시 즉시 반영
                if (bloomChanged)
                {
                    deferred.SetBloomSettings(bloomSettings);
                }
            }
            
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

                        ALICE_LOG_INFO("[Editor] Material albedo set from MatEditor: \"%s\"\n",
                                      g_MaterialEditorData.albedoTexturePath.c_str());
                    }
                }

                if (changed)
                {
                    // 1) 에셋 파일에 저장
                    MaterialFile::Save(g_MaterialEditorPath, g_MaterialEditorData);

                    // 2) 이 에셋을 참조하는 모든 엔티티의 MaterialComponent 를 갱신
                    const std::string targetPath = g_MaterialEditorPath.string();
                    const auto& allMats = world.GetComponents<MaterialComponent>();
                    for (const auto& [id, matConst] : allMats)
                    {
                        MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
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
                    ALICE_LOG_INFO("[Editor] SceneFile::Load (no-save path): \"%s\"\n",
                        g_NextScenePath.string().c_str());
                }
                {
                    const std::filesystem::path loadAbs =
                        (m_resources ? m_resources->Resolve(g_NextScenePath) : g_NextScenePath);
                    SceneFile::Load(world, loadAbs);
                }
                EnsureSkinnedMeshesRegistered(world);
                selectedEntity       = InvalidEntityId;
                g_CurrentScenePath   = g_NextScenePath;
                g_HasCurrentScenePath = true;
                g_SceneDirty         = false;
            }
            g_RequestSceneLoad = false;
        }
 
        

        // === 씬 로드 에러 모달 ===
        if (g_ShowSceneLoadError)
        {
            ImGui::OpenPopup("SceneLoadError");
            g_ShowSceneLoadError = false;
        }

        if (ImGui::BeginPopupModal("SceneLoadError", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "씬 로드 실패");
            ImGui::Separator();
            
            // 에러 메시지 표시 (여러 줄 지원)
            std::istringstream iss(g_SceneLoadErrorMsg);
            std::string line;
            while (std::getline(iss, line))
            {
                ImGui::TextWrapped("%s", line.c_str());
            }
            
            ImGui::Separator();
            if (ImGui::Button("확인"))
            {
                g_SceneLoadErrorMsg.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("SaveSceneBeforeLoad", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            Alice::ImGuiText(L"현재 씬의 변경 내용을 저장하시겠습니까?");
            ImGui::Separator();

			if (ImGui::Button("Save"))
			{
				SaveScene(world);
				// 씬 로드 요청 (안전 지점에서 커밋)
				if (sceneManager)
				{
					const std::filesystem::path loadAbs =
						(m_resources ? m_resources->Resolve(g_NextScenePath) : g_NextScenePath);
					if (!sceneManager->LoadSceneFileRequest(loadAbs))
					{
						// 요청 실패: 에러 로그 및 팝업 표시
						const std::string errorMsg = "씬 로드 요청 실패: " + g_NextScenePath.string() + "\n\n경로가 잘못되었거나 SceneManager가 초기화되지 않았습니다.";
						ALICE_LOG_ERRORF("[Editor] Scene load request failed: %s", g_NextScenePath.string().c_str());
						
						g_SceneLoadErrorMsg = errorMsg;
						g_ShowSceneLoadError = true;
						g_RequestSceneLoad = false;
						ImGui::CloseCurrentPopup();
						return;
					}
					// 성공 시 경로만 저장 (실제 로드는 엔진의 안전 지점에서 CommitPendingSceneChange로 처리됨)
					g_CurrentScenePath = g_NextScenePath;
					g_HasCurrentScenePath = true;
					g_SceneDirty = false;
				}
				else
				{
					ALICE_LOG_ERRORF("[Editor] SceneManager is null, cannot load scene");
				}
				selectedEntity = InvalidEntityId;
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

            ImGui::SameLine();
            if (ImGui::Button("Don't Save"))
            {
                // 씬 로드 요청 (안전 지점에서 커밋)
                if (sceneManager)
                {
                    ALICE_LOG_INFO("[Editor] LoadSceneFileRequest (dont-save): \"%s\"\n",
                        g_NextScenePath.string().c_str());
                    const std::filesystem::path loadAbs =
                        (m_resources ? m_resources->Resolve(g_NextScenePath) : g_NextScenePath);
                    if (!sceneManager->LoadSceneFileRequest(loadAbs))
                    {
                        // 요청 실패: 에러 로그 및 팝업 표시
                        const std::string errorMsg = "씬 로드 요청 실패: " + g_NextScenePath.string() + "\n\n경로가 잘못되었거나 SceneManager가 초기화되지 않았습니다.";
                        ALICE_LOG_ERRORF("[Editor] Scene load request failed: %s", g_NextScenePath.string().c_str());
                        
                        g_SceneLoadErrorMsg = errorMsg;
                        g_ShowSceneLoadError = true;
                        g_RequestSceneLoad = false;
                        return;
                    }
					// 성공 시 경로만 저장 (실제 로드는 엔진의 안전 지점에서 CommitPendingSceneChange로 처리됨)
					g_CurrentScenePath = g_NextScenePath;
					g_HasCurrentScenePath = true;
					g_SceneDirty = false;
				}
				else
				{
					ALICE_LOG_ERRORF("[Editor] SceneManager is null, cannot load scene");
				}
				selectedEntity = InvalidEntityId;
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				// 아무것도 하지 않고 씬 로드를 취소합니다.
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

            ImGui::EndPopup();
        }
    }

    void EditorCore::DrawInspectorTransform(World& world, const EntityId& _selectedEntity)
    {
        if (auto* transform =
            world.GetComponent<TransformComponent>(_selectedEntity)) {
            if (ImGui::CollapsingHeader("Transform",
                ImGuiTreeNodeFlags_DefaultOpen)) {
                bool changed = false;
                changed |= ReflectionUI::RenderProperty(*transform, "position", "Position");

                DirectX::XMFLOAT3 rotDeg = {
                    DirectX::XMConvertToDegrees(transform->rotation.x),
                    DirectX::XMConvertToDegrees(transform->rotation.y),
                    DirectX::XMConvertToDegrees(transform->rotation.z),
                };
                if (ImGui::DragFloat3("Rotation (deg)", &rotDeg.x, 1.0f)) {
                    transform->rotation = {
                        DirectX::XMConvertToRadians(rotDeg.x),
                        DirectX::XMConvertToRadians(rotDeg.y),
                        DirectX::XMConvertToRadians(rotDeg.z),
                    };
                    changed = true;
                }

                changed |= ReflectionUI::RenderProperty(*transform, "scale", "Scale");
                changed |= ReflectionUI::RenderProperty(*transform, "enabled", "Enabled");
                
                // Transform이 변경되었고 물리 컴포넌트가 있으면 텔레포트 자동 활성화
                if (changed)
                {
                    if (auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(_selectedEntity))
                    {
                        rigidBody->teleport = true;
                    }
                    if (auto* cct = world.GetComponent<Phy_CCTComponent>(_selectedEntity))
                    {
                        cct->teleport = true;
                    }
                    g_SceneDirty = true;
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

        // 엔진 컴포넌트 추가 UI - rttr으로 등록된 모든 컴포넌트 타입을 자동으로 처리
        if (ImGui::BeginCombo("Add Engine Component", "Select Component...")) {
            // rttr으로 등록된 모든 타입을 순회하며 "Component"로 끝나는 타입을 찾음
            // IScript는 제외 (스크립트는 별도 관리)
            static std::vector<rttr::type> componentTypes;
            if (componentTypes.empty()) {
                auto allTypes = rttr::type::get_types();
                for (const auto& type : allTypes) {
                    std::string typeName = type.get_name().to_string();
                    // "Component"로 끝나고 IScript가 아닌 타입만 추가
                    if (typeName.size() >= 9 && typeName.substr(typeName.size() - 9) == "Component") {
                        // IScript 제외
                        if (typeName != "IScript" && !type.is_derived_from(rttr::type::get<IScript>())) {
                            componentTypes.push_back(type);
                        }
                    }
                }
                // 타입 이름으로 정렬
                std::sort(componentTypes.begin(), componentTypes.end(),
                    [](const rttr::type& a, const rttr::type& b) {
                        return a.get_name().to_string() < b.get_name().to_string();
                    });
            }

            for (const auto& compType : componentTypes) {
                std::string typeName = compType.get_name().to_string();
                
                // 이미 해당 컴포넌트가 있는지 확인 (rttr을 통한 동적 확인은 복잡하므로,
                // 일단 모든 타입을 보여주고 추가 시 실패 처리)
                if (ImGui::Selectable(typeName.c_str())) {
                    // rttr을 통해 컴포넌트 추가 (템플릿 기반이므로 직접 호출은 어려움)
                    // 대신 World에 헬퍼 함수가 필요하거나, 여기서 직접 타입별 분기 처리
                    // 일단 간단하게 World::AddComponentByName 같은 함수를 사용하거나,
                    // 타입별 분기는 최소한으로 유지
                    // 임시로 알려진 타입들만 처리 (추후 개선 가능)
                    bool added = false;
                    if (typeName == "CameraComponent") {
                        world.AddComponent<CameraComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraFollowComponent") {
                        world.AddComponent<CameraFollowComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraSpringArmComponent") {
                        world.AddComponent<CameraSpringArmComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraLookAtComponent") {
                        world.AddComponent<CameraLookAtComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraShakeComponent") {
                        world.AddComponent<CameraShakeComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraBlendComponent") {
                        world.AddComponent<CameraBlendComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "CameraInputComponent") {
                        world.AddComponent<CameraInputComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "TransformComponent") {
                        world.AddComponent<TransformComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "MaterialComponent") {
                        world.AddComponent<MaterialComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "PointLightComponent") {
                        world.AddComponent<PointLightComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "SpotLightComponent") {
                        world.AddComponent<SpotLightComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "RectLightComponent") {
                        world.AddComponent<RectLightComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "EffectComponent") {
                        world.AddComponent<EffectComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "TrailEffectComponent") {
                        world.AddComponent<TrailEffectComponent>(_selectedEntity);
                    } else if (typeName == "Phy_RigidBodyComponent") {
                        world.AddComponent<Phy_RigidBodyComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_ColliderComponent") {
                        world.AddComponent<Phy_ColliderComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_MeshColliderComponent") {
                        world.AddComponent<Phy_MeshColliderComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_CCTComponent") {
                        world.AddComponent<Phy_CCTComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_TerrainHeightFieldComponent") {
                        world.AddComponent<Phy_TerrainHeightFieldComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_SettingsComponent") {
                        world.AddComponent<Phy_SettingsComponent>(_selectedEntity);
                        added = true;
                    } else if (typeName == "Phy_JointComponent") {
                        world.AddComponent<Phy_JointComponent>(_selectedEntity);
                        added = true;
                    }
                    
                    if (added) {
                        g_SceneDirty = true;
                    }
                }
            }
            ImGui::EndCombo();
        }


        // 엔진 컴포넌트 표시 - rttr으로 등록된 모든 컴포넌트 타입을 자동으로 처리
        // (TransformComponent와 MaterialComponent는 별도 처리되므로 제외)
        static std::vector<rttr::type> displayComponentTypes;
        if (displayComponentTypes.empty()) {
            auto allTypes = rttr::type::get_types();
            for (const auto& type : allTypes) {
                std::string typeName = type.get_name().to_string();
                // "Component"로 끝나고 Transform/Material/IScript가 아닌 타입만 추가
                if (typeName.size() >= 9 && typeName.substr(typeName.size() - 9) == "Component") {
                    if (typeName != "IScript" && typeName != "TransformComponent" && 
                        typeName != "MaterialComponent" &&
                        !type.is_derived_from(rttr::type::get<IScript>())) {
                        displayComponentTypes.push_back(type);
                    }
                }
            }
            // 타입 이름으로 정렬
            std::sort(displayComponentTypes.begin(), displayComponentTypes.end(),
                [](const rttr::type& a, const rttr::type& b) {
                    return a.get_name().to_string() < b.get_name().to_string();
                });
        }

        // 각 컴포넌트 타입별로 UI 표시 (타입별 분기 처리 필요)
        for (const auto& compType : displayComponentTypes) {
            std::string typeName = compType.get_name().to_string();
            
            // 타입별로 컴포넌트 가져오기 및 제거 함수 호출
            if (typeName == "CameraComponent") {
                DrawEngineComponent("CameraComponent",
                    world.GetComponent<CameraComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraComponent>(_selectedEntity); });
            } else if (typeName == "CameraFollowComponent") {
                DrawEngineComponent("CameraFollowComponent",
                    world.GetComponent<CameraFollowComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraFollowComponent>(_selectedEntity); });
            } else if (typeName == "CameraSpringArmComponent") {
                DrawEngineComponent("CameraSpringArmComponent",
                    world.GetComponent<CameraSpringArmComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraSpringArmComponent>(_selectedEntity); });
            } else if (typeName == "CameraLookAtComponent") {
                DrawEngineComponent("CameraLookAtComponent",
                    world.GetComponent<CameraLookAtComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraLookAtComponent>(_selectedEntity); });
            } else if (typeName == "CameraShakeComponent") {
                DrawEngineComponent("CameraShakeComponent",
                    world.GetComponent<CameraShakeComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraShakeComponent>(_selectedEntity); });
            } else if (typeName == "CameraBlendComponent") {
                DrawEngineComponent("CameraBlendComponent",
                    world.GetComponent<CameraBlendComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraBlendComponent>(_selectedEntity); });
            } else if (typeName == "CameraInputComponent") {
                DrawEngineComponent("CameraInputComponent",
                    world.GetComponent<CameraInputComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<CameraInputComponent>(_selectedEntity); });
            } else if (typeName == "PointLightComponent") {
                DrawEngineComponent("PointLightComponent",
                    world.GetComponent<PointLightComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<PointLightComponent>(_selectedEntity); });
            } else if (typeName == "SpotLightComponent") {
                DrawEngineComponent("SpotLightComponent",
                    world.GetComponent<SpotLightComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<SpotLightComponent>(_selectedEntity); });
            } else if (typeName == "RectLightComponent") {
                DrawEngineComponent("RectLightComponent",
                    world.GetComponent<RectLightComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<RectLightComponent>(_selectedEntity); });
            } else if (typeName == "EffectComponent") {
                DrawEngineComponent("EffectComponent",
                    world.GetComponent<EffectComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<EffectComponent>(_selectedEntity); });
            } else if (typeName == "TrailEffectComponent") {
                DrawEngineComponent("TrailEffectComponent",
                    world.GetComponent<TrailEffectComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<TrailEffectComponent>(_selectedEntity); });
            } else if (typeName == "Phy_RigidBodyComponent") {
                DrawEngineComponent("Phy_RigidBodyComponent",
                    world.GetComponent<Phy_RigidBodyComponent>(_selectedEntity),
                    [&]() { world.RemoveComponent<Phy_RigidBodyComponent>(_selectedEntity); });
            } else if (typeName == "Phy_ColliderComponent") {
                DrawInspectorCollider(world, _selectedEntity);
            } else if (typeName == "Phy_MeshColliderComponent") {
                DrawInspectorMeshCollider(world, _selectedEntity);
            } else if (typeName == "Phy_CCTComponent") {
                DrawInspectorCharacterController(world, _selectedEntity);
            } else if (typeName == "Phy_TerrainHeightFieldComponent") {
                DrawInspectorTerrainHeightField(world, _selectedEntity);
            } else if (typeName == "Phy_SettingsComponent") {
                DrawInspectorPhysicsSceneSettings(world, _selectedEntity);
            } else if (typeName == "Phy_JointComponent") {
                DrawInspectorJoint(world, _selectedEntity);
            }
            // 새로운 컴포넌트 타입이 추가되면 여기에 else if 추가
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
                    if (ImGui::Button("Remove"))
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
                                    if (ImGui::Selectable("None",currentRef == InvalidEntityId)) {
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
                            }
                            else {
                                // Generic
                                if (ReflectionUI::Detail::RenderProperty(prop, inst))
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

    // Engine Components
    void EditorCore::DrawEngineComponent(const char* label, auto* comp, auto removeFn)
    {
		if (!comp) return;
		if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
			bool changed = false;
			if (ImGui::Button("Remove")) {
				removeFn();
				g_SceneDirty = true;
				return;
			}
			changed |= ReflectionUI::RenderInspector(*comp);
			if (changed) g_SceneDirty = true;
		}
    }

    void EditorCore::DrawInspectorMaterial(World& world, const EntityId& _selectedEntity)
    {
        if (auto* mat = world.GetComponent<MaterialComponent>(_selectedEntity)) {
            ImGui::Text("Material");
            if (!mat->assetPath.empty())
                ImGui::Text("Asset: %s", mat->assetPath.c_str());

            bool changed = false;
            changed |= ReflectionUI::RenderInspector(*mat, MaterialInspectorFilter);

            const char* shadingItems[] = {
                "Global",
                "Lambert",
                "Phong",
                "Blinn-Phong",
                "Toon",
                "PBR",
                "ToonPBR"
            };
            int shadingIndex = mat->shadingMode + 1; // -1 -> 0 (Global)
            shadingIndex = std::clamp(shadingIndex, 0, (int)(std::size(shadingItems) - 1));
            if (ImGui::Combo("Shading", &shadingIndex, shadingItems, (int)std::size(shadingItems)))
            {
                mat->shadingMode = shadingIndex - 1;
                changed = true;
            }

            ImGui::Text("Albedo: %s", mat->albedoTexturePath.empty()
                ? "None"
                : mat->albedoTexturePath.c_str());
            if (ImGui::Button("Browse...")) {
                wchar_t buf[MAX_PATH] = {};
                OPENFILENAMEW ofn = { sizeof(ofn) };
                ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.dds\0All\0*.*\0";
                ofn.lpstrFile = buf;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) {
                    mat->albedoTexturePath = std::filesystem::path(buf).string();
                    changed = true;
                }
            }

            if (changed) {
                g_SceneDirty = true; /* Save logic omitted for brevity as requested
                                                                "Short code" */
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
                
                if (ImGui::Button("Remove"))
                {
                    world.RemoveComponent<Phy_ColliderComponent>(_selectedEntity);
                    g_SceneDirty = true;
                    return;
                }
                
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
                changed |= ReflectionUI::RenderInspector(*collider, [](const std::string& name) {
                    // type, layerBits, collideMask, queryMask는 커스텀 UI로 처리
                    return name != "type" && name != "layerBits" && name != "collideMask" && name != "queryMask" && name != "physicsActorHandle";
                });
                
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
                
                // Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능 (16개 레이어만 지원)
                ImGui::Text("Layer");
                ImGui::Indent();
                {
                    // 현재 선택된 레이어 찾기
                    int currentLayer = -1;
                    for (int i = 0; i < 16; ++i) // 16개만 확인
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
                
                // Collide Mask (어떤 레이어와 충돌할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Collide Mask", collider->collideMask, layerNames);
                
                // Query Mask (어떤 레이어를 쿼리할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Query Mask", collider->queryMask, layerNames);
                
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

                if (ImGui::Button("Remove"))
                {
                    world.RemoveComponent<Phy_MeshColliderComponent>(_selectedEntity);
                    g_SceneDirty = true;
                    return;
                }

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

                changed |= ReflectionUI::RenderInspector(*meshCollider, [](const std::string& name) {
                    return name != "type" && name != "layerBits" && name != "collideMask" && name != "queryMask" &&
                           name != "ignoreLayers" && name != "physicsActorHandle" &&
                           name != "flipNormals" && name != "doubleSidedQueries" && name != "validate" &&
                           name != "shiftVertices" && name != "vertexLimit";
                });

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

                changed |= DrawLayerMaskChipEditor("Collide Mask", meshCollider->collideMask, layerNames);
                changed |= DrawLayerMaskChipEditor("Query Mask", meshCollider->queryMask, layerNames);

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
                
                if (ImGui::Button("Remove"))
                {
                    world.RemoveComponent<Phy_CCTComponent>(_selectedEntity);
                    g_SceneDirty = true;
                    return;
                }
                
                // 기본 프로퍼티는 ReflectionUI로
                changed |= ReflectionUI::RenderInspector(*cct, [](const std::string& name) {
                    // layerBits, collideMask, queryMask는 커스텀 UI로 처리
                    return name != "layerBits" && name != "collideMask" && name != "queryMask" && name != "controllerHandle";
                });
                
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
                
                // Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능 (16개 레이어만 지원)
                ImGui::Text("Layer");
                ImGui::Indent();
                {
                    // 현재 선택된 레이어 찾기
                    int currentLayer = -1;
                    for (int i = 0; i < 16; ++i) // 16개만 확인
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
                
                // Collide Mask (어떤 레이어와 충돌할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Collide Mask", cct->collideMask, layerNames);
                
                // Query Mask (어떤 레이어를 쿼리할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Query Mask", cct->queryMask, layerNames);
                
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
                
                if (ImGui::Button("Remove"))
                {
                    world.RemoveComponent<Phy_SettingsComponent>(_selectedEntity);
                    g_SceneDirty = true;
                    return;
                }
                
                // 기본 프로퍼티는 ReflectionUI로
                changed |= ReflectionUI::RenderInspector(*settings, [](const std::string& name) {
                    // layerCollideMatrix, layerQueryMatrix, layerNames는 커스텀 UI로 처리
                    return name != "layerCollideMatrix" && name != "layerQueryMatrix" && name != "layerNames" &&
                           name != "enableGroundPlane" && name != "groundStaticFriction" && name != "groundDynamicFriction" &&
                           name != "groundRestitution" && name != "groundLayerBits" && name != "groundCollideMask" &&
                           name != "groundQueryMask" && name != "groundIgnoreLayers" && name != "groundIsTrigger";
                });

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
                
                // 레이어 충돌 매트릭스 편집 (최대 16개 레이어만 표시)
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
                
                // 레이어 쿼리 매트릭스 편집 (최대 16개 레이어만 표시)
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
                
                if (ImGui::Button("Remove"))
                {
                    world.RemoveComponent<Phy_TerrainHeightFieldComponent>(_selectedEntity);
                    g_SceneDirty = true;
                    return;
                }
                
                // 기본 프로퍼티는 ReflectionUI로
                changed |= ReflectionUI::RenderInspector(*terrain, [](const std::string& name) {
                    // layerBits, collideMask, queryMask, heightSamples, physicsActorHandle는 커스텀 UI로 처리
                    return name != "layerBits" && name != "collideMask" && name != "queryMask" && 
                           name != "heightSamples" && name != "physicsActorHandle";
                });
                
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
                
                // Layer Bits (이 오브젝트가 속한 레이어) - 1개만 선택 가능 (16개 레이어만 지원)
                ImGui::Text("Layer");
                ImGui::Indent();
                {
                    // 현재 선택된 레이어 찾기
                    int currentLayer = -1;
                    for (int i = 0; i < 16; ++i) // 16개만 확인
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
                
                // Collide Mask (어떤 레이어와 충돌할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Collide Mask", terrain->collideMask, layerNames);
                
                // Query Mask (어떤 레이어를 쿼리할지) - 칩 UI
                changed |= DrawLayerMaskChipEditor("Query Mask", terrain->queryMask, layerNames);
                
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

                if (ImGui::Button("Remove"))
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
                            hfs << "#include \"Core/IScript.h\"\n";
                            hfs << "#include \"Core/ScriptReflection.h\"\n\n";
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
                            cfs << "#include \"" << "Core/ScriptFactory.h\n";
                            cfs << "#include \"" << "Core/Logger.h\n";
                            cfs << "#include \"Core/World.h\"\n\n";
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
                    };
                    j["Scripts"] = nlohmann::json::array();

                    std::ofstream ofs(newPath);
                    if (ofs.is_open())
                        ofs << j.dump(4);
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

                    // JSON(.mat)로 저장 (RTTR + ReflectionSerializer 내부 사용)
                    MaterialComponent mat;
                    mat.color = DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
                    mat.roughness = 0.5f;
                    mat.metalness = 0.0f;
                    mat.assetPath = newPath.string();
                    mat.albedoTexturePath.clear();
                    MaterialFile::Save(newPath, mat);
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
                    const EntityId e = temp.CreateEntity();
                    temp.AddComponent<TransformComponent>(e);
                    temp.AddComponent<MaterialComponent>(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
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
                    g_NextScenePath    = path;
                    g_RequestSceneLoad = true;
                }
                else if (ext == ".mat")
                {
                    // 머티리얼 에셋 전용 편집 창을 엽니다.
                    g_MaterialEditorPath = path;
                    g_MaterialEditorData = {};
                    // 파일에서 값을 불러옵니다. 실패하면 기본 값으로 남겨둡니다.
                    MaterialFile::Load(path, g_MaterialEditorData, m_resources);
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
                            g_SceneDirty   = true;
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
                            MaterialFile::Load(path, *mat, m_resources);
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
                            ALICE_LOG_INFO("[Editor] Instantiate FBX: assetPath=\"%s\" sourceFbx=\"%s\" meshKey=\"%s\" mats=%zu\n",
                                          path.string().c_str(),
                                          asset.sourceFbx.c_str(),
                                          asset.meshAssetPath.c_str(),
                                          asset.materialAssetPaths.size());

                            // 레지스트리에 GPU 메시가 없다면, 원본 FBX 를 다시 임포트해서 등록합니다.
                            if (m_skinnedRegistry && m_resources && m_renderDevice)
                            {
                                if (!m_skinnedRegistry->Find(asset.meshAssetPath))
                                {
                                    FbxImportOptions opt{};
                                    FbxImporter importer(*m_resources, m_skinnedRegistry);
                                    auto* device = m_renderDevice->GetDevice();
                                    // 원본 FBX 경로는 .fbxasset 안의 source_fbx 에 저장되어 있습니다.
                                    std::filesystem::path srcFbxPath =
                                        (m_resources ? m_resources->Resolve(asset.sourceFbx) : std::filesystem::path(asset.sourceFbx));
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
                            t.scale    = { 1.0f, 1.0f, 1.0f };
                            t.rotation = { 0.0f, 0.0f, 0.0f };

                            SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, asset.meshAssetPath);
                            skinned.instanceAssetPath     = path.string();
                            static DirectX::XMFLOAT4X4 s_identityBone =
                                DirectX::XMFLOAT4X4(1,0,0,0,
                                                    0,1,0,0,
                                                    0,0,1,0,
                                                    0,0,0,1);
                            skinned.boneMatrices = &s_identityBone;
                            skinned.boneCount    = 1;

                            ALICE_LOG_INFO("[Editor] Instantiate FBX: created entity=%u, boneCount=%u\n",
                                          static_cast<unsigned>(e),
                                          skinned.boneCount);

                            if (!asset.materialAssetPaths.empty())
                            {
                                DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                                MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
                                mat.assetPath = asset.materialAssetPaths.front();
                                MaterialFile::Load(mat.assetPath, mat, m_resources);
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
        if (!m_skinnedRegistry || !m_resources || !m_renderDevice || world.GetComponents<SkinnedMeshComponent>().empty())
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
            std::filesystem::path absPath = m_resources->Resolve(fbxPath);

            // 로드 실패 검사
            if (!Alice::LoadFbxInstanceAsset(absPath, instance) || instance.sourceFbx.empty())
            {
                ALICE_LOG_WARN("[Editor] Failed loading fbxasset: %s", absPath.string().c_str());
                continue;
            }

            // 재임포트 및 등록
            FbxImporter importer(*m_resources, m_skinnedRegistry);
            FbxImportResult res = importer.Import(m_renderDevice->GetDevice(), m_resources->Resolve(instance.sourceFbx), {});

            ALICE_LOG_INFO("[Editor] Re-imported FBX: %s -> %s", instance.sourceFbx.c_str(), res.meshAssetPath.c_str());
        }
    }

    // 씬 저장
    void EditorCore::SaveScene(World& world)
    {
        std::filesystem::path savePath = g_CurrentScenePath.empty() ? "Assets/AutoSaved.scene" : g_CurrentScenePath;

        ALICE_LOG_INFO("[Editor] Saving Scene: %s", savePath.string().c_str());

        // 저장 실행 (실패 처리는 내부 로직에 맡김)
        SceneFile::Save(world, m_resources ? m_resources->Resolve(savePath) : savePath);

        // 상태 갱신
        g_CurrentScenePath = savePath;
        g_HasCurrentScenePath = true;
        g_SceneDirty = false;
    }

    // 씬 로드 (레거시 함수 - 이제는 LoadSceneFileRequest 사용 권장)
    void EditorCore::LoadScene(World& world)
    {
        // 이 함수는 더 이상 사용하지 않음. SceneManager::LoadSceneFileRequest을 사용해야 함.
        // 하지만 호환성을 위해 남겨둠 (내부적으로는 즉시 로드)
        ALICE_LOG_WARN("[Editor] LoadScene() is deprecated. Use SceneManager::LoadSceneFileRequest() instead.");

        const std::filesystem::path loadAbs = m_resources ? m_resources->Resolve(g_NextScenePath) : g_NextScenePath;
        
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
}



