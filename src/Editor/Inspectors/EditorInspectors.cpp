#include "Editor/Inspectors/EditorInspectors.h"
#include "Editor/EditorCore.h"

namespace Alice
{
    // 각 파일에 있는 등록 함수들 forward
    void RegisterRttrAutoInspectors();
    void RegisterPhysicsInspectors(EditorCore& core);
    void RegisterFixedLayoutInspectors(EditorCore& core);

    void RegisterEngineInspectors(EditorCore& core)
    {
        RegisterFixedLayoutInspectors(core);  // 먼저 등록 (순서 제어용)
        RegisterRttrAutoInspectors();
        RegisterPhysicsInspectors(core);
    }
}
