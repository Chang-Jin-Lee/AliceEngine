#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Alice
{
    /// 매우 단순한 리소스 매니저입니다.
    /// - 이후 텍스처/메시/셰이더 등을 캐싱/스트리밍하는 쪽으로 확장할 수 있습니다.
    /// - 현재는 "암호화/복호화된 바이너리 파일 입출력" 만 담당합니다.
    class ResourceManager
    {
    public:
        ResourceManager()  = default;
        ~ResourceManager() = default;

        /// 보관 중인 리소스를 모두 정리합니다.
        void Clear();

        /// 바이너리 파일을 읽어옵니다.
        /// - encrypted 가 true 이면, 간단한 XOR 기반 복호화를 수행합니다.
        bool LoadBinary(const std::filesystem::path& path,
                        std::vector<std::uint8_t>& outData,
                        bool encrypted) const;

        /// 원본 파일을 읽어 간단히 암호화해서 대상 경로에 저장합니다.
        /// - "쿠킹(cooking)" 용도로 사용합니다.
        bool CookAndSave(const std::filesystem::path& srcPath,
                         const std::filesystem::path& cookedPath) const;

    private:
        /// 매우 단순한 XOR 기반 스트림 암·복호화
        void XorCrypt(std::vector<std::uint8_t>& data) const;

        // 필요하면 나중에 키를 외부에서 주입받을 수 있게 바꿀 수 있습니다.
        const std::string m_key = "AliceRendererSimpleKey";
    };
}

