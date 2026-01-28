#pragma once

#include <string>
#include "Core/IScript.h"
#include "AliceUI/BindWidget.h"

namespace Alice
{
    class UIHudExample : public IScript
    {
    public:
        RTTR_ENABLE(IScript)
        void Start() override;
        void Update(float deltaTime) override;

        // 인스펙터에서 설정할 루트 위젯 이름
        std::string rootWidgetName = "UI_Root";

        // BindWidget 대상
        UIButtonComponent* okButton = nullptr;
        UITextComponent* titleText = nullptr;
        UIGaugeComponent* hpGauge = nullptr;
    };
}
