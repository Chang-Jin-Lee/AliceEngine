#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include <DirectXMath.h>

namespace Alice
{
    // ---------------------------
    // Anim Notify (언리얼 엔진 스타일)
    // ---------------------------
    struct AnimNotify
    {
        float timeSec;
        std::function<void()> callback;
    };

    // ---------------------------
    // Advanced animation data
    // ---------------------------
    struct AdvancedAnimLayer
    {
        bool enabled = true;
        bool autoAdvance = true;

        std::string clipA;
        std::string clipB;

        float timeA = 0.0f;
        float timeB = 0.0f;

        float speedA = 1.0f;
        float speedB = 1.0f;

        bool loopA = true;
        bool loopB = true;

        float blend01 = 0.0f;    // A -> B crossfade (0..1)
        float layerAlpha = 1.0f; // Upper layer weight (0..1)
    };

    struct AdvancedAnimAdditive
    {
        bool enabled = false;
        bool autoAdvance = true;

        std::string clip;
        std::string refClip; // reference pose (ex: Idle t=0)

        float time = 0.0f;
        float speed = 1.0f;
        bool loop = false;

        float alpha = 1.0f; // additive strength (0..1)
    };

    struct AdvancedAnimProcedural
    {
        float strength = 0.0f;
        std::uint32_t seed = 0u;
        float timeSec = 0.0f;
    };

    struct AdvancedAnimIK
    {
        bool enabled = false;
        std::string tipBone = "Hand_L";
        int chainLength = 3;
        DirectX::XMFLOAT3 targetMS = { 0.0f, 0.0f, 0.0f };
        float weight = 1.0f;
    };

    struct AdvancedAnimAim
    {
        bool enabled = false;
        float yawRad = 0.0f;
        float weight = 1.0f;
    };

    struct AdvancedAnimSocket
    {
        std::string name;
        std::string parentBone;
        DirectX::XMFLOAT3 pos = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotDeg = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

        // Runtime output (world space)
        DirectX::XMFLOAT4X4 worldMatrix{ 1,0,0,0,
                                         0,1,0,0,
                                         0,0,1,0,
                                         0,0,0,1 };
    };

    struct AdvancedAnimationComponent
    {
        bool enabled = true;
        bool playing = true;

        AdvancedAnimLayer base;
        AdvancedAnimLayer upper;
        AdvancedAnimAdditive additive;
        AdvancedAnimProcedural procedural;
        AdvancedAnimIK ik;
        AdvancedAnimAim aim;

        std::vector<AdvancedAnimSocket> sockets;

        // CPU palette for rendering (auto-filled by AdvancedAnimSystem)
        std::vector<DirectX::XMFLOAT4X4> palette;

        // --------------------------------------------------------
        // Anim Montage & Notify System (언리얼 엔진 스타일)
        // --------------------------------------------------------
        using NotifyMap = std::unordered_map<std::string, std::vector<AnimNotify>>;
        NotifyMap notifies;

        // 노티파이 등록 (어떤 클립의, 몇 초에, 무슨 함수를 실행할지)
        void AddNotify(const std::string& clipName, float time, std::function<void()> func)
        {
            notifies[clipName].push_back({ time, func });
        }

        // 시스템에서 호출: 시간 범위 내의 노티파이 실행
        void CheckAndFireNotifies(const std::string& clipName, float prevTime, float currTime)
        {
            if (clipName.empty()) return;
            auto it = notifies.find(clipName);
            if (it == notifies.end()) return;

            for (const auto& notify : it->second)
            {
                // 시간 구간 사이에 노티파이가 있는지 확인
                // (일반 재생: prev < notify <= curr)
                // (역재생: prev > notify >= curr)
                bool forwardPass = (prevTime < notify.timeSec && currTime >= notify.timeSec);
                bool backwardPass = (prevTime > notify.timeSec && currTime <= notify.timeSec);
                
                if (forwardPass || backwardPass)
                {
                    if (notify.callback) notify.callback();
                }
            }
        }

        // Helper: add/update a socket definition
        void SetSocketSRT(const std::string& name,
                          const std::string& parentBone,
                          DirectX::XMFLOAT3 pos,
                          DirectX::XMFLOAT3 rotDeg,
                          DirectX::XMFLOAT3 scale)
        {
            for (auto& s : sockets)
            {
                if (s.name == name)
                {
                    s.parentBone = parentBone;
                    s.pos = pos;
                    s.rotDeg = rotDeg;
                    s.scale = scale;
                    return;
                }
            }

            AdvancedAnimSocket s{};
            s.name = name;
            s.parentBone = parentBone;
            s.pos = pos;
            s.rotDeg = rotDeg;
            s.scale = scale;
            sockets.push_back(std::move(s));
        }

        // Helper: get socket world matrix (identity if missing)
        DirectX::XMMATRIX GetSocketWorldMatrix(const std::string& name) const
        {
            for (const auto& s : sockets)
            {
                if (s.name == name)
                    return DirectX::XMLoadFloat4x4(&s.worldMatrix);
            }
            return DirectX::XMMatrixIdentity();
        }
    };
}

