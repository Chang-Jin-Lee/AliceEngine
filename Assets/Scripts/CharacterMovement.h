#pragma once
#include "Core/Script.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    class CharacterMovement : public IScript
    {
        // [1] ALICE_BODY 뒤에 세미콜론(;)은 붙여도 되고 안 붙여도 되지만,
        // 매크로 내부가 완벽하다면 붙이는 것이 관습입니다.
        ALICE_BODY(CharacterMovement);

    public:
        void Update(float DeltaTime) override;

        // --- 변수 리플렉션 ---
        ALICE_PROPERTY(float, m_moveSpeed, 10.0f);
        ALICE_PROPERTY(float, m_jumpSpeed, 6.5f);

        // [수정] 이 줄이 빠져서 cpp 파일에서 에러가 났던 것입니다. 다시 추가하세요.
        ALICE_PROPERTY(float, m_gravity, 18.0f);

        // --- 함수 리플렉션 ---
        void Attack();
        ALICE_FUNC(Attack);

        void SetSpeed(float newSpeed);
        ALICE_FUNC(SetSpeed);

    private:
        float m_velY = 0.0f;
    };
}