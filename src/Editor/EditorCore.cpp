#include "Editor/EditorCore.h"

#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Core/ImGuiEx.h"

// ImGui
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

using namespace DirectX;

namespace Alice
{
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
                for (const auto& [entityId, transform] : transforms)
                {
                    (void)transform;

                    const bool isSelected = (selectedEntity == entityId);
                    const std::string label = "Entity " + std::to_string(static_cast<std::uint32_t>(entityId));

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        selectedEntity = entityId;
                    }
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

                auto* transform = world.GetTransform(selectedEntity);
                if (!transform)
                {
                    Alice::ImGuiText(L"Transform 컴포넌트가 없습니다.");
                }
                else
                {
                    ImGui::Text("Transform");
                    ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
                    ImGui::DragFloat3("Rotation (rad)", &transform->rotation.x, 0.01f);
                    ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
                }
            }
        }
        ImGui::End();

        // === Project ===
        if (ImGui::Begin("Project"))
        {
            Alice::ImGuiText(L"Resource 폴더");
            ImGui::Separator();

            const std::filesystem::path resourceRoot = "../Resource";
            DrawDirectoryNode(resourceRoot);
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
    }

    void EditorCore::DrawDirectoryNode(const std::filesystem::path& path)
    {
        namespace fs = std::filesystem;
        if (!fs::exists(path)) return;

        const bool isDirectory = fs::is_directory(path);
        const std::string label = path.filename().string();

        ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_SpanAvailWidth;

        if (isDirectory)
        {
            const bool open = ImGui::TreeNodeEx(label.c_str(), baseFlags);
            if (open)
            {
                for (const auto& entry : fs::directory_iterator(path))
                {
                    DrawDirectoryNode(entry.path());
                }
                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TreeNodeEx(label.c_str(),
                              baseFlags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        }
    }
}



