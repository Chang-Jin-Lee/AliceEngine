#include "Core/SceneFile.h"
#include "Core/SceneFileHelper.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함

#include <fstream>
#include <sstream>

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
                
                // Transform 저장 (기존 포맷 호환: position, rotation, scale 직접)
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

                // Script는 특별 처리 (문자열만)
                ofs << "script: ";
                if (script)
                    ofs << script->scriptName;
                ofs << "\n";

                // RTTR 기반으로 Material 저장
                if (mat)
                {
                    SceneFileHelper::SaveComponent(ofs, *mat, "material");
                }

                // RTTR 기반으로 SkinnedMesh 저장 (boneMatrices 제외)
                if (skinned)
                {
                    SceneFileHelper::SaveComponent(ofs, *skinned, "skinned");
                }

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
            world.Clear();

            // 한 엔티티에 대한 임시 버퍼 (RTTR 사용)
            TransformComponent tempTransform;
            MaterialComponent tempMaterial;
            SkinnedMeshComponent tempSkinnedMesh;
            std::string scriptName;
            bool hasAnyField = false;

            auto commitEntity = [&]()
            {
                if (!hasAnyField)
                    return;

                EntityId e = world.CreateEntity();
                
                // Transform 복사
                auto& t = world.AddTransform(e);
                t = tempTransform;

                // Script 추가
                if (!scriptName.empty())
                {
                    world.AddScript(e, scriptName);
                }

                // Material 추가 (color나 assetPath가 있으면)
                if (!tempMaterial.assetPath.empty() || 
                    tempMaterial.color.x != 0.7f || tempMaterial.color.y != 0.7f || tempMaterial.color.z != 0.7f)
                {
                    MaterialComponent& mat = world.AddMaterial(e, tempMaterial.color, tempMaterial.assetPath);
                    mat.roughness = tempMaterial.roughness;
                    mat.metalness = tempMaterial.metalness;
                    mat.albedoTexturePath = tempMaterial.albedoTexturePath;
                }

                // SkinnedMesh 추가
                if (!tempSkinnedMesh.meshAssetPath.empty())
                {
                    SkinnedMeshComponent& sm = world.AddSkinnedMesh(e, tempSkinnedMesh.meshAssetPath);
                    sm.instanceAssetPath = tempSkinnedMesh.instanceAssetPath;

                    // 아직 애니메이션 시스템과 연결되지 않았으므로
                    // 간단히 1개짜리 항등 본 팔레트를 연결해 둡니다.
                    sm.boneMatrices = &g_IdentityBone;
                    sm.boneCount    = 1;
                }

                // 다음 엔티티를 위해 초기화
                tempTransform = TransformComponent();
                tempMaterial = MaterialComponent();
                tempSkinnedMesh = SkinnedMeshComponent();
                scriptName.clear();
                hasAnyField = false;
            };

            std::string line;
            while (std::getline(ifs, line))
            {
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
                else if (key == "script")
                {
                    scriptName = value;
                    hasAnyField = true;
                }
                else
                {
                    // RTTR 기반으로 컴포넌트 프로퍼티 로드
                    bool loaded = false;
                    
                    // Transform 프로퍼티 (기존 포맷 호환: position, rotation, scale 직접 처리)
                    if (key == "position")
                    {
                        std::istringstream vs(value);
                        vs >> tempTransform.position.x >> tempTransform.position.y >> tempTransform.position.z;
                        loaded = true;
                    }
                    else if (key == "rotation")
                    {
                        std::istringstream vs(value);
                        vs >> tempTransform.rotation.x >> tempTransform.rotation.y >> tempTransform.rotation.z;
                        loaded = true;
                    }
                    else if (key == "scale")
                    {
                        std::istringstream vs(value);
                        vs >> tempTransform.scale.x >> tempTransform.scale.y >> tempTransform.scale.z;
                        loaded = true;
                    }
                    else
                    {
                        // Material, SkinnedMesh는 prefix로 구분하여 RTTR로 로드
                        loaded = SceneFileHelper::LoadComponentProperty(key, value, tempMaterial, "material");
                        if (!loaded)
                        {
                            loaded = SceneFileHelper::LoadComponentProperty(key, value, tempSkinnedMesh, "skinned");
                        }
                    }
                    
                    if (loaded)
                        hasAnyField = true;
                }
            }

            // 마지막 엔티티 커밋
            commitEntity();

            return true;
        }
    }
}
