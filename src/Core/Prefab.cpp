#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/Prefab.h"
#include "Core/ComponentRegistry.h"
#include "Core/JsonRttr.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "Core/SceneSerializationHelpers.h"

#include "Core/World.h"
#include "Components/ScriptComponent.h"
#include "Components/IDComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"

#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_TerrainHeightFieldComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DirectXMath.h>

namespace Alice
{
    namespace Prefab
    {
        namespace
        {
            // GUID 파싱 (EntityRef용, 실패 시 0 반환)
            static std::uint64_t ParseGuidAny(const JsonRttr::json& j)
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
            static bool IsEntityRefProp(const rttr::property& prop)
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

            // 프리팹 저장 대상 엔티티의 스크립트에서, 다른 엔티티를 참조하는 경우 경고
            static void WarnExternalEntityRefsInPrefab(const World& world, EntityId prefabRoot)
            {
                const auto* scripts = world.GetScripts(prefabRoot);
                if (!scripts) return;
                for (const auto& sc : *scripts)
                {
                    if (!sc.instance) continue;
                    rttr::instance inst = *sc.instance;
                    rttr::type type = rttr::type::get_by_name(sc.scriptName);
                    if (!type.is_valid()) type = inst.get_type();
                    for (auto prop : type.get_properties())
                    {
                        if (!IsEntityRefProp(prop)) continue;
                        rttr::variant v = prop.get_value(inst);
                        if (!v.is_valid() || !v.can_convert<EntityId>()) continue;
                        EntityId ref = v.get_value<EntityId>();
                        if (ref == InvalidEntityId || ref == prefabRoot) continue;
                        ALICE_LOG_WARN("Prefab: script '%s' has EntityRef to external entity (not prefab root). "
                                       "Ref will be Invalid when prefab is instantiated in another scene/project.",
                                       sc.scriptName.c_str());
                    }
                }
            }

            // 스크립트 props 저장 (EntityId → GUID 변환)
            static JsonRttr::json WriteScriptProps_WithEntityRefGuid(const World& world, const ScriptComponent& sc)
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
            static bool ApplyScriptProps_WithEntityRefGuid(World& world,
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

            // World의 모든 엔티티의 GUID→EntityId 맵 생성
            static std::unordered_map<std::uint64_t, EntityId> BuildGuidToEntityMap(const World& world)
            {
                std::unordered_map<std::uint64_t, EntityId> guidToEntity;
                const auto& idComponents = world.GetComponents<IDComponent>();
                for (const auto& [entityId, idComp] : idComponents)
                {
                    guidToEntity[idComp.guid] = entityId;
                }
                return guidToEntity;
            }
        }

        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return InvalidEntityId;

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root))
                return InvalidEntityId;
            if (!root.is_object())
                return InvalidEntityId;

            // 엔티티 생성
            EntityId entity = world.CreateEntity();

            const std::string name = root.value("name", std::string{});
            if (!name.empty())
                world.SetEntityName(entity, name);

            // Transform
            TransformComponent& t = world.AddComponent<TransformComponent>(entity);
            auto itT = root.find("Transform");
            if (itT != root.end() && itT->is_object())
            {
                rttr::instance inst = t;
                if (!JsonRttr::FromJsonObject(inst, *itT))
                    return InvalidEntityId;
            }

            // Scripts (여러 개)
            // 프리팹 로드 시 씬의 모든 엔티티 GUID 맵 생성 (프리팹 스크립트가 씬의 다른 엔티티 참조 가능)
            std::unordered_map<std::uint64_t, EntityId> guidToEntity = BuildGuidToEntityMap(world);

            auto itS = root.find("Scripts");
            if (itS != root.end() && itS->is_array())
            {
                for (const auto& s : *itS)
                {
                    if (!s.is_object()) continue;
                    const std::string sn = s.value("name", std::string{});
                    if (sn.empty()) continue;

                    ScriptComponent& sc = world.AddScript(entity, sn);
                    sc.enabled = s.value("enabled", true);

                    auto itP = s.find("props");
                    if (itP != s.end() && itP->is_object() && sc.instance)
                    {
                        JsonRttr::json propsCopy = *itP; // 복사본 생성 (EntityRef 키 제거용)
                        if (!ApplyScriptProps_WithEntityRefGuid(world, sc, propsCopy, guidToEntity))
                            return InvalidEntityId;
                        sc.defaultsApplied = true; // 프리팹이 값 주입 완료
                    }
                }
            }

            // Material
            auto itM = root.find("Material");
            if (itM != root.end() && itM->is_object())
            {
                MaterialComponent& mc = world.AddComponent<MaterialComponent>(entity, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itM))
                    return InvalidEntityId;
            }

            // SkinnedMesh
            auto itSM = root.find("SkinnedMesh");
            if (itSM != root.end() && itSM->is_object())
            {
                SkinnedMeshComponent tmp;
                rttr::instance instTmp = tmp;
                if (!JsonRttr::FromJsonObject(instTmp, *itSM))
                    return InvalidEntityId;

                if (!tmp.meshAssetPath.empty())
                {
                    SkinnedMeshComponent& sm = world.AddComponent<SkinnedMeshComponent>(entity, tmp.meshAssetPath);
                    sm.instanceAssetPath = tmp.instanceAssetPath;
                    sm.boneMatrices = &SceneSerializationHelpers::g_IdentityBone;
                    sm.boneCount = 1;
                }
            }

            // SkinnedAnimation
            auto itSA = root.find("SkinnedAnimation");
            if (itSA != root.end() && itSA->is_object())
            {
                SkinnedAnimationComponent& sa = world.AddComponent<SkinnedAnimationComponent>(entity);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSA))
                    return InvalidEntityId;
            }

            // Camera
            auto itC = root.find("Camera");
            if (itC != root.end() && itC->is_object())
            {
                CameraComponent& cc = world.AddComponent<CameraComponent>(entity);
                rttr::instance inst = cc;
                if (!JsonRttr::FromJsonObject(inst, *itC))
                    return InvalidEntityId;
            }

            // CameraFollow
            auto itCF = root.find("CameraFollow");
            if (itCF != root.end() && itCF->is_object())
            {
                CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(entity);
                rttr::instance inst = cf;
                if (!JsonRttr::FromJsonObject(inst, *itCF))
                    return InvalidEntityId;
            }

            // CameraSpringArm
            auto itSpring = root.find("CameraSpringArm");
            if (itSpring != root.end() && itSpring->is_object())
            {
                CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(entity);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSpring))
                    return InvalidEntityId;
            }

            // CameraLookAt
            auto itLA = root.find("CameraLookAt");
            if (itLA != root.end() && itLA->is_object())
            {
                CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(entity);
                rttr::instance inst = la;
                if (!JsonRttr::FromJsonObject(inst, *itLA))
                    return InvalidEntityId;
            }

            // CameraShake
            auto itCS = root.find("CameraShake");
            if (itCS != root.end() && itCS->is_object())
            {
                CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(entity);
                rttr::instance inst = cs;
                if (!JsonRttr::FromJsonObject(inst, *itCS))
                    return InvalidEntityId;
            }

            // CameraBlend
            auto itCB = root.find("CameraBlend");
            if (itCB != root.end() && itCB->is_object())
            {
                CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(entity);
                rttr::instance inst = cb;
                if (!JsonRttr::FromJsonObject(inst, *itCB))
                    return InvalidEntityId;
            }

            // CameraInput
            auto itCI = root.find("CameraInput");
            if (itCI != root.end() && itCI->is_object())
            {
                CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(entity);
                rttr::instance inst = ci;
                if (!JsonRttr::FromJsonObject(inst, *itCI))
                    return InvalidEntityId;
            }

            // Point Light
            auto itPL = root.find("PointLight");
            if (itPL != root.end() && itPL->is_object())
            {
                PointLightComponent& pl = world.AddComponent<PointLightComponent>(entity);
                rttr::instance inst = pl;
                if (!JsonRttr::FromJsonObject(inst, *itPL))
                    return InvalidEntityId;
            }

            // Spot Light
            auto itSL = root.find("SpotLight");
            if (itSL != root.end() && itSL->is_object())
            {
                SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(entity);
                rttr::instance inst = sl;
                if (!JsonRttr::FromJsonObject(inst, *itSL))
                    return InvalidEntityId;
            }

            // Rect Light
            auto itRL = root.find("RectLight");
            if (itRL != root.end() && itRL->is_object())
            {
                RectLightComponent& rl = world.AddComponent<RectLightComponent>(entity);
                rttr::instance inst = rl;
                if (!JsonRttr::FromJsonObject(inst, *itRL))
                    return InvalidEntityId;
            }

            // PhysX Components
            auto itRB = root.find("RigidBody");
            if (itRB != root.end() && itRB->is_object())
            {
                Phy_RigidBodyComponent& rb = world.AddComponent<Phy_RigidBodyComponent>(entity);
                rttr::instance inst = rb;
                if (!JsonRttr::FromJsonObject(inst, *itRB))
                    return InvalidEntityId;
            }

            auto itCollider = root.find("Collider");
            if (itCollider != root.end() && itCollider->is_object())
            {
                Phy_ColliderComponent& col = world.AddComponent<Phy_ColliderComponent>(entity);
                rttr::instance inst = col;
                if (!JsonRttr::FromJsonObject(inst, *itCollider))
                    return InvalidEntityId;
            }

            auto itMeshCollider = root.find("MeshCollider");
            if (itMeshCollider != root.end() && itMeshCollider->is_object())
            {
                Phy_MeshColliderComponent& mc = world.AddComponent<Phy_MeshColliderComponent>(entity);
                rttr::instance inst = mc;
                if (!JsonRttr::FromJsonObject(inst, *itMeshCollider))
                    return InvalidEntityId;
            }

            auto itCCT = root.find("CharacterController");
            if (itCCT != root.end() && itCCT->is_object())
            {
                Phy_CCTComponent& cct = world.AddComponent<Phy_CCTComponent>(entity);
                rttr::instance inst = cct;
                if (!JsonRttr::FromJsonObject(inst, *itCCT))
                    return InvalidEntityId;
            }

            auto itTerrain = root.find("TerrainHeightField");
            if (itTerrain != root.end() && itTerrain->is_object())
            {
                Phy_TerrainHeightFieldComponent& terrain = world.AddComponent<Phy_TerrainHeightFieldComponent>(entity);
                rttr::instance inst = terrain;
                if (!JsonRttr::FromJsonObject(inst, *itTerrain))
                    return InvalidEntityId;
            }

            auto itJoint = root.find("Joint");
            if (itJoint != root.end() && itJoint->is_object())
            {
                Phy_JointComponent& joint = world.AddComponent<Phy_JointComponent>(entity);
                rttr::instance inst = joint;
                if (!JsonRttr::FromJsonObject(inst, *itJoint))
                    return InvalidEntityId;
            }

            auto itPhysicsSettings = root.find("PhysicsSceneSettings");
            if (itPhysicsSettings != root.end() && itPhysicsSettings->is_object())
            {
                Phy_SettingsComponent& ps = world.AddComponent<Phy_SettingsComponent>(entity);
                // 수동 역직렬화 사용 (중첩 배열 보장)
                if (!SceneSerializationHelpers::LoadPhysicsSceneSettings(ps, *itPhysicsSettings))
                    return InvalidEntityId;
            }

            return entity;
        }

        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path)
        {
            if (entity == InvalidEntityId) return false;

            const TransformComponent* t = world.GetComponent<TransformComponent>(entity);
            if (!t)
                return false;

            WarnExternalEntityRefsInPrefab(world, entity);

            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;

            const std::string name = world.GetEntityName(entity);
            if (!name.empty())
                root["name"] = name;

            // Transform
            {
                rttr::instance inst = const_cast<TransformComponent&>(*t);
                root["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            // Scripts
            if (const auto* scripts = world.GetScripts(entity); scripts && !scripts->empty())
            {
                JsonRttr::json arr = JsonRttr::json::array();
                for (const auto& sc : *scripts)
                {
                    JsonRttr::json s = JsonRttr::json::object();
                    s["name"] = sc.scriptName;
                    s["enabled"] = sc.enabled;

                    if (sc.instance)
                    {
                        s["props"] = WriteScriptProps_WithEntityRefGuid(world, sc);
                    }

                    arr.push_back(s);
                }
                root["Scripts"] = arr;
            }

            // Material
            if (const auto* mat = world.GetComponent<MaterialComponent>(entity); mat)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                MaterialComponent matCopy = *mat;
                matCopy.assetPath = SceneSerializationHelpers::NormalizePathToRelative(matCopy.assetPath);
                matCopy.albedoTexturePath = SceneSerializationHelpers::NormalizePathToRelative(matCopy.albedoTexturePath);
                
                rttr::instance inst = matCopy;
                root["Material"] = JsonRttr::ToJsonObject(inst);
            }

            // SkinnedMesh
            if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entity); skinned)
            {
                // 경로를 상대 경로로 변환하기 위해 복사본 생성
                SkinnedMeshComponent skinnedCopy = *skinned;
                skinnedCopy.instanceAssetPath = SceneSerializationHelpers::NormalizePathToRelative(skinnedCopy.instanceAssetPath);
                skinnedCopy.meshAssetPath = SceneSerializationHelpers::NormalizePathToRelative(skinnedCopy.meshAssetPath);
                
                rttr::instance inst = skinnedCopy;
                root["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
            }

            // SkinnedAnimation
            if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(entity); anim)
            {
                rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                root["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            // Camera
            if (const auto* cam = world.GetComponent<CameraComponent>(entity); cam)
            {
                rttr::instance inst = const_cast<CameraComponent&>(*cam);
                root["Camera"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraFollow
            if (const auto* follow = world.GetComponent<CameraFollowComponent>(entity); follow)
            {
                rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
                root["CameraFollow"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraSpringArm
            if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(entity); spring)
            {
                rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
                root["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraLookAt
            if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(entity); lookAt)
            {
                rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
                root["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraShake
            if (const auto* shake = world.GetComponent<CameraShakeComponent>(entity); shake)
            {
                rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
                root["CameraShake"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraBlend
            if (const auto* blend = world.GetComponent<CameraBlendComponent>(entity); blend)
            {
                rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
                root["CameraBlend"] = JsonRttr::ToJsonObject(inst);
            }

            // CameraInput
            if (const auto* input = world.GetComponent<CameraInputComponent>(entity); input)
            {
                rttr::instance inst = const_cast<CameraInputComponent&>(*input);
                root["CameraInput"] = JsonRttr::ToJsonObject(inst);
            }

            // Point Light
            if (const auto* point = world.GetComponent<PointLightComponent>(entity); point)
            {
                rttr::instance inst = const_cast<PointLightComponent&>(*point);
                root["PointLight"] = JsonRttr::ToJsonObject(inst);
            }

            // Spot Light
            if (const auto* spot = world.GetComponent<SpotLightComponent>(entity); spot)
            {
                rttr::instance inst = const_cast<SpotLightComponent&>(*spot);
                root["SpotLight"] = JsonRttr::ToJsonObject(inst);
            }

            // Rect Light
            if (const auto* rect = world.GetComponent<RectLightComponent>(entity); rect)
            {
                rttr::instance inst = const_cast<RectLightComponent&>(*rect);
                root["RectLight"] = JsonRttr::ToJsonObject(inst);
            }

            // PhysX Components
            if (const auto* rigidBody = world.GetComponent<Phy_RigidBodyComponent>(entity); rigidBody)
            {
                rttr::instance inst = const_cast<Phy_RigidBodyComponent&>(*rigidBody);
                root["RigidBody"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* collider = world.GetComponent<Phy_ColliderComponent>(entity); collider)
            {
                rttr::instance inst = const_cast<Phy_ColliderComponent&>(*collider);
                root["Collider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(entity); meshCollider)
            {
                rttr::instance inst = const_cast<Phy_MeshColliderComponent&>(*meshCollider);
                root["MeshCollider"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* cct = world.GetComponent<Phy_CCTComponent>(entity); cct)
            {
                rttr::instance inst = const_cast<Phy_CCTComponent&>(*cct);
                root["CharacterController"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(entity); terrain)
            {
                rttr::instance inst = const_cast<Phy_TerrainHeightFieldComponent&>(*terrain);
                root["TerrainHeightField"] = JsonRttr::ToJsonObject(inst);
            }

            if (const auto* physicsSettings = world.GetComponent<Phy_SettingsComponent>(entity); physicsSettings)
            {
                // 수동 직렬화 사용 (중첩 배열 보장)
                root["PhysicsSceneSettings"] = SceneSerializationHelpers::WritePhysicsSceneSettings(*physicsSettings);
            }

            if (const auto* joint = world.GetComponent<Phy_JointComponent>(entity); joint)
            {
                rttr::instance inst = const_cast<Phy_JointComponent&>(*joint);
                root["Joint"] = JsonRttr::ToJsonObject(inst);
            }

            return JsonRttr::SaveJsonFile(path, root, 4);
        }
    }
}



