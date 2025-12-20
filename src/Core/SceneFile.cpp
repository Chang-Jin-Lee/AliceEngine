#include "Core/SceneFile.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함
#include "Core/JsonRttr.h"

#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/World.h"
#include "Core/Script.h"
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

        static bool WriteEntity(JsonRttr::json& outEntity, const World& world, EntityId id)
        {
            outEntity = JsonRttr::json::object();
            outEntity["id"] = static_cast<std::uint32_t>(id);

            
            if (const auto* transform = world.GetTransform(id); transform)
            {
                rttr::instance inst = const_cast<TransformComponent&>(*transform);
                outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* script = world.GetScript(id); script)
            {
                JsonRttr::json s = JsonRttr::json::object();
                s["name"] = script->scriptName;
                s["enabled"] = script->enabled;
                outEntity["Script"] = s;
            }

            
            if (const auto* mat = world.GetMaterial(id); mat)
            {
                rttr::instance inst = const_cast<MaterialComponent&>(*mat);
                outEntity["Material"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* skinned = world.GetSkinnedMesh(id); skinned)
            {
                rttr::instance inst = const_cast<SkinnedMeshComponent&>(*skinned);
                outEntity["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
            }

            
            if (const auto* anim = world.GetSkinnedAnimation(id); anim)
            {
                rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
                outEntity["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
            }

            return true;
        }

        static bool ApplyEntity(World& world, const JsonRttr::json& e)
        {
            if (!e.is_object()) return false;

            const EntityId id = world.CreateEntity();

            // Transform
            TransformComponent& t = world.AddTransform(id);
            auto itT = e.find("Transform");
            if (itT != e.end())
            {
                rttr::instance inst = t;
                if (!JsonRttr::FromJsonObject(inst, *itT)) return false;
            }

            // Script
            auto itS = e.find("Script");
            if (itS != e.end() && itS->is_object())
            {
                const std::string name = itS->value("name", std::string{});
                const bool enabled = itS->value("enabled", true);
                if (!name.empty())
                {
                    ScriptComponent& sc = world.AddScript(id, name);
                    sc.enabled = enabled;
                }
            }

            // Material
            auto itM = e.find("Material");
            if (itM != e.end() && itM->is_object())
            {
                MaterialComponent& mc = world.AddMaterial(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f), {});
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
                    SkinnedMeshComponent& sm = world.AddSkinnedMesh(id, tmp.meshAssetPath);
                    sm.instanceAssetPath = tmp.instanceAssetPath;
                    sm.boneMatrices = &g_IdentityBone;
                    sm.boneCount = 1;
                }
            }

            // SkinnedAnimation (선택)
            auto itSA = e.find("SkinnedAnimation");
            if (itSA != e.end() && itSA->is_object())
            {
                SkinnedAnimationComponent& sa = world.AddSkinnedAnimation(id);
                rttr::instance inst = sa;
                if (!JsonRttr::FromJsonObject(inst, *itSA)) return false;
            }

            return true;
        }
    }

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
        {
            JsonRttr::json root = JsonRttr::json::object();
            root["version"] = 1;
            root["entities"] = JsonRttr::json::array();

            const auto& transforms = world.GetTransforms();
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
                    world.AddTransform(e);
                    world.AddMaterial(e, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f), {});
                    Save(world, path);
                    return true;
                }
            }

            JsonRttr::json root;
            if (!JsonRttr::LoadJsonFile(path, root)) return false;

            auto itEntities = root.find("entities");
            if (itEntities == root.end() || !itEntities->is_array()) return false;

            // 현재 월드 비우기
            world.Clear();

            for (const auto& e : *itEntities)
                if (!ApplyEntity(world, e)) 
                    return false;

            return true;
        }
    }
}
