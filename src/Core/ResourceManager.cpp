#include "Core/ResourceManager.h"

#include <fstream>
#include <system_error>
#include "Core/Logger.h"

namespace Alice
{
    bool ResourceManager::StartsWith(std::string_view s, std::string_view prefix)
    {
        return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
    }

    std::filesystem::path ResourceManager::NormalizeLegacyDotDot(const std::filesystem::path& p)
    {
        // 레거시: "../Assets/...", "../Resource/...", "../Cooked/..." 를
        //        "Assets/...",  "Resource/...",  "Cooked/..." 로 정규화합니다.
        // C++20: generic_string()으로 슬래시 통일 후 prefix 검사
        const std::string s = p.generic_string();
        if (StartsWith(s, "../Assets/"))   return std::filesystem::path("Assets")   / s.substr(std::string_view("../Assets/").size());
        if (StartsWith(s, "../Resource/")) return std::filesystem::path("Resource") / s.substr(std::string_view("../Resource/").size());
        if (StartsWith(s, "../Cooked/"))   return std::filesystem::path("Cooked")   / s.substr(std::string_view("../Cooked/").size());
        return p;
    }

    std::filesystem::path ResourceManager::ToAlicePath(std::filesystem::path p)
    {
        // 디렉터리(확장자 없음)에는 적용하지 않습니다.
        if (!p.has_filename())
            return p;
        p.replace_extension(".alice");
        return p;
    }

    void ResourceManager::Configure(bool gameMode, const std::filesystem::path& exeDir)
    {
        m_gameMode = gameMode;

        // gameMode: exeDir 기준(배포 폴더에 Assets/Resource/Cooked가 있다고 가정)
        if (m_gameMode)
        {
            m_rootDir = exeDir;
            return;
        }

        // editorMode: exeDir = build/bin/(Debug|Release) 이므로,
        //            프로젝트 루트는 exeDir/../../.. 로 가정합니다.
        //            (너무 복잡하게 분기하지 않고, 기존 규칙을 그대로 사용)
        m_rootDir = exeDir.parent_path().parent_path().parent_path();
    }

    std::filesystem::path ResourceManager::Resolve(const std::filesystem::path& logicalOrRelative) const
    {
        if (logicalOrRelative.empty())
            return {};

        if (logicalOrRelative.is_absolute())
            return logicalOrRelative.lexically_normal();

        std::filesystem::path p = NormalizeLegacyDotDot(logicalOrRelative);
        const std::string s = p.generic_string();

        // 논리 루트 3종을 지원합니다.
        if (StartsWith(s, "Assets/") || s == "Assets")
            return (m_rootDir / p).lexically_normal();

        // gameMode에서는 Resource/... 를 직접 들고 있지 않으므로,
        // Resource/<rel> 요청은 Cooked/<rel>.alice 로 매핑합니다.
        if (StartsWith(s, "Resource/"))
        {
            if (m_gameMode)
            {
                std::filesystem::path rest = s.substr(std::string_view("Resource/").size());
                return (m_rootDir / ToAlicePath(std::filesystem::path("Cooked") / rest)).lexically_normal();
            }
            return (m_rootDir / p).lexically_normal();
        }
        if (s == "Resource")
        {
            return (m_rootDir / (m_gameMode ? std::filesystem::path("Cooked") : std::filesystem::path("Resource"))).lexically_normal();
        }

        if (StartsWith(s, "Cooked/") || s == "Cooked")
            return (m_rootDir / p).lexically_normal();

        // 그 외: 루트 기준 상대경로로 취급 (호환용)
        return (m_rootDir / p).lexically_normal();
    }

    void ResourceManager::Clear()
    {
        // 아직 구체적인 리소스는 없으므로 빈 구현입니다.
        // 이후 텍스처/메시/셰이더 등을 추가할 때 이곳에서 정리합니다.
    }

    bool ResourceManager::LoadBinary(const std::filesystem::path& path,
                                     std::vector<std::uint8_t>& outData,
                                     bool encrypted) const
    {
        outData.clear();

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) return false;

        ifs.seekg(0, std::ios::end);
        const std::streamoff size = ifs.tellg();
        if (size <= 0) return true; // 빈 파일

        ifs.seekg(0, std::ios::beg);
        outData.resize(static_cast<std::size_t>(size));
        ifs.read(reinterpret_cast<char*>(outData.data()), size);

        if (encrypted)
        {
            XorCrypt(outData);
        }

        return true;
    }

    bool ResourceManager::LoadBinaryAuto(const std::filesystem::path& logicalPath,
                                         std::vector<std::uint8_t>& outData) const
    {
        outData.clear();

        // 1) gameMode에서는 Cooked(암호화)를 우선 시도합니다.
        //    - 예: "Resource/Image/a.png" 요청 → "Cooked/Image/a.alice" 를 복호화 로드
        if (m_gameMode)
        {
            const std::filesystem::path normalized = NormalizeLegacyDotDot(logicalPath);
            const std::string s = normalized.generic_string();

            // Resource/... 는 Resolve 단계에서 Cooked/<rel>.alice 로 매핑됩니다.
            const auto resolved = Resolve(normalized);
            if (std::filesystem::exists(resolved))
            {
                return LoadBinary(resolved, outData, true);
            }

            // Cooked 직접 지정 또는 .alice 같은 암호화 확장자면 복호화 로드
            const auto ext = resolved.extension().string();
            if (StartsWith(resolved.generic_string(), (CookedDir().generic_string() + "/")))
            {
                // Cooked 아래는 기본적으로 암호화된 바이너리로 취급
                return LoadBinary(resolved, outData, true);
            }
            if (_stricmp(ext.c_str(), ".alice") == 0)
            {
                return LoadBinary(resolved, outData, true);
            }

            // 마지막: 원본 파일을 그냥 로드(디버그/개발 편의용)
            return LoadBinary(resolved, outData, false);
        }

        // 2) editorMode에서는 원본을 우선 로드 (복호화 없이)
        return LoadBinary(Resolve(logicalPath), outData, false);
    }

    bool ResourceManager::CookAndSave(const std::filesystem::path& srcPath,
                                      const std::filesystem::path& cookedPath) const
    {
        std::vector<std::uint8_t> data;
        if (!LoadBinary(srcPath, data, false)) return false;

        ALICE_LOG_INFO("CookAndSave: \"%s\" -> \"%s\" (bytes=%zu)",
                       srcPath.string().c_str(),
                       cookedPath.string().c_str(),
                       data.size());

        // 간단한 XOR 암호화
        XorCrypt(data);

        auto parent = cookedPath.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent))
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }

        std::ofstream ofs(cookedPath, std::ios::binary);
        if (!ofs.is_open()) return false;

        if (!data.empty())
        {
            ofs.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        }

        return true;
    }

    bool ResourceManager::CookDirectoryRecursive(const std::filesystem::path& srcDir,
                                                 const std::filesystem::path& dstDir) const
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(srcDir, ec) || ec)
        {
            ALICE_LOG_ERRORF("ResourceManager::CookDirectoryRecursive: srcDir does not exist. \"%s\" (%s)",
                             srcDir.string().c_str(), ec.message().c_str());
            return false;
        }
        if (!fs::is_directory(srcDir, ec) || ec)
        {
            ALICE_LOG_ERRORF("ResourceManager::CookDirectoryRecursive: srcDir is not a directory. \"%s\" (%s)",
                             srcDir.string().c_str(), ec.message().c_str());
            return false;
        }

        fs::create_directories(dstDir, ec);
        if (ec)
        {
            ALICE_LOG_ERRORF("ResourceManager::CookDirectoryRecursive: failed to create dstDir. \"%s\" (%s)",
                             dstDir.string().c_str(), ec.message().c_str());
            return false;
        }

        std::size_t cookedCount = 0;
        for (fs::recursive_directory_iterator it(srcDir, ec), end; it != end; it.increment(ec))
        {
            if (ec)
            {
                ALICE_LOG_WARN("ResourceManager::CookDirectoryRecursive: iterator error under \"%s\" (%s)",
                               srcDir.string().c_str(), ec.message().c_str());
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec) || ec)
            {
                ec.clear();
                continue;
            }

            const fs::path inPath = it->path();
            const fs::path rel    = fs::relative(inPath, srcDir, ec);
            if (ec)
            {
                ALICE_LOG_WARN("ResourceManager::CookDirectoryRecursive: relative() failed. in=\"%s\" (%s)",
                               inPath.string().c_str(), ec.message().c_str());
                ec.clear();
                continue;
            }

            const fs::path outPath = dstDir / rel;
            if (!CookAndSave(inPath, outPath))
            {
                ALICE_LOG_ERRORF("ResourceManager::CookDirectoryRecursive: CookAndSave failed. in=\"%s\" out=\"%s\"",
                                 inPath.string().c_str(), outPath.string().c_str());
                return false; // 실패는 즉시 중단 (배포 결과가 불완전해지면 안 됨)
            }
            ++cookedCount;
        }

        ALICE_LOG_INFO("ResourceManager::CookDirectoryRecursive: cooked %zu files. src=\"%s\" dst=\"%s\"",
                       cookedCount, srcDir.string().c_str(), dstDir.string().c_str());
        return true;
    }

    void ResourceManager::XorCrypt(std::vector<std::uint8_t>& data) const
    {
        if (data.empty() || m_key.empty())
            return;

        const std::size_t keyLen = m_key.size();
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            data[i] ^= static_cast<std::uint8_t>(m_key[i % keyLen]);
        }
    }
}

