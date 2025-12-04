#include "Editor/EditorCore.h"

#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Core/ImGuiEx.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <fstream>
#include <Core/Prefab.h>
#include <Core/Script.h>
#include <Core/Material.h>
#include <Core/SceneFile.h>
#include <shellapi.h>

using namespace DirectX;

namespace Alice
{
    namespace
    {
        // 현재 씬이 수정되었는지 여부 (저장 필요 여부)
        bool                     g_SceneDirty           = false;
        bool                     g_HasCurrentScenePath  = false;
        std::filesystem::path    g_CurrentScenePath;

        // 다른 씬을 로드하기 위해 대기 중인 경로
        bool                     g_RequestSceneLoad     = false;
        std::filesystem::path    g_NextScenePath;
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
                                  ViewportPicker& picker)
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
            ImGui::Text("DeltaTime: %.3f  FPS: %.1f", deltaTime, fps);

            ImGui::EndMainMenuBar();
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
                    if (ImGui::ColorEdit3("Base Color", &mat->color.x))
                    {
                        g_SceneDirty = true;
                    }

                    if (!mat->assetPath.empty())
                    {
                        ImGui::Text("Asset: %s", mat->assetPath.c_str());
                    }

                    if (ImGui::Button("Remove Material"))
                    {
                        world.RemoveMaterial(selectedEntity);
                        g_SceneDirty = true;
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
                SceneFile::Load(world, g_NextScenePath);
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
                std::filesystem::path savePath = g_CurrentScenePath;
                if (savePath.empty())
                {
                    savePath = "../Assets/AutoSaved.scene";
                }
                SceneFile::Save(world, savePath);
                g_CurrentScenePath    = savePath;
                g_HasCurrentScenePath = true;
                g_SceneDirty          = false;

                SceneFile::Load(world, g_NextScenePath);
                selectedEntity        = InvalidEntityId;
                g_CurrentScenePath    = g_NextScenePath;
                g_HasCurrentScenePath = true;
                g_SceneDirty          = false;

                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Don't Save"))
            {
                SceneFile::Load(world, g_NextScenePath);
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

        const bool isDirectory = fs::is_directory(path);
        const std::string label = path.filename().string();

        ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth;

        // 파일/폴더 이름 변경 상태를 관리하는 간단한 정적 상태입니다.
        static bool                 s_renaming      = false;
        static std::filesystem::path s_renamingPath;
        static char                 s_renameBuffer[260] = {};
        static bool                 s_renameFocus   = false;

        if (isDirectory)
        {
            const bool open = ImGui::TreeNodeEx(label.c_str(), baseFlags);

            // 디렉터리 노드에 대한 우클릭 컨텍스트 메뉴 (스크립트/프리팹 생성 등)
            if (ImGui::BeginPopupContextItem())
            {
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
                for (const auto& entry : fs::directory_iterator(path))
                {
                    DrawDirectoryNode(world, selectedEntity, entry.path());
                }
                ImGui::TreePop();
            }
        }
        else
        {
            const std::string ext = path.extension().string();

            const bool isRenamingThis = s_renaming && (s_renamingPath == path);

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
                    std::strncpy(s_renameBuffer, fileName.c_str(), sizeof(s_renameBuffer) - 1);
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

                ImGui::EndPopup();
            }
        }
    }
}



