#include "SoundBoxSetupExample.h"

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Components/SoundBoxComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(SoundBoxSetupExample);

    void SoundBoxSetupExample::Start()
    {
        // 컴포넌트가 없으면 추가하고 기본값을 세팅
        auto go = gameObject();
        auto* box = go.GetComponent<SoundBoxComponent>();
        if (!box)
            box = &go.AddComponent<SoundBoxComponent>();

        box->soundPath = soundPath;
        box->loop = loop;
        box->minDistance = minDistance;
        box->maxDistance = maxDistance;
        box->debugDraw = debugDraw;
    }

    void SoundBoxSetupExample::Update(float dt)
    {
        // 에디터에서 값을 바꾸면 실시간으로 반영
        auto go = gameObject();
        if (auto* box = go.GetComponent<SoundBoxComponent>())
        {
            box->debugDraw = debugDraw;
        }
    }
}

