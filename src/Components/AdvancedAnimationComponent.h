#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cmath> // atan2, asin 등을 위해 필요

#include <DirectXMath.h>

namespace Alice
{
    // ---------------------------
    // Anim Notify
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

        // 단일 IK -> 다중 IK 리스트 (발 IK 등 여러 개 동시 지원)
        // 예: 0: 왼발, 1: 오른발, 2: 왼손...
        std::vector<AdvancedAnimIK> ikChains;

        // 기존 코드를 위해 단일 IK 접근 유지 (ikChains[0]과 동기화)
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

        // 소켓의 월드 Transform(위치, 회전)을 추출하는 헬퍼 함수
        bool GetSocketWorldTransform(const std::string& name, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outRotDeg) const
        {
            for (const auto& s : sockets)
            {
                if (s.name == name)
                {
                    DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&s.worldMatrix);

                    DirectX::XMVECTOR scale, rotQuat, trans;
                    if (!DirectX::XMMatrixDecompose(&scale, &rotQuat, &trans, m))
                        return false;

                    // Position 저장
                    DirectX::XMStoreFloat3(&outPos, trans);

                    // Quaternion -> Euler Angles (Degrees) 변환
                    DirectX::XMFLOAT4 q;
                    DirectX::XMStoreFloat4(&q, rotQuat);

                    // 간단한 쿼터니언 -> 오일러 변환 (Y-X-Z 순서 등 엔진 좌표계에 따라 다를 수 있음)
                    // 여기서는 일반적인 Pitch(X), Yaw(Y), Roll(Z) 변환 적용
                    float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
                    float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
                    float pitch = std::atan2(sinr_cosp, cosr_cosp);

                    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
                    float yaw = 0.0f;
                    if (std::abs(sinp) >= 1.0f)
                        yaw = std::copysign(3.14159265f / 2.0f, sinp); // Use 90 degrees if out of range
                    else
                        yaw = std::asin(sinp);

                    float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
                    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
                    float roll = std::atan2(siny_cosp, cosy_cosp);

                    // Radian -> Degree 변환
                    constexpr float ToDeg = 180.0f / 3.14159265f;
                    outRotDeg.x = pitch * ToDeg;
                    outRotDeg.y = yaw * ToDeg;
                    outRotDeg.z = roll * ToDeg;

                    return true;
                }
            }
            return false;
        }

        // IK 체인 설정 헬퍼 함수
        void SetIK(int index, const std::string& boneName, int length, const DirectX::XMFLOAT3& target, float weight = 1.0f)
        {
            if (index < 0)
                return;
            if (index >= (int)ikChains.size())
                ikChains.resize(index + 1);
            ikChains[index].enabled = true;
            ikChains[index].tipBone = boneName;
            ikChains[index].chainLength = length;
            ikChains[index].targetMS = target;
            ikChains[index].weight = weight;

            // index 0이면 기존 ik 변수도 업데이트
            if (index == 0)
            {
                ik = ikChains[0];
            }
        }

        // IK 끄기
        void DisableIK(int index)
        {
            if (index >= 0 && index < (int)ikChains.size())
            {
                ikChains[index].enabled = false;

                // index 0이면 기존 ik 변수도 업데이트
                if (index == 0)
                {
                    ik.enabled = false;
                }
            }
        }
    };
}