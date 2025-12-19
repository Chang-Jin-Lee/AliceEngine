#include "Core/Material.h"
#include "Core/ReflectionSerializer.h"
#include "Core/ComponentRegistry.h"  // RTTR 등록 코드 포함

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <sstream>

#include "Core/World.h"
#include "Core/Logger.h"

namespace Alice
{
    namespace MaterialFile
    {
        bool Load(const std::filesystem::path& path, MaterialComponent& outMaterial)
        {
            // RTTR 기반으로 자동 로드
            bool result = ReflectionSerializer::Load(path, outMaterial);
            
            // roughness, metalness 클램핑 (RTTR로는 기본값 처리만 하므로 여기서 보정)
            outMaterial.roughness = std::clamp(outMaterial.roughness, 0.0f, 1.0f);
            outMaterial.metalness = std::clamp(outMaterial.metalness, 0.0f, 1.0f);

            ALICE_LOG_INFO("[MaterialFile] Load: \"%s\" color=(%.3f, %.3f, %.3f) rough=%.3f metal=%.3f tex=\"%s\"",
                           path.string().c_str(),
                           outMaterial.color.x, outMaterial.color.y, outMaterial.color.z,
                           outMaterial.roughness,
                           outMaterial.metalness,
                           outMaterial.albedoTexturePath.c_str());

            return result;
        }

        bool Save(const std::filesystem::path& path, const MaterialComponent& material)
        {
            // RTTR 기반으로 자동 저장
            bool result = ReflectionSerializer::Save(path, material);

            ALICE_LOG_INFO("[MaterialFile] Save: \"%s\" color=(%.3f, %.3f, %.3f) rough=%.3f metal=%.3f tex=\"%s\"",
                           path.string().c_str(),
                           material.color.x, material.color.y, material.color.z,
                           material.roughness,
                           material.metalness,
                           material.albedoTexturePath.c_str());

            return result;
        }
    }
}


