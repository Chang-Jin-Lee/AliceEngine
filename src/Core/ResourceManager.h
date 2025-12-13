#pragma once

#include <filesystem>
#include <string_view>
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

        /// GameMode(배포용 실행)인지 여부에 따라, Assets/Resource/Cooked 루트 해석 기준을 설정합니다.
        /// - editorMode(false): 프로젝트 루트(= exeDir 기준 3단계 상위)를 기준으로 Assets/Resource/Cooked 를 찾습니다.
        /// - gameMode(true)   : exeDir(= 실행 파일 폴더) 기준으로 Assets/Resource/Cooked 를 찾습니다.
        ///
        /// 사용 예:
        ///   resources.Configure(/*gameMode=*/!m_editorMode, exeDir);
        void Configure(bool gameMode, const std::filesystem::path& exeDir);

        /// 논리 경로("Assets/..", "Resource/..", "Cooked/..")를 실제 경로로 변환합니다.
        /// - "../Assets/..." 같은 레거시 경로도 자동으로 "Assets/..." 로 정규화해서 처리합니다.
        std::filesystem::path Resolve(const std::filesystem::path& logicalOrRelative) const;

        /// 루트 디렉터리들(디버그/로그/툴에서 사용)
        const std::filesystem::path& RootDir()   const { return m_rootDir; }
        std::filesystem::path        AssetsDir() const { return m_rootDir / "Assets"; }
        std::filesystem::path        ResourceDir() const { return m_rootDir / "Resource"; }
        std::filesystem::path        CookedDir() const { return m_rootDir / "Cooked"; }

        /// 보관 중인 리소스를 모두 정리합니다.
        void Clear();

        /// 바이너리 파일을 읽어옵니다.
        /// - encrypted 가 true 이면, 간단한 XOR 기반 복호화를 수행합니다.
        bool LoadBinary(const std::filesystem::path& path,
                        std::vector<std::uint8_t>& outData,
                        bool encrypted) const;

        /// "논리 경로"를 받아서 자동으로 로드합니다.
        /// - gameMode 에서는 Cooked 쪽(암호화)을 우선 사용합니다.
        /// - editorMode 에서는 원본(Resource/Assets) 파일을 우선 사용합니다.
        bool LoadBinaryAuto(const std::filesystem::path& logicalPath,
                            std::vector<std::uint8_t>& outData) const;

        /// 원본 파일을 읽어 간단히 암호화해서 대상 경로에 저장합니다.
        /// - "쿠킹(cooking)" 용도로 사용합니다.
        bool CookAndSave(const std::filesystem::path& srcPath,
                         const std::filesystem::path& cookedPath) const;

        /// 디렉터리 전체를 암호화 Cooked 로 내보냅니다.
        /// - srcDir 하위의 파일들을 dstDir 하위에 동일한 상대 경로로 저장합니다.
        bool CookDirectoryRecursive(const std::filesystem::path& srcDir,
                                    const std::filesystem::path& dstDir) const;

    private:
        /// 매우 단순한 XOR 기반 스트림 암·복호화
        void XorCrypt(std::vector<std::uint8_t>& data) const;

        static bool StartsWith(std::string_view s, std::string_view prefix);
        static std::filesystem::path NormalizeLegacyDotDot(const std::filesystem::path& p);
        static std::filesystem::path ToAlicePath(std::filesystem::path p);

        // 필요하면 나중에 키를 외부에서 주입받을 수 있게 바꿀 수 있습니다.
        const std::string m_key = "AliceRendererSimpleKey";

        bool m_gameMode = false;
        std::filesystem::path m_rootDir; // editorMode: projectRoot, gameMode: exeDir
    };
}

