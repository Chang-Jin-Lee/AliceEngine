#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/SceneFile.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Core/ThreadSafety.h"
#include "Core/EditorComponentRegistry.h"
#include "Components/IDComponent.h"
#include <random>

#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/World.h"
#include "Components/ScriptComponent.h"
#include "Components/ComputeEffectComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"


#include "UI/UIWorldManager.h"

#include <wrl/client.h>
#include <dxgi.h>
#include <dxgi1_3.h>

namespace Alice
{
    // SceneFile 내부 헬퍼 함수들 (ComponentRegistry에서도 사용)
    namespace SceneFileInternal
    {
        // GUID 생성 함수
        std::uint64_t NewGuid()
        {
            static std::mt19937_64 rng{ std::random_device{}() };
            static std::uniform_int_distribution<std::uint64_t> dist;
            return dist(rng);
        }

        // GUID 파싱 (JSON string 또는 number)
        std::uint64_t ParseGuid(const JsonRttr::json& j)
        {
            if (j.is_string())
            {
                try
                {
                    return std::stoull(j.get<std::string>());
                }
                catch (...)
                {
                    return NewGuid();
                }
            }
            else if (j.is_number_unsigned())
            {
                return j.get<std::uint64_t>();
            }
            return NewGuid();
        }

        // GUID 파싱 (EntityRef용, 실패 시 0 반환)
        std::uint64_t ParseGuidAny(const JsonRttr::json& j)
        {
            if (j.is_string())
            {
                try
                {
                    return std::stoull(j.get<std::string>());
                }
                catch (...)
                {
                    return 0;
                }
            }
            if (j.is_number_unsigned())
            {
                return j.get<std::uint64_t>();
            }
            return 0;
        }

        // 프로퍼티가 EntityId/EntityRef인지 확인
        bool IsEntityRefProp(const rttr::property& prop)
        {
            const rttr::type pt = prop.get_type();
            const std::string tn = pt.get_name().to_string();
            if (pt == rttr::type::get<EntityId>())
                return true;
            if (tn == "EntityId" || tn == "Alice::EntityId")
                return true;
            if (prop.get_metadata("EntityRef").is_valid())
                return true;
            return false;
        }

        // 스크립트 props 저장 (EntityId → GUID 변환)
        JsonRttr::json WriteScriptProps_WithEntityRefGuid(const World& world, const ScriptComponent& sc)
        {
            JsonRttr::json out = JsonRttr::json::object();
            if (!sc.instance)
                return out;

            rttr::instance inst = *sc.instance;
            rttr::type type = rttr::type::get_by_name(sc.scriptName);
            if (!type.is_valid())
                type = inst.get_type();

            for (auto prop : type.get_properties())
            {
                const std::string key = prop.get_name().to_string();
                rttr::variant v = prop.get_value(inst);
                if (!v.is_valid())
                    continue;

                if (IsEntityRefProp(prop))
                {
                    EntityId ref = InvalidEntityId;
                    if (v.can_convert<EntityId>())
                        ref = v.get_value<EntityId>();

                    if (ref == InvalidEntityId)
                    {
                        out[key] = nullptr;
                    }
                    else
                    {
                        if (const auto* idc = world.GetComponent<IDComponent>(ref))
                            out[key] = std::to_string(idc->guid);
                        else
                            out[key] = nullptr;
                    }
                }
                else
                {
                    out[key] = JsonRttr::ToJsonVariant(v);
                }
            }
            return out;
        }

        // 스크립트 props 로드 (GUID → EntityId 변환)
        bool ApplyScriptProps_WithEntityRefGuid(World& world,
                                                      ScriptComponent& sc,
                                                      JsonRttr::json props,
                                                      const std::unordered_map<std::uint64_t, EntityId>& guidToEntity)
        {
            if (!sc.instance)
                return true;

            rttr::instance inst = *sc.instance;
            rttr::type type = rttr::type::get_by_name(sc.scriptName);
            if (!type.is_valid())
                type = inst.get_type();

            // EntityRef 프로퍼티를 먼저 처리
            for (auto prop : type.get_properties())
            {
                if (!IsEntityRefProp(prop))
                    continue;

                const std::string key = prop.get_name().to_string();
                auto it = props.find(key);
                if (it == props.end())
                    continue;

                std::uint64_t guid = ParseGuidAny(*it);
                EntityId ref = InvalidEntityId;
                if (guid != 0)
                {
                    auto mit = guidToEntity.find(guid);
                    if (mit != guidToEntity.end())
                        ref = mit->second;
                }
                prop.set_value(inst, ref);
                props.erase(it);
            }

            // 나머지 props는 FromJsonObject로 처리
            return JsonRttr::FromJsonObject(inst, props, type);
        }

        bool WriteEntity(JsonRttr::json& outEntity, const World& world, EntityId id)
        {
            outEntity = JsonRttr::json::object();
            // 엔티티 id는 저장하지 않음 (로드 시 재사용되지 않으므로 혼란 방지)

            const std::string name = world.GetEntityName(id);
            if (!name.empty())
                outEntity["name"] = name;
            
            // GUID 저장
            if (const auto* idComp = world.GetComponent<IDComponent>(id); idComp)
            {
                // uint64는 JSON에서 string으로 저장 (호환성)
                outEntity["guid"] = std::to_string(idComp->guid);
            }
            
            // Parent 관계 저장 (GUID 기반)
            EntityId parentId = world.GetParent(id);
            if (parentId != InvalidEntityId)
            {
                if (const auto* parentIdComp = world.GetComponent<IDComponent>(parentId); parentIdComp)
                {
                    outEntity["_parentGuid"] = std::to_string(parentIdComp->guid);
                }
            }
            
            // Transform은 필수 컴포넌트이므로 특별 처리
            if (const auto* transform = world.GetComponent<TransformComponent>(id); transform)
            {
                rttr::instance inst = const_cast<TransformComponent&>(*transform);
                outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            // Scripts는 특별 처리 (EntityRef GUID 변환 필요)
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
                        s["props"] = SceneFileInternal::WriteScriptProps_WithEntityRefGuid(world, sc);
                    }

                    arr.push_back(s);
                }
                outEntity["Scripts"] = arr;
            }

            // 등록된 컴포넌트 자동 저장 (registry loop)
            auto& reg = EditorComponentRegistry::Get();
            for (const auto& d : reg.All())
            {
                // Transform과 Scripts는 이미 처리했으므로 스킵
                if (d.fileKey == "Transform" || d.fileKey == "Scripts")
                    continue;

                if (!d.has(world, id))
                    continue;

                // TODO: AudioSource/Listener/SoundBox 등도 EditorComponentRegistry에 Register해서 registry 기반 저장/로드로 통일할 것.
                JsonRttr::json c = d.serialize ? d.serialize(world, id) : JsonRttr::json();
                if (!c.is_null())
                    outEntity[d.fileKey] = std::move(c);
            }

            return true;
        }

        bool ApplyEntity(World& world, const JsonRttr::json& e, std::unordered_map<std::uint64_t, EntityId>& guidToEntity, std::vector<std::pair<EntityId, std::uint64_t>>& pendingParents, EntityId* outCreatedId = nullptr)
        {
            if (!e.is_object()) return false;

            const EntityId id = world.CreateEntity();
            if (outCreatedId) *outCreatedId = id;

            const std::string name = e.value("name", std::string{});
            if (!name.empty())
                world.SetEntityName(id, name);

            // IDComponent: GUID 로드 또는 생성
            auto* idComp = world.GetComponent<IDComponent>(id);
            if (!idComp)
            {
                // IDComponent가 없으면 생성
                idComp = &world.AddComponent<IDComponent>(id);
            }
            
            if (auto itGuid = e.find("guid"); itGuid != e.end())
            {
                auto parsed = SceneFileInternal::ParseGuid(*itGuid);
                if (parsed != 0) idComp->guid = parsed; // 실패면 덮어쓰지 않기
                else idComp->guid = SceneFileInternal::NewGuid(); // ParseGuid 실패 시 새 GUID 생성
            }
            else
            {
                idComp->guid = SceneFileInternal::NewGuid();
            }
            guidToEntity[idComp->guid] = id;

            // Parent GUID 저장 (나중에 연결)
            if (auto itParentGuid = e.find("_parentGuid"); itParentGuid != e.end())
            {
                std::uint64_t parentGuid = SceneFileInternal::ParseGuid(*itParentGuid);
                pendingParents.push_back({ id, parentGuid });
            }

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
                        JsonRttr::json propsCopy = *itP; // 복사본 생성 (EntityRef 키 제거용)
                        if (!SceneFileInternal::ApplyScriptProps_WithEntityRefGuid(world, sc, propsCopy, guidToEntity))
                            return false;
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

            // 등록된 컴포넌트 자동 로드 (registry loop)
            auto& reg = EditorComponentRegistry::Get();
            for (const auto& d : reg.All())
            {
                // Transform과 Scripts는 이미 처리했으므로 스킵
                if (d.fileKey == "Transform" || d.fileKey == "Scripts")
                    continue;

                auto it = e.find(d.fileKey);
                if (it == e.end() || it->is_null())
                    continue;

                if (d.deserialize)
                {
                    if (!d.deserialize(world, id, *it))
                        return false;
                }
                else
                {
                    // fallback: add + FromJsonObject
                    if (!d.has(world, id))
                    {
                        if (d.add) d.add(world, id);
                    }
                    rttr::instance inst = d.getInstance(world, id);
                    if (!inst.is_valid())
                        return false;
                    if (!JsonRttr::FromJsonObject(inst, *it))
                        return false;
                }
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

        bool LoadFromRoot(World& world, const JsonRttr::json& root)
        {
            auto itEntities = root.find("entities");
            if (itEntities == root.end() || !itEntities->is_array())
                return false;

            world.Clear();

            // 2-pass 로드: GUID 기반 parent 복원
            std::unordered_map<std::uint64_t, EntityId> guidToEntity;
            std::vector<std::pair<EntityId, std::uint64_t>> pendingParents;

            // PASS 1: 엔티티 생성 + 컴포넌트 복원 + GUID 맵 생성
            for (const auto& e : *itEntities)
            {
                if (!SceneFileInternal::ApplyEntity(world, e, guidToEntity, pendingParents))
                    return false;
            }

            // PASS 2: parent 연결 (keepWorld=false, 로드이므로)
            for (const auto& [childId, parentGuid] : pendingParents)
            {
                auto it = guidToEntity.find(parentGuid);
                if (it != guidToEntity.end())
                {
                    world.SetParent(childId, it->second, false);
                }
            }

            return true;
        }

        bool LoadFromBytes(World& world,
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

            return SceneFileInternal::LoadFromRoot(world, root);
        }
    }

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            
            // Scene 이름 저장 (파일명 기반, 확장자 제외)
            std::string sceneName = path.stem().string();
            root["sceneName"] = sceneName;
            
            root["entities"] = JsonRttr::json::array();

            const auto& transforms = world.GetComponents<TransformComponent>();
            for (const auto& [id, transform] : transforms)
            {
                (void)transform;
                JsonRttr::json e;
                if (!SceneFileInternal::WriteEntity(e, world, id)) return false;
                root["entities"].push_back(e);
            }

            if (!JsonRttr::SaveJsonFile(path, root, 4)) return false;

            return true;
        }

        bool SaveToJsonString(const World& world, std::string& out)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            root["entities"] = JsonRttr::json::array();

            const auto& transforms = world.GetComponents<TransformComponent>();
            for (const auto& [id, transform] : transforms)
            {
                (void)transform;
                JsonRttr::json e;
                if (!SceneFileInternal::WriteEntity(e, world, id)) return false;
                root["entities"].push_back(e);
            }

            out = root.dump(4);
            return true;
        }

        bool LoadFromJsonString(World& world, const std::string& json)
        {
            ThreadSafety::AssertMainThread();
            JsonRttr::json root;
            try
            {
                root = JsonRttr::json::parse(json);
            }
            catch (...)
            {
                ALICE_LOG_ERRORF("[SceneFile] LoadFromJsonString: JSON parse failed.");
                return false;
            }
            world.Clear();
            return SceneFileInternal::LoadFromRoot(world, root);
        }

        bool Load(World& world, const std::filesystem::path& path)
        {
            ThreadSafety::AssertMainThread();
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
            return SceneFileInternal::LoadFromRoot(world, root);
        }

        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath, UIWorldManager* uiWorldManager)
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
                const bool ok = SceneFileInternal::LoadFromBytes(world, sp->data(), sp->size(), logicalPath.generic_string());
                if (!ok) return false;
                if (uiWorldManager) return uiWorldManager->LoadUI(logicalPath, &resources);
                return true;
            }

            // 일반 파일: resolved 경로로 로드
            ALICE_LOG_INFO("[SceneFile] LoadAuto: file load. logical=\"%s\" resolved=\"%s\"",
                           logicalPath.generic_string().c_str(),
                           resolvedStr.c_str());
            return Load(world, resolved, uiWorldManager);
        }
        
        bool Save(const World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager)
        {
            // World 저장
            if (!Save(world, path)) return false;
            
            // UI 저장 (있는 경우)
            if (uiWorldManager)
            {
                if (!uiWorldManager->SaveUI(path)) return false;
            }
            
            return true;
        }
        
        bool Load(World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager)
        {
            // World 로드
            if (!Load(world, path)) return false;
            
            // UI 로드 (있는 경우)
            if (uiWorldManager)
            {
                ALICE_LOG_INFO("[SceneFile] Load: Calling LoadUI for scene: %s", path.generic_string().c_str());
                if (!uiWorldManager->LoadUI(path, nullptr))
                {
                    ALICE_LOG_ERRORF("[SceneFile] Load: LoadUI failed for: %s", path.generic_string().c_str());
                    return false;
                }
            }
            else
            {
                ALICE_LOG_WARN("[SceneFile] Load: uiWorldManager is null, skipping UI load");
            }
            
            return true;
        }

        bool WriteEntity(JsonRttr::json& out, const World& world, EntityId id)
        {
            return SceneFileInternal::WriteEntity(out, world, id);
        }

        bool ApplyEntity(World& world,
                        const JsonRttr::json& e,
                        std::unordered_map<std::uint64_t, EntityId>& guidToEntity,
                        std::vector<std::pair<EntityId, std::uint64_t>>& pendingParents,
                        EntityId* outCreatedId)
        {
            return SceneFileInternal::ApplyEntity(world, e, guidToEntity, pendingParents, outCreatedId);
        }
    }
}
