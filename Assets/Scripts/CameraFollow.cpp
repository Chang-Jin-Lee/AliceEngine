#include "CameraFollow.h"

#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraFollow);

    ALICE_SCRIPT_REFLECT_BEGIN(CameraFollow)
        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_offsetX)
        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_offsetY)
        ALICE_SCRIPT_SERIALIZE_FIELD(CameraFollow, m_offsetZ)
    ALICE_SCRIPT_REFLECT_END()

    // 헬퍼 함수: 선형 보간 (a에서 b로 t만큼 이동)
    float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    void CameraFollow::MoveDirectly()
    {
        auto go = gameObject();

        if (!go.IsValid())
            return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid())
            return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT)  return;

        myT->position.x = tT->position.x + Get_m_offsetX();
        myT->position.y = tT->position.y + Get_m_offsetY();
        myT->position.z = tT->position.z + Get_m_offsetZ();

        // 타겟을 바라보게 회전(yaw/pitch) 맞추기
        const float dx = tT->position.x - myT->position.x;
        const float dy = tT->position.y - myT->position.y;
        const float dz = tT->position.z - myT->position.z;

        const float yaw = std::atan2(dx, dz);
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        const float pitch = -std::atan2(dy, distXZ);

        myT->rotation.x = pitch;
        myT->rotation.y = yaw;
    }

    void CameraFollow::MoveLerp(const float& lateDeltaTime)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto target = go.FindFirstSkinnedMesh();
        if (!target.IsValid())
            return;

        auto* myT = go.GetComponent<TransformComponent>();
        auto* tT = target.GetComponent<TransformComponent>();
        if (!myT || !tT)
            return;

        // 1. 목표 위치 계산 (아직 대입하지 않음)
        float targetX = tT->position.x + Get_m_offsetX();
        float targetY = tT->position.y + Get_m_offsetY();
        float targetZ = tT->position.z + Get_m_offsetZ();

        // 2. 보간(Lerp) 적용
        // 공식: 현재위치 += (목표위치 - 현재위치) * 속도 * 시간
        // smoothSpeed가 높을수록 빠르게 달라붙고, 낮을수록 부드럽게(느리게) 따라옵니다.
        //float t = Get_m_smoothSpeed() * lateDeltaTime;
        //myT->position.x = Lerp(myT->position.x, targetX, t);
        //myT->position.y = Lerp(myT->position.y, targetY, t);
        //myT->position.z = Lerp(myT->position.z, targetZ, t);

        // 보간 안쓰고 그냥 따라붙게 하기
        float t = lateDeltaTime;
        myT->position.x = myT->position.x * lateDeltaTime;
        myT->position.y = myT->position.y * lateDeltaTime;
        myT->position.z = myT->position.z * lateDeltaTime;
    }

    void CameraFollow::LateUpdate(float lateDeltaTime)
    {
        //MoveLerp(lateDeltaTime);
        MoveDirectly();
    }
}


