#pragma once

#include <string>

namespace Alice
{
    /// 매우 단순한 리소스 매니저의 골격입니다.
    /// - 훗날 텍스처/메시/셰이더 등을 캐싱하는 용도로 확장할 예정입니다.
    class ResourceManager
    {
    public:
        ResourceManager()  = default;
        ~ResourceManager() = default;

        /// 보관 중인 리소스를 모두 정리합니다.
        void Clear();
    };
}


