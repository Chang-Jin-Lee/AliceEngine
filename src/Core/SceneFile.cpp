#include "Core/SceneFile.h"

#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/World.h"
#include "Core/Script.h"

namespace Alice
{
    namespace
    {
        inline void Trim(std::string& s)
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
        }

        // 스키닝 메시가 아직 애니메이션 시스템과 연결되지 않았을 때 사용할
        // 1개짜리 항등 본 팔레트입니다. (정적인 메시처럼 렌더링되도록 함)
        static DirectX::XMFLOAT4X4 g_IdentityBone(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1);
    }

    namespace SceneFile
    {
        bool Save(const World& world, const std::filesystem::path& path)
        {
            auto parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent))
            {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }

            std::ofstream ofs(path);
            if (!ofs.is_open())
                return false;

            ofs << "# AliceRenderer scene\n";

            const auto& transforms = world.GetTransforms();
            for (const auto& [id, transform] : transforms)
            {
                const ScriptComponent*     script = world.GetScript(id);
                const MaterialComponent*   mat    = world.GetMaterial(id);
                const SkinnedMeshComponent* skinned = world.GetSkinnedMesh(id);

                ofs << "entity: " << static_cast<std::uint32_t>(id) << "\n";
                ofs << "position: "
                    << transform.position.x << " "
                    << transform.position.y << " "
                    << transform.position.z << "\n";
                ofs << "rotation: "
                    << transform.rotation.x << " "
                    << transform.rotation.y << " "
                    << transform.rotation.z << "\n";
                ofs << "scale: "
                    << transform.scale.x << " "
                    << transform.scale.y << " "
                    << transform.scale.z << "\n";

                ofs << "script: ";
                if (script)
                    ofs << script->scriptName;
                ofs << "\n";

                ofs << "material_color: ";
                if (mat)
                {
                    ofs << mat->color.x << " "
                        << mat->color.y << " "
                        << mat->color.z;
                }
                ofs << "\n";

                ofs << "material_asset: ";
                if (mat && !mat->assetPath.empty())
                    ofs << mat->assetPath;
                ofs << "\n";

                // 추가 머티리얼 파라미터 (선택 사항)
                ofs << "material_roughness: ";
                if (mat)
                    ofs << mat->roughness;
                ofs << "\n";

                ofs << "material_metalness: ";
                if (mat)
                    ofs << mat->metalness;
                ofs << "\n";

                ofs << "material_albedoTex: ";
                if (mat && !mat->albedoTexturePath.empty())
                    ofs << mat->albedoTexturePath;
                ofs << "\n";

                // SkinnedMesh 정보 (FBX 인스턴스)
                ofs << "skinned_mesh: ";
                if (skinned && !skinned->meshAssetPath.empty())
                    ofs << skinned->meshAssetPath;
                ofs << "\n";

                ofs << "skinned_instance: ";
                if (skinned && !skinned->instanceAssetPath.empty())
                    ofs << skinned->instanceAssetPath;
                ofs << "\n";

                ofs << "\n";
            }

            return true;
        }

        bool Load(World& world, const std::filesystem::path& path)
        {
            // 1. 현재 프로그램의 작업 디렉토리(CWD) 확인
            std::filesystem::path cwd = std::filesystem::current_path();

            // 2. 입력된 상대 경로가 실제로 가리키는 절대 경로 확인
            std::filesystem::path absPath = std::filesystem::absolute(path);

            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            // 현재 월드 비우기
            {
                std::vector<EntityId> ids;
                ids.reserve(world.GetTransforms().size());
                for (const auto& [id, _] : world.GetTransforms())
                {
                    ids.push_back(id);
                }
                for (EntityId id : ids)
                {
                    world.DestroyEntity(id);
                }
            }

            // 한 엔티티에 대한 임시 버퍼
            DirectX::XMFLOAT3 position { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 rotation { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale    { 1.0f, 1.0f, 1.0f };
            std::string       scriptName;
            DirectX::XMFLOAT3 materialColor { 0.7f, 0.7f, 0.7f };
            bool              hasMaterialColor = false;
            float             materialRoughness = 0.5f;
            float             materialMetalness = 0.0f;
            std::string       materialAlbedoTex;
            std::string       materialAsset;
            std::string       skinnedMeshAsset;
            std::string       skinnedInstanceAsset;
            bool              hasAnyField      = false;

            auto commitEntity = [&]()
            {
                if (!hasAnyField)
                    return;

                EntityId e = world.CreateEntity();
                auto& t = world.AddTransform(e);
                t.SetPosition(position.x, position.y, position.z)
                 .SetRotation(rotation.x, rotation.y, rotation.z)
                 .SetScale(scale.x, scale.y, scale.z);

                if (!scriptName.empty())
                {
                    world.AddScript(e, scriptName);
                }

                if (hasMaterialColor || !materialAsset.empty())
                {
                    DirectX::XMFLOAT3 col = hasMaterialColor ? materialColor
                                                             : DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
                    MaterialComponent& mat = world.AddMaterial(e, col, materialAsset);
                    mat.roughness         = materialRoughness;
                    mat.metalness         = materialMetalness;
                    mat.albedoTexturePath = materialAlbedoTex;
                }

                if (!skinnedMeshAsset.empty())
                {
                    SkinnedMeshComponent& sm = world.AddSkinnedMesh(e, skinnedMeshAsset);
                    sm.instanceAssetPath = skinnedInstanceAsset;

                    // 아직 애니메이션 시스템과 연결되지 않았으므로
                    // 간단히 1개짜리 항등 본 팔레트를 연결해 둡니다.
                    sm.boneMatrices = &g_IdentityBone;
                    sm.boneCount    = 1;
                }

                // 다음 엔티티를 위해 초기화
                position = { 0.0f, 0.0f, 0.0f };
                rotation = { 0.0f, 0.0f, 0.0f };
                scale    = { 1.0f, 1.0f, 1.0f };
                scriptName.clear();
                materialColor      = { 0.7f, 0.7f, 0.7f };
                hasMaterialColor   = false;
                materialRoughness  = 0.5f;
                materialMetalness  = 0.0f;
                materialAlbedoTex.clear();
                materialAsset.clear();
                skinnedMeshAsset.clear();
                skinnedInstanceAsset.clear();
                hasAnyField        = false;
            };

            std::string line;
            while (std::getline(ifs, line))
            {
                // 원시 한 줄 로그 (필요 시 주석 해제)
                // {
                //     char dbg[256] = {};
                //     std::snprintf(dbg, sizeof(dbg),
                //                   "[SceneFile::Load] line=\"%s\"\n",
                //                   line.c_str());
                //     OutputDebugStringA(dbg);
                // }

                if (line.empty())
                {
                    commitEntity();
                    continue;
                }

                if (!line.empty() && line[0] == '#')
                    continue;

                std::istringstream iss(line);
                std::string key;
                if (!std::getline(iss, key, ':'))
                    continue;

                std::string value;
                std::getline(iss, value);

                Trim(key);
                Trim(value);

                if (key == "entity")
                {
                    // 새 엔티티 시작: 이전 엔티티를 커밋
                    commitEntity();
                    hasAnyField = true;
                }
                else if (key == "position")
                {
                    std::istringstream vs(value);
                    vs >> position.x >> position.y >> position.z;
                    hasAnyField = true;
                }
                else if (key == "rotation")
                {
                    std::istringstream vs(value);
                    vs >> rotation.x >> rotation.y >> rotation.z;
                    hasAnyField = true;
                }
                else if (key == "scale")
                {
                    std::istringstream vs(value);
                    vs >> scale.x >> scale.y >> scale.z;
                    hasAnyField = true;
                }
                else if (key == "script")
                {
                    scriptName = value;
                    hasAnyField = true;
                }
                else if (key == "material_color")
                {
                    std::istringstream vs(value);
                    vs >> materialColor.x >> materialColor.y >> materialColor.z;
                    hasMaterialColor = true;
                    hasAnyField      = true;
                }
                else if (key == "material_asset")
                {
                    materialAsset = value;
                    hasAnyField   = true;
                }
                else if (key == "material_roughness")
                {
                    materialRoughness = std::clamp(std::stof(value), 0.0f, 1.0f);
                    hasAnyField       = true;
                }
                else if (key == "material_metalness")
                {
                    materialMetalness = std::clamp(std::stof(value), 0.0f, 1.0f);
                    hasAnyField       = true;
                }
                else if (key == "material_albedoTex")
                {
                    materialAlbedoTex = value;
                    hasAnyField       = true;
                }
                else if (key == "skinned_mesh")
                {
                    skinnedMeshAsset = value;
                    hasAnyField      = true;

                    char buf[256] = {};
                    std::snprintf(buf, sizeof(buf),
                                  "[SceneFile::Load] skinned_mesh=\"%s\"\n",
                                  skinnedMeshAsset.c_str());
                    OutputDebugStringA(buf);
                }
                else if (key == "skinned_instance")
                {
                    skinnedInstanceAsset = value;
                    hasAnyField          = true;

                    char buf[256] = {};
                    std::snprintf(buf, sizeof(buf),
                                  "[SceneFile::Load] skinned_instance=\"%s\"\n",
                                  skinnedInstanceAsset.c_str());
                    OutputDebugStringA(buf);
                }
            }

            // 마지막 엔티티 커밋
            commitEntity();

            return true;
        }
    }
}
