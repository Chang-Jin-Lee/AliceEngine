#include "Engine/Engine.h"

#include "Rendering/D3D11/D3D11RenderDevice.h"
#include "Rendering/DebugDrawSystem.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder API ���
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Win32 �޽��� ���� (GET_X/Y_LPARAM)
#include <Windowsx.h>

// ǥ�� ���̺귯��
#include <filesystem>
#include <cfloat>      // FLT_MAX
#include <algorithm>   // std::max
#include <memory>
#include <fstream>
#include <sstream>

// ���ڿ� ��ȯ / ImGui ����
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
        // ������ Ŭ���� �̸��� ���� ����� �����մϴ�.
        constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";

        // BuildSettings.txt ���� ���� ��(.scene ����)�� �о�� World �� �ε��մϴ�.
        // - scenes ������ "index: path" �������� ����Ǿ� �ִٰ� �����մϴ�.
        bool LoadStartupSceneFromBuildSettings(World& world, const std::filesystem::path& exeDir)
        {
            namespace fs = std::filesystem;

            fs::path cfgPath = exeDir / "BuildSettings.txt";
            if (!fs::exists(cfgPath))
            {
                // �����Ϳ��� �⺻���� �����ϴ� ��ġ (������Ʈ ��Ʈ/Build) �� �� �� �� �õ�
                fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Release �� ������Ʈ ��Ʈ
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
                const auto  b  = s.find_first_not_of(ws);
                const auto  e  = s.find_last_not_of(ws);
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

                // default: ���� ��� �־ ó��
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

                // "- path" ������ �� ���
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

            // ��� ��δ� exeDir �������� �ؼ�
            if (!scenePath.is_absolute())
            {
                // 1) exeDir �������� �õ�
                fs::path candidate = exeDir / scenePath;
                if (fs::exists(candidate))
                {
                    scenePath = candidate;
                }
                else
                {
                    // 2) exeDir ����(������Ʈ ��Ʈ) �������ε� �õ�
                    fs::path projectRoot = exeDir.parent_path().parent_path().parent_path();
                    candidate = projectRoot / scenePath;
                    if (fs::exists(candidate))
                    {
                        scenePath = candidate;
                    }
                    else
                    {
                        // �׷��� ������ exeDir ���� ��� ��η� ��
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

        // 1) �ν��Ͻ� �ڵ� ����
        m_hInstance = hInstance;

        // 2) ������ ����
        if (!CreateMainWindow(nCmdShow))
        {
            ALICE_LOG_ERRORF("Engine::Initialize: CreateMainWindow failed.");
            return false;
        }
        ALICE_LOG_INFO("Engine::Initialize: CreateMainWindow succeeded.");

        // 3) �Է� �ý��� �ʱ�ȭ (DirectXTK Keyboard/Mouse)
        m_inputSystem.Initialize(m_hWnd);
        ALICE_LOG_INFO("Engine::Initialize: InputSystem initialized.");

        // 4) ���� ����̽� ����(D3D11 ����ü ���)
        m_renderDevice = std::make_unique<D3D11RenderDevice>();
        if (!m_renderDevice->Initialize(m_hWnd, m_width, m_height))
        {
            ALICE_LOG_ERRORF("Engine::Initialize: D3D11RenderDevice::Initialize failed.");
            return false;
        }
        ALICE_LOG_INFO("Engine::Initialize: D3D11RenderDevice initialized.");

        // 5) ImGui / Editor �ھ� �ʱ�ȭ (������ ��忡����)
        if (m_editorMode)
        {
            if (!m_editorCore.Initialize(m_hWnd, *m_renderDevice))
            {
                ALICE_LOG_ERRORF("Engine::Initialize: EditorCore::Initialize failed.");
                return false;
            }
            // ResourceManager �� �����Ϳ� ���� (FBX ����Ʈ ��� ���)
            m_editorCore.SetResourceManager(&m_resourceManager);
            // SkinnedMeshRegistry �� �����Ϳ� ���� (FBX ����Ʈ �� GPU �޽� ���)
            m_editorCore.SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);
            ALICE_LOG_INFO("Engine::Initialize: EditorCore initialized.");
        }

        // 6) Forward ���� �ý��� �ʱ�ȭ
        m_forwardRenderSystem = std::make_unique<ForwardRenderSystem>(*m_renderDevice);
        // ���ҽ� �Ŵ����� ���� �ý��ۿ� �����մϴ� (�ؽ�ó ��ŷ/�ε� � ���).
        m_forwardRenderSystem->SetResourceManager(&m_resourceManager);
        // ��Ű�� �޽� ������Ʈ���� ���� �ý��ۿ� ���� (�����/���̷��� ��Ÿ������ ��ȸ��)
        m_forwardRenderSystem->SetSkinnedMeshRegistry(&m_skinnedMeshRegistry);
        if (!m_forwardRenderSystem->Initialize(m_width, m_height))
        {
            ALICE_LOG_ERRORF("Engine::Initialize: ForwardRenderSystem::Initialize failed.");
            return false;
        }
        ALICE_LOG_INFO("Engine::Initialize: ForwardRenderSystem initialized.");

        // 7) DebugDraw �ý��� �ʱ�ȭ (�ɼ� ���)
        m_debugDrawSystem = std::make_unique<DebugDrawSystem>(*m_renderDevice);
        if (!m_debugDrawSystem->Initialize())
        {
            ALICE_LOG_ERRORF("Engine::Initialize: DebugDrawSystem::Initialize failed.");
            return false;
        }
        ALICE_LOG_INFO("Engine::Initialize: DebugDrawSystem initialized.");

        // 8) ī�޶� ����
        const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
        m_cameraPosition = DirectX::XMFLOAT3(0.0f, 2.0f, -5.0f);
        DirectX::XMFLOAT3 target(0.0f, 0.0f, 0.0f);
        m_camera.SetLookAt(m_cameraPosition, target, DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
        m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 5000.0f);

        // 9) ��ũ��Ʈ DLL (���̺� �ڵ���) �ε� �õ�
        ScriptHotReload_Load();
        ALICE_LOG_INFO("Engine::Initialize: ScriptHotReload_Load called.");

        // 10) �� �Ŵ��� ���� �� �⺻ ��/�� ���� �ε�
        m_resourceManager.Clear();
        m_sceneManager = std::make_unique<SceneManager>(m_world, m_resourceManager);
        ALICE_LOG_INFO("Engine::Initialize: SceneManager created.");

        // ������ ���: �ڵ� ��� SampleScene �� �⺻���� ���
        if (m_editorMode)
        {
            m_sceneManager->SwitchTo("SampleScene");
            ALICE_LOG_INFO("Engine::Initialize: editor mode, switched to SampleScene.");
        }
        else
        {
            // ���� ���: BuildSettings.txt �� ���ǵ� 0�� �ε��� ��(.scene)�� �켱 �ε�
            wchar_t exePathW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
            std::filesystem::path exePath = exePathW;
            std::filesystem::path exeDir  = exePath.parent_path();

            if (!LoadStartupSceneFromBuildSettings(m_world, exeDir))
            {
                // ���� �� ������ �������� SampleScene �� ���
                m_sceneManager->SwitchTo("SampleScene");
                ALICE_LOG_WARN("Engine::Initialize: failed to load startup scene from BuildSettings, fallback to SampleScene.");
            }
            else
            {
                ALICE_LOG_INFO("Engine::Initialize: startup scene loaded from BuildSettings.");
            }
        }

        // ���� ���� SkinnedMeshComponent �鿡 �����ϴ� GPU �޽õ���
        // SkinnedMeshRegistry �� ��� ��ϵǾ� �ִ��� Ȯ���մϴ�.
        EnsureSkinnedMeshesRegisteredForWorld();

        const auto& transforms   = m_world.GetTransforms();
        const auto& skinnedMeshes = m_world.GetSkinnedMeshes();
        const auto& scripts      = m_world.GetScripts();
        const auto& materials    = m_world.GetMaterials();
        ALICE_LOG_INFO("Engine::Initialize: world summary: transforms=%zu, skinnedMeshes=%zu, scripts=%zu, materials=%zu",
                       transforms.size(), skinnedMeshes.size(), scripts.size(), materials.size());

        ALICE_LOG_INFO("Engine::Initialize: success.");
        return true;
    }

    int Engine::Run()
    {
        m_isRunning = true;

        MSG msg = {};

        // ���ػ� Ÿ�̸� �ʱ�ȭ
        m_timer.Reset();
        m_timer.Start();

        // �⺻ ���� ����
        while (m_isRunning)
        {
            // 1) ������ �޽��� ó��
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

        // 1) ī�޶� �̵� (WASD + Q/E) - ������ ���콺 ��ư�� ������ ���� ���� ����
        const bool canControlCamera = m_inputSystem.IsRightButtonDown(); // ��Ŭ�� ���¿����� �̵�/ȸ��

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
            // E: ����, Q: �Ʒ��� �̵�
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
                // ī�޶��� ���� ȸ���� ���� �̵� ���͸� ȸ��
                XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
                XMVECTOR worldMoveDir = XMVector3TransformNormal(moveDir, rotMatrix);
                worldMoveDir = XMVector3Normalize(worldMoveDir);

                XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
                pos = XMVectorAdd(pos, XMVectorScale(worldMoveDir, m_cameraMoveSpeed * m_timer.DeltaTime()));
                XMStoreFloat3(&m_cameraPosition, pos);
            }

            // 2) ���콺 �̵����� ī�޶� ȸ�� (��Ŭ�� ���¿�����)
            POINT mouseDelta = m_inputSystem.GetMouseDelta();
            m_cameraYawRadians   += static_cast<float>(mouseDelta.x) * m_cameraMouseSensitivity;
            // ���콺�� �Ʒ��� ������ ȭ�鵵 �Ʒ��� ������ Y�� ȸ���� �ݴ�� �����մϴ�.
            m_cameraPitchRadians += static_cast<float>(mouseDelta.y) * m_cameraMouseSensitivity;
        }

        // ��ġ ������ -89 ~ 89�� ���̷� ����
        const float pitchLimit = XMConvertToRadians(89.0f);
        if (m_cameraPitchRadians > pitchLimit)  m_cameraPitchRadians = pitchLimit;
        if (m_cameraPitchRadians < -pitchLimit) m_cameraPitchRadians = -pitchLimit;

        // 3) ī�޶� LookAt ����
        XMMATRIX rotMatrix = XMMatrixRotationRollPitchYaw(m_cameraPitchRadians, m_cameraYawRadians, 0.0f);
        XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotMatrix);

        XMVECTOR pos = XMLoadFloat3(&m_cameraPosition);
        XMVECTOR target = XMVectorAdd(pos, forward);

        XMFLOAT3 targetFloat3;
        XMStoreFloat3(&targetFloat3, target);

        m_camera.SetLookAt(m_cameraPosition, targetFloat3, XMFLOAT3(0.0f, 1.0f, 0.0f));

        // 4) ���� �� �� ��ũ��Ʈ ������Ʈ
        //    - ������ ���: Play ��ư�� ������ ���� ����
        //    - ���� ���� ���: �׻� ����
        const bool play = m_editorMode ? m_isPlaying : true;
        if (play)
        {
            if (m_sceneManager)
            {
                m_sceneManager->Update(m_timer.DeltaTime());
            }

            // ��ƼƼ�� �پ� �ִ� ��� ScriptComponent �� �����մϴ�.
            m_scriptSystem.Update(m_world, m_timer.DeltaTime());
        }
    }

    void Engine::Render()
    {
        if (!m_renderDevice || !m_forwardRenderSystem)
            return;

        // ȭ�� Ŭ���� ���� (£�� �Ķ��� �迭)
        const float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

        m_renderDevice->BeginFrame(clearColor);

        // ������ ��忡���� ImGui/��ŷ UI + ����� ���� �׸��ϴ�.
        if (m_editorMode)
        {
            // ImGui ������ ���� (EditorCore �� ����)
            m_editorCore.BeginFrame();

            const float dt  = m_timer.DeltaTime();
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

            // DebugDraw ���� �ʱ�ȭ �� ���� ��(axis) �߰�
            if (m_debugDrawSystem)
            {
                m_debugDrawSystem->Clear();

                // �������� XYZ ���� �׸��ϴ�.
                // X: ����, Y: �ʷ�, Z: �Ķ�
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

        // ��Ű�� �޽� ��ο� ����Ʈ�� ���� �����մϴ�.
        m_skinnedMeshSystem.BuildDrawList(m_world, m_skinnedDrawCommands);

        // ������ Forward ������ (ť�� + ��Ű�� �޽�)
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

			// SRV/RTV ���� ���ҽ� ������
			Microsoft::WRL::ComPtr<ID3D11Resource> src;
			Microsoft::WRL::ComPtr<ID3D11Resource> dst;

			// src: ForwardRenderSystem �� �÷� �ؽ�ó
			auto* sceneSRV = m_forwardRenderSystem->GetSceneSRV();
			sceneSRV->GetResource(src.GetAddressOf());

			// dst: ����� �ؽ�ó
			backBufferRTV->GetResource(dst.GetAddressOf());

			// ���� ����
			ctx->CopyResource(dst.Get(), src.Get());
		}

        // DebugDraw ������ (Forward ���� ����, ���� ī�޶� ����)
        if (m_debugDrawSystem)
        {
            m_debugDrawSystem->Render(m_camera);
        }

        // ImGui ������ (������ ��忡����)
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
                continue; // �̹� ��ϵ�

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
                ALICE_LOG_WARN("Engine::EnsureSkinnedMeshesRegisteredForWorld: failed to load .fbxasset \"%s\" for meshKey=\"%s\"",
                               fbxAssetPath.string().c_str(),
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

            std::filesystem::path srcFbxPath = instance.sourceFbx;
            FbxImportResult result = importer.Import(device, srcFbxPath, opt);

            ALICE_LOG_INFO("Engine::EnsureSkinnedMeshesRegisteredForWorld: re-import FBX \"%s\" -> meshKey=\"%s\" result.mesh=\"%s\"",
                           srcFbxPath.string().c_str(),
                           comp.meshAssetPath.c_str(),
                           result.meshAssetPath.c_str());
        }
    }

    bool Engine::CreateMainWindow(int nCmdShow)
    {
        // 1) ������ Ŭ���� ���
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = &Engine::WindowProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = m_hInstance;
        // ���� ���� �������� �ε��մϴ�. (�����ϸ� �⺻ �������� ���)
        HICON hIconBig = static_cast<HICON>(LoadImageW(
            nullptr,
            L"../Resource/Icon/Alice.ico",
            IMAGE_ICON,
            32,
            32,
            LR_LOADFROMFILE));
        if (!hIconBig) hIconBig = LoadIcon(nullptr, IDI_APPLICATION);
        HICON hIconSmall = static_cast<HICON>(LoadImageW(
            nullptr,
            L"../Resource/Icon/Alice.ico",
            IMAGE_ICON,
            16,
            16,
            LR_LOADFROMFILE));
        if (!hIconSmall) hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

        wc.hIcon         = hIconBig;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = kWindowClassName;
        wc.hIconSm       = hIconSmall;

        if (!RegisterClassExW(&wc)) return false;

        // 2) ������ ũ�⸦ Ŭ���̾�Ʈ �������� ���߱� ���� ����
        RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

        const int windowWidth  = windowRect.right - windowRect.left;
        const int windowHeight = windowRect.bottom - windowRect.top;

        // 3) ������ ���� (this �����͸� lpParam���� ����)
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
        m_width  = width;
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
            const auto newWidth  = static_cast<std::uint32_t>(LOWORD(lParam));
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
        // ImGui�� ���� Win32 �޽����� ó���� �� �ֵ��� �����մϴ�.
        if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
            return true;

        // DirectXTK Keyboard / Mouse �� Win32 �޽��� ���� (GameApp::WndProc ����)
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

        // 1) WM_NCCREATE �ܰ迡�� Engine �ν��Ͻ� �����͸� HWND�� ����
        if (message == WM_NCCREATE)
        {
            auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto engine = static_cast<Engine*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(engine));
        }

        // 2) ����� Engine �����͸� �����ͼ� ��� �Լ��� ����
        auto engine = reinterpret_cast<Engine*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (engine) return engine->HandleMessage(hWnd, message, wParam, lParam);

        // 3) ���� �����Ͱ� ������ �⺻ ó��
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}


