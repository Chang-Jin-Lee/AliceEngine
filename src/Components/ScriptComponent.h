#pragma once

#include <memory>
#include <string>

namespace Alice
{
    class IScript;

    /// 한 엔티티에 붙는 단일 스크립트 컴포넌트입니다.
    /// - scriptName 은 팩토리/리플렉션용 이름입니다.
    /// - instance 는 실제 실행되는 스크립트 객체입니다.
    struct ScriptComponent
    {
        std::string                scriptName;
        std::unique_ptr<IScript>   instance;
        bool enabled { true };
        bool awoken  { false };
        bool started { false };
        bool wasEnabled { true };

        // .meta 기본값을 한 번만 주입하기 위한 플래그입니다.
        bool defaultsApplied { false };
    };
}
