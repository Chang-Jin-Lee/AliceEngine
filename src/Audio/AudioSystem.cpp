#include "Audio/AudioSystem.h"

#include <algorithm>

#include "Audio/SoundManager.h"
#include "Core/Helper.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"

namespace Alice
{
    namespace
    {
        std::wstring ToKeyW(const std::string& key)
        {
            return WStringFromUtf8(key);
        }

        std::wstring MakeInstanceId(EntityId id)
        {
            return L"AudioSource#" + std::to_wstring(static_cast<std::uint64_t>(id));
        }

        std::wstring MakeSoundBoxId(EntityId id)
        {
            return L"SoundBox#" + std::to_wstring(static_cast<std::uint64_t>(id));
        }

        bool IsInsideBox(const SoundBoxComponent& box, const TransformComponent* tr, const DirectX::XMFLOAT3& pos)
        {
            DirectX::XMFLOAT3 p = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            DirectX::XMFLOAT3 s = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            float x0 = box.boundsMin.x * s.x + p.x;
            float x1 = box.boundsMax.x * s.x + p.x;
            float y0 = box.boundsMin.y * s.y + p.y;
            float y1 = box.boundsMax.y * s.y + p.y;
            float z0 = box.boundsMin.z * s.z + p.z;
            float z1 = box.boundsMax.z * s.z + p.z;

            const float minX = std::min(x0, x1);
            const float maxX = std::max(x0, x1);
            const float minY = std::min(y0, y1);
            const float maxY = std::max(y0, y1);
            const float minZ = std::min(z0, z1);
            const float maxZ = std::max(z0, z1);

            return (pos.x >= minX && pos.x <= maxX &&
                    pos.y >= minY && pos.y <= maxY &&
                    pos.z >= minZ && pos.z <= maxZ);
        }

        float CenterWeight01(const SoundBoxComponent& box, const TransformComponent* tr, const DirectX::XMFLOAT3& pos)
        {
            DirectX::XMFLOAT3 p = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            DirectX::XMFLOAT3 s = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            float x0 = box.boundsMin.x * s.x + p.x;
            float x1 = box.boundsMax.x * s.x + p.x;
            float y0 = box.boundsMin.y * s.y + p.y;
            float y1 = box.boundsMax.y * s.y + p.y;
            float z0 = box.boundsMin.z * s.z + p.z;
            float z1 = box.boundsMax.z * s.z + p.z;

            const float minX = std::min(x0, x1);
            const float maxX = std::max(x0, x1);
            const float minY = std::min(y0, y1);
            const float maxY = std::max(y0, y1);
            const float minZ = std::min(z0, z1);
            const float maxZ = std::max(z0, z1);

            const float cx = (minX + maxX) * 0.5f;
            const float cy = (minY + maxY) * 0.5f;
            const float cz = (minZ + maxZ) * 0.5f;
            const float ex = std::max(1e-6f, (maxX - minX) * 0.5f);
            const float ey = std::max(1e-6f, (maxY - minY) * 0.5f);
            const float ez = std::max(1e-6f, (maxZ - minZ) * 0.5f);

            const float nx = std::fabs(pos.x - cx) / ex;
            const float ny = std::fabs(pos.y - cy) / ey;
            const float nz = std::fabs(pos.z - cz) / ez;
            float u = std::max(nx, std::max(ny, nz));
            u = std::clamp(u, 0.0f, 1.0f);

            float w = 1.0f - u;
            w = std::pow(w, std::max(box.curve, 0.0001f));
            return std::clamp(w, 0.0f, 1.0f);
        }
    }

    void AudioSystem::Update(World& world, double)
    {
        if (!m_resources) return;

        // Listener 업데이트 (AudioListenerComponent 우선, 없으면 MainCamera)
        bool listenerSet = false;
        DirectX::XMFLOAT3 listenerPos{ 0, 0, 0 };
        for (const auto& [id, listener] : world.GetComponents<AudioListenerComponent>())
        {
            if (!listener.primary) continue;
            if (auto* tr = world.GetComponent<TransformComponent>(id))
            {
                DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&tr->rotation));
                DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), R);
                DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 1, 0, 0), R);
                Sound::SetListener(tr->position, DirectX::XMFLOAT3(0, 0, 0), forward, up);
                listenerPos = tr->position;
                listenerSet = true;
                break;
            }
        }

        if (!listenerSet)
        {
            EntityId camId = world.GetMainCameraEntityId();
            if (camId != InvalidEntityId)
            {
                if (auto* tr = world.GetComponent<TransformComponent>(camId))
                {
                    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&tr->rotation));
                    DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 0, 1, 0), R);
                    DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0, 1, 0, 0), R);
                    Sound::SetListener(tr->position, DirectX::XMFLOAT3(0, 0, 0), forward, up);
                    listenerPos = tr->position;
                }
            }
        }

        for (auto [id, src] : world.GetComponents<AudioSourceComponent>())
        {
            if (src.soundPath.empty())
                continue;

            Runtime& rt = m_runtime[id];
            if (rt.key.empty())
            {
                rt.key = ToKeyW(src.soundKey.empty() ? src.soundPath : src.soundKey);
                rt.instanceId = MakeInstanceId(id);
            }

            if (!rt.loaded)
            {
                const auto type = (src.type == AudioType::BGM) ? Sound::Type::BGM : Sound::Type::SFX;
                if (Sound::LoadAuto(*m_resources, rt.key, src.soundPath, type))
                    rt.loaded = true;
            }

            if (!rt.loaded)
                continue;

            auto play2D = [&]() {
                if (src.type == AudioType::BGM)
                {
                    Sound::PlayBGM(rt.key);
                    Sound::SetBGMVolume(src.volume);
                }
                else
                {
                    Sound::PlaySFX(rt.key, src.volume, src.pitch, src.loop);
                }
            };

            auto play3D = [&]() {
                Sound::Play3DInstance(rt.instanceId, rt.key, src.loop);
                rt.playing3D = true;
            };

            if ((src.playOnStart && !rt.started) || src.requestPlay)
            {
                if (src.is3D) play3D();
                else play2D();

                rt.started = true;
                src.requestPlay = false;
            }

            if (src.requestStop)
            {
                if (src.is3D)
                {
                    Sound::Stop3DInstance(rt.instanceId);
                    rt.playing3D = false;
                }
                else
                {
                    if (src.type == AudioType::BGM) Sound::StopBGM();
                    else Sound::StopSfx(rt.key);
                }
                src.requestStop = false;
            }

            if (src.is3D && rt.playing3D)
            {
                if (auto* tr = world.GetComponent<TransformComponent>(id))
                {
                    Sound::Update3DInstance(rt.instanceId, tr->position, src.volume, src.minDistance, src.maxDistance);
                }
            }
        }

        // SoundBox 처리 (listener 위치 기준)
        for (auto [id, box] : world.GetComponents<SoundBoxComponent>())
        {
            if (box.soundPath.empty())
                continue;

            SoundBoxRuntime& rt = m_soundBoxRuntime[id];
            if (rt.key.empty())
            {
                rt.key = ToKeyW(box.soundKey.empty() ? box.soundPath : box.soundKey);
                rt.instanceId = MakeSoundBoxId(id);
            }

            if (!rt.loaded)
            {
                const auto type = (box.type == SoundBoxType::BGM) ? Sound::Type::BGM : Sound::Type::SFX;
                if (Sound::LoadAuto(*m_resources, rt.key, box.soundPath, type))
                    rt.loaded = true;
            }

            if (!rt.loaded)
                continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(id);
            const bool inside = IsInsideBox(box, tr, listenerPos);

            if (inside && !rt.wasInside && box.playOnEnter)
                Sound::Play3DInstance(rt.instanceId, rt.key, box.loop);
            if (!inside && rt.wasInside && box.stopOnExit)
                Sound::Stop3DInstance(rt.instanceId);

            rt.wasInside = inside;

            if (inside)
            {
                const float w = CenterWeight01(box, tr, listenerPos);
                const float vol = box.edgeVolume + (box.centerVolume - box.edgeVolume) * w;
                const DirectX::XMFLOAT3 srcPos = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
                Sound::Update3DInstance(rt.instanceId, srcPos, vol, box.minDistance, box.maxDistance);
            }
        }

        Sound::Update();
    }
}

