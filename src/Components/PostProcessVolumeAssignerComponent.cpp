#include "Components/PostProcessVolumeAssignerComponent.h"
#include "Core/World.h"
#include "Core/Logger.h"
#include "Components/TransformComponent.h"
#include "core/GameObject.h"

namespace Alice
{
    bool PostProcessVolumeAssignerComponent::Apply(World& world)
    {
        m_lastApplySuccess = false;
        m_lastApplyMessage.clear();
        m_lastAppliedEntityId = InvalidEntityId;

        // 1. 대상 GameObject 이름 확인
        if (targetGameObjectName.empty())
        {
            m_lastApplyMessage = "Target GameObject Name is empty";
            return false;
        }

        // 2. World에서 GameObject 찾기
        GameObject targetObj = world.FindGameObject(targetGameObjectName);
        
        // 2-1. 이름으로 찾지 못한 경우, "Entity_123" 형식인지 확인하여 Entity ID로 직접 검색
        if (!targetObj.IsValid())
        {
            // "Entity_" 접두사로 시작하는지 확인
            if (targetGameObjectName.size() > 7 && 
                targetGameObjectName.substr(0, 7) == "Entity_")
            {
                try
                {
                    // "Entity_" 뒤의 숫자를 파싱
                    std::string idStr = targetGameObjectName.substr(7);
                    EntityId parsedId = static_cast<EntityId>(std::stoul(idStr));
                    
                    // EntityId가 유효한지 확인 (간단한 검증: IDComponent 존재 여부)
                    if (auto* idComp = world.GetComponent<IDComponent>(parsedId))
                    {
                        targetObj = GameObject(&world, parsedId, nullptr);
                    }
                }
                catch (...)
                {
                    // 파싱 실패 시 무시하고 계속 진행
                }
            }
        }
        
        if (!targetObj.IsValid())
        {
            m_lastApplyMessage = "GameObject '" + targetGameObjectName + "' not found. "
                "Make sure the entity name is set correctly (use 'Change Name' in Hierarchy context menu).";
            return false;
        }

        EntityId targetId = targetObj.id();
        m_lastAppliedEntityId = targetId;

        // 3. TransformComponent 존재 확인 (경고만, 적용은 계속)
        auto* transform = world.GetComponent<TransformComponent>(targetId);
        if (!transform || !transform->enabled)
        {
            ALICE_LOG_WARN("PostProcessVolumeAssigner: Target GameObject '%s' has no valid TransformComponent. Bound volume may not work correctly.", targetGameObjectName.c_str());
        }

        // 4. PostProcessVolumeComponent 가져오기 또는 생성
        PostProcessVolumeComponent* ppv = world.GetComponent<PostProcessVolumeComponent>(targetId);
        if (!ppv)
        {
            if (createIfMissing)
            {
                ppv = &world.AddComponent<PostProcessVolumeComponent>(targetId);
            }
            else
            {
                m_lastApplyMessage = "PostProcessVolumeComponent not found on '" + targetGameObjectName + "' and CreateIfMissing is false";
                return false;
            }
        }

        // 5. 템플릿 설정 복사
        *ppv = templateVolume;

        // 6. 성공 메시지
        m_lastApplySuccess = true;
        m_lastApplyMessage = "Applied to '" + targetGameObjectName + "' (EntityId: " + std::to_string(static_cast<uint32_t>(targetId)) + ")";
        m_lastAppliedTargetName = targetGameObjectName;

        return true;
    }
}
