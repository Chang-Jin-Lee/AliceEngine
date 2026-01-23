#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// SoundBoxComponent를 초기화하는 예시
    class SoundBoxSetupExample : public IScript
    {
        ALICE_BODY(SoundBoxSetupExample);

    public:
        void Start() override;
        void Update(float dt) override;

    public:
        // 사운드 박스용 기본 파라미터를 에디터에서 바로 조절
        ALICE_PROPERTY(std::string, soundPath,    std::string("Assets/Sounds/Forest.wav"));
        ALICE_PROPERTY(bool,        loop,         true);
        ALICE_PROPERTY(float,       minDistance,  1.0f);
        ALICE_PROPERTY(float,       maxDistance, 30.0f);
        
        // [Alice] 에디터에서 체크하면 디버그 박스가 보입니다.
        ALICE_PROPERTY(bool,        debugDraw,    false);
    };
}

