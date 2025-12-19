#pragma once

#include "Core/Script.h"

namespace Alice
{
    /// 1/2/3 키로 애니메이션 클립을 바꾸는 예시
    class AnimationKeySwitch : public IScript
    {
    public:
        const char* GetName() const override { return "AnimationKeySwitch"; }

        void Start() override;
        void Update(float deltaTime) override;
    };
}



