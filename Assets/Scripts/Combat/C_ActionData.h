#pragma once

#include <string>

namespace Alice::Combat
{
    struct TimeWindow
    {
        float startSec = 0.0f;
        float endSec = 0.0f;

        bool Contains(float t) const { return t >= startSec && t <= endSec; }
    };

    struct ActionDef
    {
        std::string name;
        std::string animClip;
        float durationSec = 0.6f;

        TimeWindow hitActive{};
        TimeWindow invuln{};
        TimeWindow guardActive{};
        TimeWindow parry{};
        TimeWindow canCancel{};
    };

    struct ActionDatabase
    {
        ActionDef attackLight;
        ActionDef dodge;
        ActionDef guard;

        static ActionDatabase MakeDefault();
    };
}
