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
