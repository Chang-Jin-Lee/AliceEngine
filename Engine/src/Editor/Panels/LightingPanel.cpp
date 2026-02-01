#include "Editor/Core/EditorCore.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <DirectXMath.h>

namespace Alice
{
	void EditorCore::DrawLightingWindow(World& world,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		int& shadingMode,
		bool& useFillLight,
		bool& useForwardRendering)
	{
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
			ImGui::SameLine();
			if (ImGui::RadioButton("ToonPBREditable", mode == 7)) mode = 7;
			shadingMode = mode;

			Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

			// Forward/Deferred 모드에 따라 조명 파라미터를 각 렌더러에 반영합니다.
			//auto& lighting = useForwardRendering ? forward.GetLightingParameters() : deferred.GetLightingParameters();
			//auto& lighting = forward.GetLightingParameters();
			auto& lighting = deferred.GetLightingParameters();

			// PBR 모드일 때 PBR 파라미터 표시
			if (mode == 4 || mode == 5 || mode == 7)
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

			// === Skybox ===
			ImGui::Separator();
			ImGui::TextUnformatted("Skybox");

			static int  skyboxChoice = 3; // 0 Off, 1 Bridge, 2 Indoor, 3 Baker, 4 darkenv
			static int  lastSkyboxChoice = -1;
			static bool lastForward = false;

			const char* skyboxItems[] = { "Off", "Bridge", "Indoor", "Baker", "darkenv" };

			auto ApplySkybox = [&](auto& renderer)
				{
					if (skyboxChoice == 0)
					{
						renderer.SetSkyboxEnabled(false);
						return;
					}

					renderer.SetSkyboxEnabled(true);
					switch (skyboxChoice)
					{
					case 1: renderer.SetIblSet("Bridge", "bridge");       break;
					case 2: renderer.SetIblSet("Indoor", "indoor");       break;
					case 3: renderer.SetIblSet("Sample", "BakerSample");  break;
					case 4: renderer.SetIblSet("darkenv", "darkenvDiffuseHDR");  break;
					default: break;
					}
				};

			auto EditBgIfOff = [&](auto& renderer)
				{
					if (skyboxChoice != 0) return;

					DirectX::XMFLOAT4 bgColor = renderer.GetBackgroundColor();
					if (ImGui::ColorEdit4("Background Color", &bgColor.x))
						renderer.SetBackgroundColor(bgColor);
				};

			bool skyboxChanged = ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems));
			bool rendererChanged = (lastForward != useForwardRendering);

			// 선택 변경 or 렌더러 토글 변경 시 반영 (초기 1회 포함)
			if (skyboxChanged || rendererChanged || lastSkyboxChoice != skyboxChoice)
			{
				if (useForwardRendering) ApplySkybox(forward);
				else                     ApplySkybox(deferred);

				lastSkyboxChoice = skyboxChoice;
				lastForward = useForwardRendering;
			}

			if (useForwardRendering) EditBgIfOff(forward);
			else                     EditBgIfOff(deferred);

			// === Post-Process (Exposure, Max HDR Nits) ===
			ImGui::Separator();
			ImGui::TextUnformatted("Post-Process");
			ImGui::Separator();

			float exposure = 0.0f;
			float maxHDRNits = 1000.0f;
			DirectX::XMFLOAT4 saturation = { 1.0f, 1.0f, 1.0f, 1.0f };
			DirectX::XMFLOAT4 contrast = { 1.0f, 1.0f, 1.0f, 1.0f };
			DirectX::XMFLOAT4 gamma = { 1.0f, 1.0f, 1.0f, 1.0f };
			DirectX::XMFLOAT4 gain = { 1.0f, 1.0f, 1.0f, 1.0f };

			auto DrawPostProcess = [&](auto& renderer)
				{
					// Exposure와 MaxHDRNits는 기존 함수로 가져오기
					renderer.GetPostProcessParams(exposure, maxHDRNits);
					// Color Grading은 Vector4로 가져오기
					renderer.GetColorGrading(saturation, contrast, gamma, gain);

					bool changed = false;

					changed |= ImGui::SliderFloat("Exposure", &exposure, -3.0f, 3.0f, "%.2f");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Exposure 값: -3.0 (어두움) ~ 3.0 (밝음)\n0.0 = 1.0배 (기본값)");

					changed |= ImGui::SliderFloat("Max HDR Nits", &maxHDRNits, 100.0f, 10000.0f, "%.0f nits");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("HDR 모니터 최대 밝기 (nits)\n일반 모니터: 100-300 nits\nHDR 모니터: 1000-10000 nits");

					ImGui::Separator();
					ImGui::TextUnformatted("Color Grading (RGB 채널별 제어)");

					ImGui::PushItemWidth(-1);

					ImGui::Text("Saturation (RGB)");
					changed |= ImGui::ColorEdit4("Saturation (RGB)", reinterpret_cast<float*>(&saturation),
						ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("채도 (R,G,B 채널별): 0.0 = 흑백, 1.0 = 원본, 2.0+ = 과포화\nW 채널은 항상 1.0으로 유지됩니다.");

					ImGui::Text("Contrast (RGB)");
					changed |= ImGui::ColorEdit4("Contrast (RGB)", reinterpret_cast<float*>(&contrast),
						ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("대비 (R,G,B 채널별): 0.0 = 회색, 1.0 = 원본, 2.0 = 고대비\nW 채널은 항상 1.0으로 유지됩니다.");

					ImGui::Text("Gamma (RGB)");
					changed |= ImGui::ColorEdit4("Gamma (RGB)", reinterpret_cast<float*>(&gamma),
						ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("감마 보정 (R,G,B 채널별): 1.0 = 원본, <1.0 = 밝게, >1.0 = 어둡게\nW 채널은 항상 1.0으로 유지됩니다.");

					ImGui::Text("Gain (RGB)");
					changed |= ImGui::ColorEdit4("Gain (RGB)", reinterpret_cast<float*>(&gain),
						ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Gain Multiply 스케일 (R,G,B 채널별): 0.0 = 검정, 1.0 = 원본, >1.0 = 밝게\nW 채널은 항상 1.0으로 유지됩니다.");
					ImGui::PopItemWidth();

					// W 채널은 항상 1.0으로 유지
					saturation.w = 1.0f;
					contrast.w = 1.0f;
					gamma.w = 1.0f;
					gain.w = 1.0f;

					if (changed)
					{
						// Exposure와 MaxHDRNits는 기존 함수로 설정
						renderer.SetPostProcessParams(exposure, maxHDRNits);
						// Color Grading은 Vector4로 설정
						renderer.ApplyColorGrading(saturation, contrast, gamma, gain);
					}
				};

			if (useForwardRendering) DrawPostProcess(forward);
			else                     DrawPostProcess(deferred);

			// === Post Process Volume Reference Object ===
			if (!useForwardRendering)
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Post Process Volume Reference");
				ImGui::Separator();

				static char refObjectNameBuf[256] = "";
				std::string currentRefName = deferred.GetPPVReferenceObjectName();
				if (strcmp(refObjectNameBuf, currentRefName.c_str()) != 0)
				{
					strncpy_s(refObjectNameBuf, currentRefName.c_str(), sizeof(refObjectNameBuf) - 1);
					refObjectNameBuf[sizeof(refObjectNameBuf) - 1] = '\0';
				}

				if (ImGui::InputText("PPV Reference GameObject Name", refObjectNameBuf, sizeof(refObjectNameBuf)))
				{
					deferred.SetPPVReferenceObjectName(refObjectNameBuf);
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("PostProcessVolume 보간 기준이 될 GameObject 이름\n비어있으면 카메라 위치 사용");

				// 현재 바인딩 상태 표시
				if (!currentRefName.empty())
				{
					GameObject refObj = world.FindGameObject(currentRefName);
					if (refObj.IsValid())
					{
						auto* transform = world.GetComponent<TransformComponent>(refObj.id());
						if (transform && transform->enabled)
						{
							ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
								"Bound to: %s (Position: %.2f, %.2f, %.2f)",
								currentRefName.c_str(),
								transform->position.x, transform->position.y, transform->position.z);
						}
						else
						{
							ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
								"Bound to: %s (Transform not found or disabled)", currentRefName.c_str());
						}
					}
					else
					{
						ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
							"Object not found: %s (using camera position)", currentRefName.c_str());
					}
				}
				else
				{
					ImGui::TextDisabled("Using camera position as reference");
				}
			}

			// === Bloom (Deferred 전용) ===
			if (!useForwardRendering)
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Bloom");
				ImGui::Separator();

				BloomSettings bloomSettings = deferred.GetBloomSettings();
				bool bloomChanged = false;

				if (ImGui::Checkbox("Enable Bloom", &bloomSettings.enabled))
					bloomChanged = true;

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Bloom 효과 활성화/비활성화");

				if (bloomSettings.enabled)
				{
					if (ImGui::SliderFloat("Bloom Intensity", &bloomSettings.intensity, 0.0f, 5.0f, "%.2f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Bloom 합성 강도 (0.0 ~ 5.0)\n최종 합성 단계에서 적용되는 강도\n값이 클수록 더 밝게 합성됩니다");

					if (ImGui::SliderFloat("Gaussian Intensity", &bloomSettings.gaussianIntensity, 0.0f, 5.0f, "%.2f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Gaussian 블러 강도 (0.0 ~ 5.0)\n블러 단계에서 적용되는 강도\n값이 클수록 블러 결과가 더 밝아집니다");

					if (ImGui::SliderFloat("Threshold", &bloomSettings.threshold, 0.0f, 5.0f, "%.2f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("밝기 추출 기준 (0.0 ~ 5.0)\n이 값보다 밝은 픽셀만 Bloom이 적용됩니다");

					if (ImGui::SliderFloat("Knee", &bloomSettings.knee, 0.0f, 1.0f, "%.2f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Soft threshold (0.0 ~ 1.0)\nBloom 경계를 부드럽게 만드는 값");

					if (ImGui::SliderFloat("Radius", &bloomSettings.radius, 0.0f, 20.0f, "%.1f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Blur 크기 (0.0 ~ 20.0)\n값이 클수록 더 넓게 퍼집니다");

					const char* downsampleItems[] = {
						"1x (원본)", "2x (1/2)", "4x (1/4)", "8x (1/8)",
						"16x (1/16)", "32x (1/32)", "64x (1/64)"
					};
					const int downsampleValues[] = { 1, 2, 4, 8, 16, 32, 64 };

					int downsampleIdx = 0;
					for (int i = 0; i < 7; ++i)
					{
						if (bloomSettings.downsample == downsampleValues[i]) { downsampleIdx = i; break; }
					}

					if (ImGui::Combo("Downsample", &downsampleIdx, downsampleItems, IM_ARRAYSIZE(downsampleItems)))
					{
						bloomSettings.downsample = downsampleValues[downsampleIdx];
						bloomChanged = true;
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Bloom 다운샘플링 (1x ~ 64x)\n높을수록 성능↑ 품질↓");

					if (ImGui::SliderFloat("Clamp", &bloomSettings.clamp, 1.0f, 20.0f, "%.1f"))
						bloomChanged = true;
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Bloom 값 상한 (1.0 ~ 20.0)\n과도한 Bloom을 제한합니다");
				}

				if (bloomChanged)
					deferred.SetBloomSettings(bloomSettings);
			}
		}
		ImGui::End();
	}
}
