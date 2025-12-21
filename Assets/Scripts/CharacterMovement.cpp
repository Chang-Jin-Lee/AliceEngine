// 캐릭터를 카메라 방향 기준으로 앞뒤좌우로 움직이게 하는 스크립트
#include "CharacterMovement.h"
#include "Core/GameObject.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Alice
{
    REGISTER_SCRIPT(CharacterMovement);

    ALICE_SCRIPT_REFLECT_BEGIN(CharacterMovement)
        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_moveSpeed)
        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_jumpSpeed)
        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_gravity)
        ALICE_SCRIPT_REFLECT_END()

        void CharacterMovement::Update(float DeltaTime)
    {
        auto* input = Input();
        if (!input) return;

        auto go = gameObject();
        auto* t = go.GetComponent<TransformComponent>();
        auto anim = go.GetAnimator();
        if (!t || !anim.IsValid()) return;

        // --- 1. 카메라 정보 가져오기 ---
        // (월드에서 Primary 카메라를 찾는다고 가정)
        auto* world = GetWorld();
        CameraComponent* mainCam = world ? world->GetCamera() : nullptr;

        float camYawRad = 0.0f;
        if (mainCam)
        {
            // 카메라 엔티티의 트랜스폼에서 Y 회전값(Degree)을 가져옴
            auto camT = mainCam->GetOwner().GetComponent<TransformComponent>();
            if (camT) camYawRad = camT->rotation.y * (static_cast<float>(M_PI) / 180.0f);
        }

        // --- 2. 입력 수집 (로컬 기준) ---
        float inputX = 0.0f; // A, D (좌우)
        float inputZ = 0.0f; // W, S (전후)

        if (input->GetKey(KeyCode::W)) inputZ += 1.0f;
        if (input->GetKey(KeyCode::S)) inputZ -= 1.0f;
        if (input->GetKey(KeyCode::D)) inputX += 1.0f;
        if (input->GetKey(KeyCode::A)) inputX -= 1.0f;

        // --- 3. 입력 벡터를 카메라 방향으로 회전 ---
        // 2D 회전 행렬 공식 적용:
        // x' = x * cos - z * sin
        // z' = x * sin + z * cos
        // (여기서 z는 전방이므로 수학적 y축 역할)
        float sinY = std::sin(camYawRad);
        float cosY = std::cos(camYawRad);

        float worldX = inputX * cosY + inputZ * sinY;
        float worldZ = -inputX * sinY + inputZ * cosY;

        // --- 4. 이동 및 회전 적용 ---
        float len = std::sqrt(worldX * worldX + worldZ * worldZ);
        if (len > 0.0001f)
        {
            // 정규화
            worldX /= len;
            worldZ /= len;

            // 캐릭터 회전: 이동하려는 월드 방향을 바라봄
            float radian = std::atan2(worldX, worldZ);
            float degree = radian * (180.0f / static_cast<float>(M_PI));
            t->SetRotation(0.0f, degree, 0.0f); // atan2(x, z)는 북쪽이 0이므로 +180 보정 불필요할 수 있음 (좌표계 확인 필요)

            anim.Play(2); // Walk
        }
        else
        {
            anim.Play(0); // Idle
        }

        // 위치 이동
        t->position.x += worldX * Get_m_moveSpeed() * DeltaTime;
        t->position.z += worldZ * Get_m_moveSpeed() * DeltaTime;

        // --- 5. 점프/중력 (기존 유지) ---
        if (t->position.y <= 0.0f)
        {
            t->position.y = 0.0f;
            if (m_velY < 0.0f) m_velY = 0.0f;
            if (input->GetKeyDown(KeyCode::Space)) m_velY = Get_m_jumpSpeed();
        }
        m_velY -= Get_m_gravity() * DeltaTime;
        t->position.y += m_velY * DeltaTime;
        if (t->position.y < 0.0f) t->position.y = 0.0f;
    }
}


// 캐릭터를 그냥 단순히 앞뒤좌우로 움직이게 하는 스크립트
//#include "CharacterMovement.h"
//#include "Core/GameObject.h"
//#include <cmath> // sqrt, atan2
//
//// PI 상수가 없다면 정의 (보통 math 헤더에 M_PI로 있습니다)
//#ifndef M_PI
//#define M_PI 3.14159265358979323846
//#endif
//
//namespace Alice
//{
//    REGISTER_SCRIPT(CharacterMovement);
//
//    ALICE_SCRIPT_REFLECT_BEGIN(CharacterMovement)
//        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_moveSpeed)
//        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_jumpSpeed)
//        ALICE_SCRIPT_SERIALIZE_FIELD(CharacterMovement, m_gravity)
//        ALICE_SCRIPT_REFLECT_END()
//
//        void CharacterMovement::Update(float DeltaTime)
//    {
//        auto* input = Input();
//        if (!input) return;
//
//        auto go = gameObject();
//        auto* t = go.GetComponent<TransformComponent>();
//        if (!t) return;
//
//        auto anim = go.GetAnimator();
//        if (!anim.IsValid()) return;
//
//        // --- 1. 입력 수집 ---
//        float mx = 0.0f;
//        float mz = 0.0f;
//
//        // 기존 코드: 여기서 rotationY를 직접 대입해서 덮어씌워지는 문제가 있었음
//        // 수정 코드: 입력 벡터(mx, mz)만 수집함
//        if (input->GetKey(KeyCode::W)) { mz += 1.0f; }
//        if (input->GetKey(KeyCode::S)) { mz -= 1.0f; }
//        if (input->GetKey(KeyCode::D)) { mx += 1.0f; }
//        if (input->GetKey(KeyCode::A)) { mx -= 1.0f; }
//
//        // --- 2. 이동 여부 및 정규화 ---
//        const float len = std::sqrt(mx * mx + mz * mz);
//        bool isMoving = (len > 0.0001f);
//
//        if (isMoving)
//        {
//            // 대각선 이동 시 속도가 빨라지는 것 방지 (정규화)
//            mx /= len;
//            mz /= len;
//
//            // --- 3. 회전 계산 ---
//            // atan2(x, z)는 (0,0)에서 (x,z)를 바라보는 각도를 라디안으로 반환합니다.
//            float radian = std::atan2(mx, mz);
//
//            // 라디안 -> 디그리 변환
//            float degree = radian * (180.0f / static_cast<float>(M_PI));
//
//            // 오프셋 보정
//            // atan2(0, 1) = 0도 (보통 북쪽/전방)
//            // 작성하신 코드에서 W(전방)일 때 180도를 주고 있으므로, +180도 보정
//            t->SetRotation(0.0f, degree + 180.0f, 0.0f);
//
//            // 걷기 애니메이션 스마트 재생
//            anim.Play(2);
//        }
//        else
//        {
//            // 대기 애니메이션 스마트 재생
//            anim.Play(0);
//        }
//
//        // --- 4. 위치 적용 ---
//        t->position.x += mx * Get_m_moveSpeed() * DeltaTime;
//        t->position.z += mz * Get_m_moveSpeed() * DeltaTime;
//
//        // --- 5. 점프/중력 로직 ---
//        const bool grounded = (t->position.y <= 0.0f);
//        if (grounded)
//        {
//            t->position.y = 0.0f;
//            if (m_velY < 0.0f) m_velY = 0.0f;
//            if (input->GetKeyDown(KeyCode::Space))
//            {
//                m_velY = Get_m_jumpSpeed();
//                // anim.Play(3, true); // 점프 예시
//            }
//        }
//
//        m_velY -= Get_m_gravity() * DeltaTime;
//        t->position.y += m_velY * DeltaTime;
//        if (t->position.y < 0.0f)
//            t->position.y = 0.0f;
//    }
//}