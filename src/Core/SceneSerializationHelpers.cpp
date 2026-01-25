#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Core/SceneSerializationHelpers.h"
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Alice
{
    namespace SceneSerializationHelpers
    {
        static std::filesystem::path GetProjectRoot()
        {
            wchar_t exePathW[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
            std::filesystem::path exePath = exePathW;
            std::filesystem::path exeDir = exePath.parent_path();
            return exeDir.parent_path().parent_path().parent_path();
        }

        std::string NormalizePathToRelative(const std::string& path)
        {
            if (path.empty())
                return path;

            std::filesystem::path p(path);

            if (!p.is_absolute())
            {
                const std::string s = p.generic_string();
                if (s.find("Assets/") == 0 || s.find("Resource/") == 0 || s.find("Cooked/") == 0)
                    return s;
            }

            if (p.is_absolute())
            {
                const std::filesystem::path projectRoot = GetProjectRoot();
                try
                {
                    std::filesystem::path relative = std::filesystem::relative(p, projectRoot);
                    if (!relative.empty())
                    {
                        const std::string result = relative.generic_string();
                        if (result.find("Assets/") == 0 || result.find("Resource/") == 0 || result.find("Cooked/") == 0)
                            return result;
                    }
                }
                catch (...) {}
            }

            return path;
        }

        DirectX::XMFLOAT4X4 g_IdentityBone(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1);

        JsonRttr::json WritePhysicsSceneSettings(const Phy_SettingsComponent& settings)
        {
            JsonRttr::json out = JsonRttr::json::object();

            out["enablePhysics"] = settings.enablePhysics;
            out["enableGroundPlane"] = settings.enableGroundPlane;
            out["groundStaticFriction"] = settings.groundStaticFriction;
            out["groundDynamicFriction"] = settings.groundDynamicFriction;
            out["groundRestitution"] = settings.groundRestitution;
            out["groundLayerBits"] = settings.groundLayerBits;
            out["groundCollideMask"] = settings.groundCollideMask;
            out["groundQueryMask"] = settings.groundQueryMask;
            out["groundIgnoreLayers"] = settings.groundIgnoreLayers;
            out["groundIsTrigger"] = settings.groundIsTrigger;
            out["gravity"] = JsonRttr::json::array({ settings.gravity.x, settings.gravity.y, settings.gravity.z });
            out["fixedDt"] = settings.fixedDt;
            out["maxSubsteps"] = settings.maxSubsteps;
            out["filterRevision"] = settings.filterRevision;

            out["layerCollideMatrix"] = JsonRttr::json::array();
            for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
            {
                JsonRttr::json row = JsonRttr::json::array();
                for (int col = 0; col < MAX_PHYSICS_LAYERS; ++col)
                    row.push_back(settings.layerCollideMatrix[i][col]);
                out["layerCollideMatrix"].push_back(row);
            }

            out["layerQueryMatrix"] = JsonRttr::json::array();
            for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
            {
                JsonRttr::json row = JsonRttr::json::array();
                for (int col = 0; col < MAX_PHYSICS_LAYERS; ++col)
                    row.push_back(settings.layerQueryMatrix[i][col]);
                out["layerQueryMatrix"].push_back(row);
            }

            out["layerNames"] = JsonRttr::json::array();
            for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
                out["layerNames"].push_back(settings.layerNames[i]);

            return out;
        }

        bool LoadPhysicsSceneSettings(Phy_SettingsComponent& settings, const JsonRttr::json& root)
        {
            if (!root.is_object()) return false;

            if (root.contains("enablePhysics") && root["enablePhysics"].is_boolean())
                settings.enablePhysics = root["enablePhysics"].get<bool>();
            if (root.contains("enableGroundPlane") && root["enableGroundPlane"].is_boolean())
                settings.enableGroundPlane = root["enableGroundPlane"].get<bool>();
            if (root.contains("groundStaticFriction") && root["groundStaticFriction"].is_number())
                settings.groundStaticFriction = root["groundStaticFriction"].get<float>();
            if (root.contains("groundDynamicFriction") && root["groundDynamicFriction"].is_number())
                settings.groundDynamicFriction = root["groundDynamicFriction"].get<float>();
            if (root.contains("groundRestitution") && root["groundRestitution"].is_number())
                settings.groundRestitution = root["groundRestitution"].get<float>();
            if (root.contains("groundLayerBits") && root["groundLayerBits"].is_number_unsigned())
                settings.groundLayerBits = root["groundLayerBits"].get<uint32_t>();
            if (root.contains("groundCollideMask") && root["groundCollideMask"].is_number_unsigned())
                settings.groundCollideMask = root["groundCollideMask"].get<uint32_t>();
            if (root.contains("groundQueryMask") && root["groundQueryMask"].is_number_unsigned())
                settings.groundQueryMask = root["groundQueryMask"].get<uint32_t>();
            if (root.contains("groundIgnoreLayers") && root["groundIgnoreLayers"].is_number_unsigned())
                settings.groundIgnoreLayers = root["groundIgnoreLayers"].get<uint32_t>();
            if (root.contains("groundIsTrigger") && root["groundIsTrigger"].is_boolean())
                settings.groundIsTrigger = root["groundIsTrigger"].get<bool>();

            if (root.contains("gravity") && root["gravity"].is_array() && root["gravity"].size() == 3)
            {
                settings.gravity.x = root["gravity"][0].get<float>();
                settings.gravity.y = root["gravity"][1].get<float>();
                settings.gravity.z = root["gravity"][2].get<float>();
            }
            if (root.contains("fixedDt") && root["fixedDt"].is_number())
                settings.fixedDt = root["fixedDt"].get<float>();
            if (root.contains("maxSubsteps") && root["maxSubsteps"].is_number_integer())
                settings.maxSubsteps = root["maxSubsteps"].get<int>();
            if (root.contains("filterRevision") && root["filterRevision"].is_number_unsigned())
                settings.filterRevision = root["filterRevision"].get<uint32_t>();

            if (root.contains("layerCollideMatrix") && root["layerCollideMatrix"].is_array())
            {
                const auto& matrix = root["layerCollideMatrix"];
                for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(matrix.size()); ++i)
                {
                    if (matrix[i].is_array())
                    {
                        const auto& row = matrix[i];
                        for (int col = 0; col < MAX_PHYSICS_LAYERS && col < static_cast<int>(row.size()); ++col)
                        {
                            if (row[col].is_boolean())
                                settings.layerCollideMatrix[i][col] = row[col].get<bool>();
                            else if (row[col].is_number_integer())
                                settings.layerCollideMatrix[i][col] = (row[col].get<int>() != 0);
                        }
                    }
                }
            }
            if (root.contains("layerQueryMatrix") && root["layerQueryMatrix"].is_array())
            {
                const auto& matrix = root["layerQueryMatrix"];
                for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(matrix.size()); ++i)
                {
                    if (matrix[i].is_array())
                    {
                        const auto& row = matrix[i];
                        for (int col = 0; col < MAX_PHYSICS_LAYERS && col < static_cast<int>(row.size()); ++col)
                        {
                            if (row[col].is_boolean())
                                settings.layerQueryMatrix[i][col] = row[col].get<bool>();
                            else if (row[col].is_number_integer())
                                settings.layerQueryMatrix[i][col] = (row[col].get<int>() != 0);
                        }
                    }
                }
            }
            if (root.contains("layerNames") && root["layerNames"].is_array())
            {
                const auto& names = root["layerNames"];
                for (int i = 0; i < MAX_PHYSICS_LAYERS && i < static_cast<int>(names.size()); ++i)
                {
                    if (names[i].is_string())
                        settings.layerNames[i] = names[i].get<std::string>();
                }
            }

            return true;
        }
    }
}
