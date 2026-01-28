#pragma once

#include <string>

namespace Alice
{
    /// Post Process Volume 보간 기준이 될 GameObject 이름을 지정합니다.
    struct PostProcessVolumeReferenceComponent
    {
        bool enabled = true;
        std::string referenceObjectName;
    };
}
