#pragma once

#include <unordered_map>
#include <string>

#include "Core/World.h"
#include "Core/ResourceManager.h"
#include "Components/AudioSourceComponent.h"
#include "Components/AudioListenerComponent.h"
#include "Components/SoundBoxComponent.h"

namespace Alice
{
    class AudioSystem
    {
    public:
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        void Update(World& world, double dtSec);

    private:
        struct Runtime
        {
            bool loaded{ false };
            bool started{ false };
            bool playing3D{ false };
            std::wstring key;
            std::wstring instanceId;
        };

        struct SoundBoxRuntime
        {
            bool loaded{ false };
            bool wasInside{ false };
            std::wstring key;
            std::wstring instanceId;
        };

        ResourceManager* m_resources = nullptr;
        std::unordered_map<EntityId, Runtime> m_runtime;
        std::unordered_map<EntityId, SoundBoxRuntime> m_soundBoxRuntime;
    };
}

