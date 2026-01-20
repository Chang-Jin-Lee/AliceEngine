#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/SceneFile.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"

#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/World.h"
#include "Components/ScriptComponent.h"
#include <wrl/client.h>
#include <dxgi.h>
#include <dxgi1_3.h>

namespace Alice
{
    namespace
    {
        // 스키닝 메시가 아직 애니메이션 시스템과 연결되지 않았을 때 사용할
        // 1개짜리 항등 본 팔레트입니다. (정적인 메시처럼 렌더링되도록 함)
        static DirectX::XMFLOAT4X4 g_IdentityBone(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1);

        // 프로젝트 루트 경로를 구하는 헬퍼 함수
        static std::filesystem::path GetProjectRoot()
        {
            wchar_t exePathW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
            std::filesystem::path exePath = exePathW;
            std::filesystem::path exeDir = exePath.parent_path();
            // build/bin/Debug 또는 build/bin/Release 가 나옴. 프로젝트 루트임
            return exeDir.parent_path().parent_path().parent_path();
        }

        // 절대 경로를 상대 경로로 변환하는 헬퍼 함수
        // Assets/ 또는 Resource/로 시작하는 경로는 그대로 유지함
        static std::string NormalizePathToRelative(const std::string& path)
        {
            if (path.empty())
                return path;

            std::filesystem::path p(path);
            
            // 이미 상대 경로이거나 Assets/ 또는 Resource/로 시작하면 그대로 반환
            if (!p.is_absolute())
            {
                const std::string s = p.generic_string();
                if (s.find("Assets/") == 0 || s.find("Resource/") == 0 || s.find("Cooked/") == 0)
                    return s;
            }

            // 절대 경로인 경우 프로젝트 루트 기준 상대 경로로 변환
            if (p.is_absolute())
            {
                const std::filesystem::path projectRoot = GetProjectRoot();
                try
                {
                    std::filesystem::path relative = std::filesystem::relative(p, projectRoot);
                    if (!relative.empty())
                    {
                        const std::string result = relative.generic_string();
                        // Assets/ 또는 Resource/로 시작하는지 확인
                        if (result.find("Assets/") == 0 || result.find("Resource/") == 0 || result.find("Cooked/") == 0)
                            return result;
                        // 상대 경로 변환이 실패하거나 예상과 다른 경우 원본 반환
                    }
                }
                catch (...)
                {
                    // relative() 실패 시 원본 반환
                }
            }

            return path;
        }

        static bool WriteEntity(JsonRttr::json& outEntity, const World& world, EntityId id)
        {
            outEntity = JsonRttr::json::object();
            outEntity["id"] = static_cast<std::uint32_t>(id);

            const std::string name = world.GetEntityName(id);
            if (!name.empty())
                outEntity["name"] = name;
            
            if (const auto* transform = world.GetComponent<TransformComponent>(id); transform)
            {
                rttr::instance inst = const_cast<TransformComponent&>(*transform);
                outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* scripts = world.GetScripts(id); scripts && !scripts->empty())
            {
                JsonRttr::json arr = JsonRttr::json::array();
                for (const auto& sc : *scripts)
                {
                    JsonRttr::json s = JsonRttr::json::object();
                    s["name"] = sc.scriptName;
                    s["enabled"] = sc.enabled;

                    if (sc.instance)
                    {
                        rttr::instance inst = *sc.instance;
                        const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        s["props"] = JsonRttr::ToJsonObject(inst, t);
                    }

                    arr.push_back(s);
                }
                outEntity["Scripts"] = arr;
            }

            
            if (const auto* mat = world.GetComponent<MaterialComponent>(id); mat)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                MaterialComponent matCopy = *mat;
                matCopy.assetPath = NormalizePathToRelative(matCopy.assetPath);
                matCopy.albedoTexturePath = NormalizePathToRelative(matCopy.albedoTexturePath);
                
                rttr::instance inst = matCopy;
                outEntity["Material"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(id); skinned)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                SkinnedMeshComponent skinnedCopy = *skinned;
                skinnedCopy.instanceAssetPath = NormalizePathToRelative(skinnedCopy.instanceAssetPath);
                // meshAssetPath는 이미 상대 경로일 가능성이 높지만 안전을 위해 변환
                skinnedCopy.meshAssetPath = NormalizePathToRelative(skinnedCopy.meshAssetPath);
                
                rttr::instance inst = skinnedCopy;
                outEntity["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(id); anim)
            {
                rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                outEntity["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* ab = world.GetComponent<AnimBlueprintComponent>(id); ab)
            {
                AnimBlueprintComponent copy = *ab;
                copy.blueprintPath = NormalizePathToRelative(copy.blueprintPath);
                rttr::instance inst = copy;
                outEntity["AnimBlueprint"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* adv = world.GetComponent<AdvancedAnimComponent>(id); adv)
            {
                rttr::instance inst = const_cast<AdvancedAnimComponent&>(*adv);
                outEntity["AdvancedAnim"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* sockets = world.GetComponent<SocketComponent>(id); sockets)
            {
                JsonRttr::json arr = JsonRttr::json::array();
                for (const auto& s : sockets->sockets)
                {
                    JsonRttr::json js;
                    js["name"] = s.name;
                    js["parentBone"] = s.parentBone;
                    js["position"] = { s.position.x, s.position.y, s.position.z };
                    js["rotation"] = { s.rotation.x, s.rotation.y, s.rotation.z };
                    js["scale"] = { s.scale.x, s.scale.y, s.scale.z };
                    arr.push_back(js);
                }
                outEntity["Sockets"] = arr;
            }

            if (const auto* audio = world.GetComponent<AudioSourceComponent>(id); audio)
            {
                AudioSourceComponent copy = *audio;
                copy.soundPath = NormalizePathToRelative(copy.soundPath);
                rttr::instance inst = copy;
                outEntity["AudioSource"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* listener = world.GetComponent<AudioListenerComponent>(id); listener)
            {
                rttr::instance inst = const_cast<AudioListenerComponent&>(*listener);
                outEntity["AudioListener"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* sb = world.GetComponent<SoundBoxComponent>(id); sb)
            {
                SoundBoxComponent copy = *sb;
                copy.soundPath = NormalizePathToRelative(copy.soundPath);
                rttr::instance inst = copy;
                outEntity["SoundBox"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* cam = world.GetComponent<CameraComponent>(id); cam)
            {
                rttr::instance inst = const_cast<CameraComponent&>(*cam);
                outEntity["Camera"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* follow = world.GetComponent<CameraFollowComponent>(id); follow)
            {
                rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
                outEntity["CameraFollow"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(id); spring)
            {
                rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
                outEntity["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(id); lookAt)
            {
                rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
                outEntity["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* shake = world.GetComponent<CameraShakeComponent>(id); shake)
            {
                rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
                outEntity["CameraShake"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* blend = world.GetComponent<CameraBlendComponent>(id); blend)
            {
                rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
                outEntity["CameraBlend"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* input = world.GetComponent<CameraInputComponent>(id); input)
            {
                rttr::instance inst = const_cast<CameraInputComponent&>(*input);
                outEntity["CameraInput"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* point = world.GetComponent<PointLightComponent>(id); point)
            {
                rttr::instance inst = const_cast<PointLightComponent&>(*point);
                outEntity["PointLight"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* spot = world.GetComponent<SpotLightComponent>(id); spot)
            {
                rttr::instance inst = const_cast<SpotLightComponent&>(*spot);
                outEntity["SpotLight"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* rect = world.GetComponent<RectLightComponent>(id); rect)
            {
                rttr::instance inst = const_cast<RectLightComponent&>(*rect);
                outEntity["RectLight"] = JsonRttr::ToJsonObject(inst);
            }

            return true;
        }

        static bool ApplyEntity(World& world, const JsonRttr::json& e)
        {
            if (!e.is_object()) return false;

            const EntityId id = world.CreateEntity();

            const std::string name = e.value("name", std::string{});
            if (!name.empty())
                world.SetEntityName(id, name);

            // Transform
            TransformComponent& t = world.AddComponent<TransformComponent>(id);
            auto itT = e.find("Transform");
            if (itT != e.end())
            {
                rttr::instance inst = t;
                if (!JsonRttr::FromJsonObject(inst, *itT)) return false;
            }

            // Scripts (여러 개)
            auto itS = e.find("Scripts");
            if (itS != e.end() && itS->is_array())
            {
                for (const auto& s : *itS)
                {
                    if (!s.is_object()) continue;
                    const std::string name = s.value("name", std::string{});
                    if (name.empty()) continue;

                    ScriptComponent& sc = world.AddScript(id, name);
                    sc.enabled = s.value("enabled", true);

                    auto itP = s.find("props");
                    if (itP != s.end() && itP->is_object() && sc.instance)
                    {
                        rttr::instance inst = *sc.instance;
                        const rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        if (!JsonRttr::FromJsonObject(inst, *itP, t)) return false;
                        sc.defaultsApplied = true; // 씬이 값 주입 완료
                    }
                }
            }
            else
            {
                // Script (레거시 단일)
                auto itLegacy = e.find("Script");
                if (itLegacy != e.end() && itLegacy->is_object())
                {
                    const std::string name = itLegacy->value("name", std::string{});
                    const bool enabled = itLegacy->value("enabled", true);
                    if (!name.empty())
                    {
                        ScriptComponent& sc = world.AddScript(id, name);
                        sc.enabled = enabled;
                    }
                }
            }

            // Material
            auto itM = e.find("Material");
            if (itM != e.end() && itM->is_object())
            {
                MaterialComponent& mc = world.AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itM)) return false;
            }

            // SkinnedMesh
            auto itSM = e.find("SkinnedMesh");
            if (itSM != e.end() && itSM->is_object())
            {
                SkinnedMeshComponent tmp;
                rttr::instance instTmp = tmp;
                if (!JsonRttr::FromJsonObject(instTmp, *itSM)) return false;

                if (!tmp.meshAssetPath.empty())
                {
                    SkinnedMeshComponent& sm = world.AddComponent<SkinnedMeshComponent>(id, tmp.meshAssetPath);
                    sm.instanceAssetPath = tmp.instanceAssetPath;
                    sm.boneMatrices = &g_IdentityBone;
                    sm.boneCount = 1;
                }
            }

            // SkinnedAnimation (선택)
            auto itSA = e.find("SkinnedAnimation");
            if (itSA != e.end() && itSA->is_object())
            {
                SkinnedAnimationComponent& sa = world.AddComponent<SkinnedAnimationComponent>(id);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSA)) return false;
            }

            // AnimBlueprint (선택)
            auto itAB = e.find("AnimBlueprint");
            if (itAB != e.end() && itAB->is_object())
            {
                AnimBlueprintComponent& ab = world.AddComponent<AnimBlueprintComponent>(id);
                rttr::instance inst = ab;
                if (!JsonRttr::FromJsonObject(inst, *itAB)) return false;
            }

            // AdvancedAnim (선택)
            auto itAdv = e.find("AdvancedAnim");
            if (itAdv != e.end() && itAdv->is_object())
            {
                AdvancedAnimComponent& adv = world.AddComponent<AdvancedAnimComponent>(id);
                rttr::instance inst = adv;
                if (!JsonRttr::FromJsonObject(inst, *itAdv)) return false;
            }

            // Sockets (선택)
            auto itSock = e.find("Sockets");
            if (itSock != e.end() && itSock->is_array())
            {
                SocketComponent& sc = world.AddComponent<SocketComponent>(id);
                for (const auto& js : *itSock)
                {
                    if (!js.is_object()) continue;
                    SocketDef s;
                    s.name = js.value("name", "");
                    s.parentBone = js.value("parentBone", "");
                    if (js.contains("position") && js["position"].is_array() && js["position"].size() >= 3)
                    {
                        s.position.x = js["position"][0].get<float>();
                        s.position.y = js["position"][1].get<float>();
                        s.position.z = js["position"][2].get<float>();
                    }
                    if (js.contains("rotation") && js["rotation"].is_array() && js["rotation"].size() >= 3)
                    {
                        s.rotation.x = js["rotation"][0].get<float>();
                        s.rotation.y = js["rotation"][1].get<float>();
                        s.rotation.z = js["rotation"][2].get<float>();
                    }
                    if (js.contains("scale") && js["scale"].is_array() && js["scale"].size() >= 3)
                    {
                        s.scale.x = js["scale"][0].get<float>();
                        s.scale.y = js["scale"][1].get<float>();
                        s.scale.z = js["scale"][2].get<float>();
                    }
                    sc.sockets.push_back(std::move(s));
                }
            }

            // Camera (선택)
            auto itC = e.find("Camera");
            if (itC != e.end() && itC->is_object())
            {
                CameraComponent& cc = world.AddComponent<CameraComponent>(id);
                rttr::instance inst = cc;
                if (!JsonRttr::FromJsonObject(inst, *itC)) return false;
            }

            // CameraFollow (선택)
            auto itCF = e.find("CameraFollow");
            if (itCF != e.end() && itCF->is_object())
            {
                CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(id);
                rttr::instance inst = cf;
                if (!JsonRttr::FromJsonObject(inst, *itCF)) return false;
            }

            // CameraSpringArm (선택)
            auto itSpring = e.find("CameraSpringArm");
            if (itSpring != e.end() && itSpring->is_object())
            {
                CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(id);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSpring)) return false;
            }

            // CameraLookAt (선택)
            auto itLA = e.find("CameraLookAt");
            if (itLA != e.end() && itLA->is_object())
            {
                CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(id);
                rttr::instance inst = la;
                if (!JsonRttr::FromJsonObject(inst, *itLA)) return false;
            }

            // CameraShake (선택)
            auto itCS = e.find("CameraShake");
            if (itCS != e.end() && itCS->is_object())
            {
                CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(id);
                rttr::instance inst = cs;
                if (!JsonRttr::FromJsonObject(inst, *itCS)) return false;
            }

            // CameraBlend (선택)
            auto itCB = e.find("CameraBlend");
            if (itCB != e.end() && itCB->is_object())
            {
                CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(id);
                rttr::instance inst = cb;
                if (!JsonRttr::FromJsonObject(inst, *itCB)) return false;
            }

            // CameraInput (선택)
            auto itCI = e.find("CameraInput");
            if (itCI != e.end() && itCI->is_object())
            {
                CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(id);
                rttr::instance inst = ci;
                if (!JsonRttr::FromJsonObject(inst, *itCI)) return false;
            }

            // Point Light 선택
            auto itPL = e.find("PointLight");
            if (itPL != e.end() && itPL->is_object())
            {
                PointLightComponent& pl = world.AddComponent<PointLightComponent>(id);
                rttr::instance inst = pl;
                if (!JsonRttr::FromJsonObject(inst, *itPL)) return false;
            }

            // Spot Light 선택
            auto itSL = e.find("SpotLight");
            if (itSL != e.end() && itSL->is_object())
            {
                SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(id);
                rttr::instance inst = sl;
                if (!JsonRttr::FromJsonObject(inst, *itSL)) return false;
            }

            // Rect Light 선택
            auto itRL = e.find("RectLight");
            if (itRL != e.end() && itRL->is_object())
            {
                RectLightComponent& rl = world.AddComponent<RectLightComponent>(id);
                rttr::instance inst = rl;
                if (!JsonRttr::FromJsonObject(inst, *itRL)) return false;
            }

            // AudioSource (선택)
            auto itAS = e.find("AudioSource");
            if (itAS != e.end() && itAS->is_object())
            {
                AudioSourceComponent& asc = world.AddComponent<AudioSourceComponent>(id);
                rttr::instance inst = asc;
                if (!JsonRttr::FromJsonObject(inst, *itAS)) return false;
            }

            // AudioListener (선택)
            auto itAL = e.find("AudioListener");
            if (itAL != e.end() && itAL->is_object())
            {
                AudioListenerComponent& alc = world.AddComponent<AudioListenerComponent>(id);
                rttr::instance inst = alc;
                if (!JsonRttr::FromJsonObject(inst, *itAL)) return false;
            }

            // SoundBox (선택)
            auto itSB = e.find("SoundBox");
            if (itSB != e.end() && itSB->is_object())
            {
                SoundBoxComponent& sb = world.AddComponent<SoundBoxComponent>(id);
                rttr::instance inst = sb;
                if (!JsonRttr::FromJsonObject(inst, *itSB)) return false;
            }

            return true;
        }

        static bool LoadFromRoot(World& world, const JsonRttr::json& root)
        {
            auto itEntities = root.find("entities");
            if (itEntities == root.end() || !itEntities->is_array())
                return false;

            world.Clear();

            for (const auto& e : *itEntities)
                if (!ApplyEntity(world, e))
                    return false;

            return true;
        }

        static bool LoadFromBytes(World& world,
                                  const std::uint8_t* bytes,
                                  std::size_t size,
                                  const std::string& debugName)
        {
            if (!bytes || size == 0)
            {
                ALICE_LOG_ERRORF("[SceneFile] LoadFromBytes FAILED: empty buffer. name=\"%s\"", debugName.c_str());
                return false;
            }

            JsonRttr::json root;
            try
            {
                root = JsonRttr::json::parse(bytes, bytes + size);
            }
            catch (...)
            {
                ALICE_LOG_ERRORF("[SceneFile] JSON parse FAILED. name=\"%s\" bytes=%zu", debugName.c_str(), size);
                return false;
            }

            return LoadFromRoot(world, root);
        }
    }

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            root["entities"] = JsonRttr::json::array();

            const auto& transforms = world.GetComponents<TransformComponent>();
            for (const auto& [id, transform] : transforms)
            {
                (void)transform;
                JsonRttr::json e;
                if (!WriteEntity(e, world, id)) return false;
                root["entities"].push_back(e);
            }

            if (!JsonRttr::SaveJsonFile(path, root, 4)) return false;

            return true;
        }

        bool Load(World& world, const std::filesystem::path& path)
        {
            // 레거시 빈 씬(텍스트 헤더만 존재) 자동 처리:
            // - 예전 포맷으로 생성된 "# AliceRenderer scene" 파일은 JSON이 아니므로 파싱에 실패합니다.
            // - 이 경우 기본 엔티티 1개를 넣어 JSON 씬으로 즉시 업그레이드합니다.
            {
                std::ifstream ifs(path);
                if (!ifs.is_open()) return false;

                std::string firstLine;
                std::getline(ifs, firstLine);
                if (firstLine.rfind("# AliceRenderer scene", 0) == 0)
                {
                    world.Clear();
                    const EntityId e = world.CreateEntity();
                    world.AddComponent<TransformComponent>(e);
                    world.AddComponent<MaterialComponent>(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                    Save(world, path);
                    return true;
                }
            }

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root)) return false;
            return LoadFromRoot(world, root);
        }

        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath)
        {
            // (1) 에디터: 실제 파일
            // (2) 게임  : Assets/... 는 Metas/Chunks 로 패킹되어 있으므로, 바이트 로드 후 JSON 파싱
            const std::filesystem::path resolved = resources.Resolve(logicalPath);
            const std::string resolvedStr = resolved.generic_string();

            // Metas/Chunks 로 매핑된 경우: chunk 파일(.alice)이므로 직접 파일 파싱하면 안 됨
            if (resolved.extension() == ".alice")
            {
                auto sp = resources.LoadSharedBinaryAuto(logicalPath);
                if (!sp)
                {
                    ALICE_LOG_ERRORF("[SceneFile] LoadAuto FAILED: chunk load failed. logical=\"%s\" resolved=\"%s\"",
                                     logicalPath.generic_string().c_str(),
                                     resolvedStr.c_str());
                    return false;
                }

                ALICE_LOG_INFO("[SceneFile] LoadAuto: metas bytes loaded. logical=\"%s\" bytes=%zu resolved=\"%s\"",
                               logicalPath.generic_string().c_str(),
                               sp->size(),
                               resolvedStr.c_str());
                return LoadFromBytes(world, sp->data(), sp->size(), logicalPath.generic_string());
            }

            // 일반 파일: resolved 경로로 로드
            ALICE_LOG_INFO("[SceneFile] LoadAuto: file load. logical=\"%s\" resolved=\"%s\"",
                           logicalPath.generic_string().c_str(),
                           resolvedStr.c_str());
            return Load(world, resolved);
        }
    }
}
