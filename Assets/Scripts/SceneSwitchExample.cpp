#include "SceneSwitchExample.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
namespace Alice
{
    REGISTER_SCRIPT(SceneSwitchExample);

    void SceneSwitchExample::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        auto* scenes = Scenes();
        if (!input || !scenes)
            return;

        if (input->GetKeyDown(KeyCode::F1))
        {
            // 코드 씬 전환 (REGISTER_SCENE 로 등록된 씬 이름)
            scenes->LoadSceneFile(m_targetName1.c_str());
        }

        if (input->GetKeyDown(KeyCode::F2))
        {
            // .scene 파일 로드 (프레임 끝에 처리됨)
            scenes->LoadSceneFile(m_targetName2.c_str());
        }
    }
}






