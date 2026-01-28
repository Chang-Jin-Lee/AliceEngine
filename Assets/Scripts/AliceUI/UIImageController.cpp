#include "UIImageController.h"

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Core/InputTypes.h"

namespace Alice
{
    REGISTER_SCRIPT(UIImageController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<UIImageController>(UIImageController::_Refl_ClassName)
            .property("rootWidgetName", &UIImageController::rootWidgetName)
                (rttr::metadata("SerializeField", true));
    }

    namespace
    {
        // UI 루트 위젯을 이름으로 찾는 헬퍼
        EntityId FindRootWidgetByName(World& world, const std::string& name)
        {
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    void UIImageController::Start()
    {
        ALICE_LOG_INFO("UIImageController::Start()");
        // UI 트리에서 루트 위젯 찾기
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_INFO("[UIImageController] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }

        // 변수명과 같은 UI 위젯을 바인딩
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
            ALICE_LOG_INFO("[UIImageController] Missing required widgets: %d", result.missingRequired);
    }

    void UIImageController::Update(float /*deltaTime*/)
    {
        ALICE_LOG_INFO("[UIImageController] Update: %s", rootWidgetName.c_str());
        // 숫자 키(1~6)로 이미지 변경
        if (!TargetImage)
            return;
        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(KeyCode::Alpha1)) TargetImage->texturePath = m_imagePaths[0];
        if (input->GetKeyDown(KeyCode::Alpha2)) TargetImage->texturePath = m_imagePaths[1];
        if (input->GetKeyDown(KeyCode::Alpha3)) TargetImage->texturePath = m_imagePaths[2];
        if (input->GetKeyDown(KeyCode::Alpha4)) TargetImage->texturePath = m_imagePaths[3];
        if (input->GetKeyDown(KeyCode::Alpha5)) TargetImage->texturePath = m_imagePaths[4];
        if (input->GetKeyDown(KeyCode::Alpha6)) TargetImage->texturePath = m_imagePaths[5];
    }
}
