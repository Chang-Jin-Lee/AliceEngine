#pragma once

namespace Alice
{
    /// 카메라 쉐이크 파라미터
    struct CameraShakeComponent
    {
        bool enabled{ true };
        float amplitude{ 0.0f };
        float frequency{ 20.0f };
        float duration{ 0.0f };
        float decay{ 1.0f };

        // 런타임
        float elapsed{ 0.0f };
    };
}
