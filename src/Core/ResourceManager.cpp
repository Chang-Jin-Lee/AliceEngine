#include "Core/ResourceManager.h"

// 구현부에서만 필요한 무거운 헤더들
#include <d3d11.h>
#include <DirectXTK/WICTextureLoader.h>
#include <DirectXTK/DDSTextureLoader.h>

#include <fstream>
#include <system_error>
#include <cstdint>
#include <cstring>
#include "Core/Logger.h"

namespace
{
    struct AliceChunkHeader
    {
        char     magic[4];      // "ALIC"
        std::uint32_t version;  // 1
        std::uint64_t fileId;
        std::uint32_t chunkIndex;
        std::uint32_t chunkCount;
        std::uint64_t originalSize;
        std::uint32_t payloadSize;
    };
}

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

    // D:\Project\Resource\Textures\player.png 
    // -> Resource/Textures/player.png 
    std::filesystem::path ResourceManager::NormalizeResourcePathAbsoluteToLogical(const std::filesystem::path& p)
    {
		// 절대 경로가 아니면 그대로 반환함
        if (!p.is_absolute()) return p;

        // absolute 경로 안에 ".../Resource/<rel>" 또는 "...\\Resource\\<rel>" 가 있으면
        // "Resource/<rel>" 로 정규화함 (최종 빌드에서 경로 노출 최소화).
        const std::string s = p.generic_string(); // '/' 로 통일
        std::string lower = s;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        const std::string needle = "/resource/";
        const auto pos = lower.find(needle);
        if (pos == std::string::npos) return p;

        const std::string rel = s.substr(pos + needle.size());
        if (rel.empty()) return std::filesystem::path("Resource");
        return std::filesystem::path("Resource") / std::filesystem::path(rel);
    }

    std::uint64_t ResourceManager::Fnv1a64Bytes(const std::uint8_t* data, std::size_t size)
    {
        constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
        constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
        std::uint64_t hash = FNV_OFFSET_BASIS;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<std::uint64_t>(data[i]);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    std::uint64_t ResourceManager::HashString64(std::string_view s)
    {
        return Fnv1a64Bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    std::uint64_t ResourceManager::ComputeBufferHashSampled(const std::vector<std::uint8_t>& data)
    {
        // AssetManager 스타일: size + sample(first/last 4KB) 조합
        constexpr std::size_t SAMPLE = 4096;
        const std::uint64_t sizeHash = static_cast<std::uint64_t>(data.size());
        if (data.empty())
            return sizeHash;

        std::vector<std::uint8_t> buf;
        buf.reserve((std::min)(data.size(), SAMPLE) * 2);

        const std::size_t head = (std::min)(SAMPLE, data.size());
        buf.insert(buf.end(), data.begin(), data.begin() + head);
        if (data.size() > SAMPLE)
        {
            const std::size_t tail = (std::min)(SAMPLE, data.size());
            buf.insert(buf.end(), data.end() - tail, data.end());
        }

        const std::uint64_t sampleHash = Fnv1a64Bytes(buf.data(), buf.size());
        std::uint64_t finalHash = sizeHash ^ (sampleHash << 1);
        return finalHash;
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
        if (logicalOrRelative.empty()) return {};

        // lexically_normal()를 쓰면 사이사이에 있는 ./ or ../ or /// 등을 정리해줌
        if (logicalOrRelative.is_absolute())
            return NormalizeResourcePathAbsoluteToLogical(logicalOrRelative).lexically_normal();

        std::filesystem::path p = NormalizeLegacyDotDot(logicalOrRelative);
        p = NormalizeResourcePathAbsoluteToLogical(p);
        const std::string s = p.generic_string();

        // 논리 루트 3종을 지원합니다.
        if (StartsWith(s, "Assets/") || s == "Assets")
            return (m_rootDir / p).lexically_normal();

        // gameMode에서는 Resource/... 원본을 들고 있지 않으므로,
        // Resource/<rel> 요청은 Cooked/Chunks/<hash>/c0000.alice 로 매핑합니다.
        if (StartsWith(s, "Resource/"))
        {
            if (m_gameMode)
            {
                const std::string rest = s.substr(std::string_view("Resource/").size());
                return Chunk0PathForResourceRel(rest);
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
        if (auto sp = LoadSharedBinaryAuto(logicalPath))
        {
            outData = *sp; // 호환 API: 복사
            return true;
        }
        return false;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> ResourceManager::LoadSharedBinaryAuto(const std::filesystem::path& logicalPath) const
    {
        const std::filesystem::path normalized = NormalizeResourcePathAbsoluteToLogical(NormalizeLegacyDotDot(logicalPath));
        const std::string logicalKey = normalized.generic_string();

        // 0) logicalPath -> contentHash 캐시
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (auto it = m_pathToHash.find(logicalKey); it != m_pathToHash.end())
            {
                const auto h = it->second;
                if (auto it2 = m_blobCache.find(h); it2 != m_blobCache.end())
                {
                    if (auto sp = it2->second.lock())
                        return sp;
                }
            }
        }

        // 1) gameMode: Resource는 청크 스토어에서 로드
        if (m_gameMode)
        {
            const std::string s = normalized.generic_string();
            if (StartsWith(s, "Resource/"))
            {
                const std::string rel = s.substr(std::string_view("Resource/").size());
                auto sp = LoadResourceChunksByRel(rel);
                if (sp)
                {
                    const auto h = ComputeBufferHashSampled(*sp);
                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                    m_blobCache[h] = sp;
                    m_pathToHash[logicalKey] = h;
                    return sp;
                }
                return nullptr;
            }

            // 그 외 Cooked 경로는 단일 .alice 파일(암호화)로 로드
            const auto resolved = Resolve(normalized);
            if (StartsWith(resolved.generic_string(), (CookedDir().generic_string() + "/")))
            {
                std::vector<std::uint8_t> data;
                if (!LoadBinary(resolved, data, true))
                    return nullptr;
                auto sp = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
                const auto h = ComputeBufferHashSampled(*sp);
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_blobCache[h] = sp;
                m_pathToHash[logicalKey] = h;
                return sp;
            }

            // 마지막 폴백: 그대로 파일 로드(개발 편의)
            std::vector<std::uint8_t> data;
            if (!LoadBinary(Resolve(normalized), data, false))
                return nullptr;
            auto sp = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
            const auto h = ComputeBufferHashSampled(*sp);
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_blobCache[h] = sp;
            m_pathToHash[logicalKey] = h;
            return sp;
        }

        // 2) editorMode: 원본 파일을 그대로 로드
        std::vector<std::uint8_t> data;
        if (!LoadBinary(Resolve(normalized), data, false))
            return nullptr;
        auto sp = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
        const auto h = ComputeBufferHashSampled(*sp);
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_blobCache[h] = sp;
        m_pathToHash[logicalKey] = h;
        return sp;
    }

    std::filesystem::path ResourceManager::Chunk0PathForResourceRel(std::string_view resourceRel) const
    {
        // fileId = rel 문자열 해시 (폴더구조 노출 방지용)
        const std::uint64_t fileId = HashString64(resourceRel);

        char hex[17] = {};
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fileId));
        const std::string hexStr = hex;

        const std::filesystem::path dir = CookedDir() / "Chunks" / hexStr.substr(0, 2) / hexStr;
        return (dir / "c0000.alice").lexically_normal();
    }

    std::shared_ptr<const std::vector<std::uint8_t>> ResourceManager::LoadResourceChunksByRel(std::string_view resourceRel) const
    {
        namespace fs = std::filesystem;

        const std::uint64_t fileId = HashString64(resourceRel);
        fs::path c0 = Chunk0PathForResourceRel(resourceRel);
        if (!fs::exists(c0))
        {
            ALICE_LOG_ERRORF("ResourceManager: missing chunk0 for Resource/%s -> \"%s\"",
                             std::string(resourceRel).c_str(), c0.string().c_str());
            return nullptr;
        }

        struct ChunkReader
        {
            const ResourceManager& rm;
            std::uint64_t          fileId;

            bool Read(std::uint32_t idx, AliceChunkHeader& outHdr, std::vector<std::uint8_t>& outPayload) const
            {
                namespace fs2 = std::filesystem;

                char hex[17] = {};
                std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fileId));
                const std::string hexStr = hex;
                const fs2::path dir = rm.CookedDir() / "Chunks" / hexStr.substr(0, 2) / hexStr;

                char name[32] = {};
                std::snprintf(name, sizeof(name), "c%04u.alice", static_cast<unsigned>(idx));
                const fs2::path p = dir / name;

                std::vector<std::uint8_t> raw;
                if (!rm.LoadBinary(p, raw, false))
                    return false;
                if (raw.size() < sizeof(AliceChunkHeader))
                    return false;

                std::memcpy(&outHdr, raw.data(), sizeof(AliceChunkHeader));
                if (std::memcmp(outHdr.magic, "ALIC", 4) != 0 || outHdr.version != 1 || outHdr.fileId != fileId)
                    return false;

                const std::size_t payloadOff = sizeof(AliceChunkHeader);
                const std::size_t payloadSize = static_cast<std::size_t>(outHdr.payloadSize);
                if (payloadOff + payloadSize > raw.size())
                    return false;

                outPayload.assign(raw.begin() + payloadOff, raw.begin() + payloadOff + payloadSize);
                rm.XorCrypt(outPayload);
                return true;
            }
        };

        const ChunkReader reader{ *this, fileId };

        AliceChunkHeader h0{};
        std::vector<std::uint8_t> p0;
        if (!reader.Read(0, h0, p0))
        {
            ALICE_LOG_ERRORF("ResourceManager: failed to read/decrypt chunk0. \"%s\"", c0.string().c_str());
            return nullptr;
        }

        ALICE_LOG_INFO("ResourceManager: chunked load Resource/%s -> chunks=%u size=%llu",
                       std::string(resourceRel).c_str(),
                       static_cast<unsigned>(h0.chunkCount),
                       static_cast<unsigned long long>(h0.originalSize));

        auto out = std::make_shared<std::vector<std::uint8_t>>();
        out->reserve(static_cast<std::size_t>(h0.originalSize));
        out->insert(out->end(), p0.begin(), p0.end());

        for (std::uint32_t i = 1; i < h0.chunkCount; ++i)
        {
            AliceChunkHeader hi{};
            std::vector<std::uint8_t> pi;
            if (!reader.Read(i, hi, pi))
            {
                ALICE_LOG_ERRORF("ResourceManager: failed to read/decrypt chunk%u for Resource/%s",
                                 static_cast<unsigned>(i), std::string(resourceRel).c_str());
                return nullptr;
            }
            out->insert(out->end(), pi.begin(), pi.end());
        }

        return out;
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

    bool ResourceManager::CookAndSaveBytes(const std::vector<std::uint8_t>& plainBytes,
                                           const std::filesystem::path& cookedPath) const
    {
        std::error_code ec;
        std::filesystem::create_directories(cookedPath.parent_path(), ec);

        std::vector<std::uint8_t> data = plainBytes;
        XorCrypt(data);

        std::ofstream ofs(cookedPath, std::ios::binary);
        if (!ofs.is_open())
        {
            ALICE_LOG_ERRORF("CookAndSaveBytes: failed to open output \"%s\"", cookedPath.string().c_str());
            return false;
        }

        if (!data.empty())
            ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

        ALICE_LOG_INFO("CookAndSaveBytes: \"%s\" (bytes=%zu)",
                       cookedPath.string().c_str(),
                       data.size());
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

    bool ResourceManager::CookResourceToChunkStore(const std::filesystem::path& resourceDirAbs,
                                                   const std::filesystem::path& cookedDirAbs,
                                                   std::size_t chunkBytes) const
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(resourceDirAbs, ec) || ec || !fs::is_directory(resourceDirAbs, ec) || ec)
        {
            ALICE_LOG_ERRORF("CookResourceToChunkStore: invalid resourceDir. \"%s\" (%s)",
                             resourceDirAbs.string().c_str(), ec.message().c_str());
            return false;
        }
        if (chunkBytes < 4096) chunkBytes = 4096;

        struct ChunkHeader
        {
            char     magic[4];      // "ALIC"
            std::uint32_t version;  // 1
            std::uint64_t fileId;
            std::uint32_t chunkIndex;
            std::uint32_t chunkCount;
            std::uint64_t originalSize;
            std::uint32_t payloadSize;
        };

        std::size_t fileCount = 0;
        for (fs::recursive_directory_iterator it(resourceDirAbs, ec), end; it != end; it.increment(ec))
        {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

            const fs::path inPath = it->path();
            fs::path rel = fs::relative(inPath, resourceDirAbs, ec);
            if (ec) { ec.clear(); continue; }

            const std::string relStr = rel.generic_string(); // fileId는 이 문자열로 결정
            const std::uint64_t fileId = HashString64(relStr);

            char hex[17] = {};
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fileId));
            const std::string hexStr = hex;
            const fs::path outDir = cookedDirAbs / "Chunks" / hexStr.substr(0, 2) / hexStr;
            fs::create_directories(outDir, ec);
            ec.clear();

            // 파일 읽기
            std::vector<std::uint8_t> data;
            if (!LoadBinary(inPath, data, false))
            {
                ALICE_LOG_ERRORF("CookResourceToChunkStore: read failed. \"%s\"", inPath.string().c_str());
                return false;
            }

            const std::uint64_t originalSize = static_cast<std::uint64_t>(data.size());
            const std::uint32_t chunkCount = static_cast<std::uint32_t>((data.size() + chunkBytes - 1) / chunkBytes);
            ALICE_LOG_INFO("CookResourceChunk: rel=\"%s\" fileId=%s size=%llu chunks=%u",
                           relStr.c_str(),
                           hexStr.c_str(),
                           static_cast<unsigned long long>(originalSize),
                           static_cast<unsigned>(chunkCount));

            for (std::uint32_t i = 0; i < chunkCount; ++i)
            {
                const std::size_t off = static_cast<std::size_t>(i) * chunkBytes;
                const std::size_t len = (std::min)(chunkBytes, data.size() - off);

                std::vector<std::uint8_t> payload(data.begin() + off, data.begin() + off + len);
                XorCrypt(payload);

                ChunkHeader hdr{};
                std::memcpy(hdr.magic, "ALIC", 4);
                hdr.version = 1;
                hdr.fileId = fileId;
                hdr.chunkIndex = i;
                hdr.chunkCount = chunkCount;
                hdr.originalSize = originalSize;
                hdr.payloadSize = static_cast<std::uint32_t>(payload.size());

                char name[32] = {};
                std::snprintf(name, sizeof(name), "c%04u.alice", static_cast<unsigned>(i));
                const fs::path outPath = outDir / name;

                std::ofstream ofs(outPath, std::ios::binary);
                if (!ofs.is_open())
                {
                    ALICE_LOG_ERRORF("CookResourceToChunkStore: write open failed. \"%s\"", outPath.string().c_str());
                    return false;
                }
                ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
                if (!payload.empty())
                    ofs.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));

                ALICE_LOG_INFO("  ChunkOut: \"%s\" payload=%u", outPath.string().c_str(), hdr.payloadSize);
            }

            ++fileCount;
        }

        ALICE_LOG_INFO("CookResourceToChunkStore: cooked %zu files into \"%s/Chunks\"",
                       fileCount, cookedDirAbs.string().c_str());
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

    // -----------------------------------------------------------------------
    // [Template Specialization 구현]
    // -----------------------------------------------------------------------

    // ID3D11ShaderResourceView 로드 구현
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> 
    ResourceLoader<ID3D11ShaderResourceView>::Load(const ResourceManager& rm, 
                                                   const std::filesystem::path& path, 
                                                   ID3D11Device* device)
    {
        if (!device)
        {
            ALICE_LOG_ERRORF("ResourceLoader<SRV>: Device is null. \"%s\"", path.string().c_str());
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outSrv = nullptr;

        // 1. ResourceManager의 자동 로드(암호화/경로 처리) 기능을 사용하여 바이너리 확보
        std::vector<std::uint8_t> data;
        if (!rm.LoadBinaryAuto(path, data) || data.empty())
        {
            ALICE_LOG_ERRORF("ResourceLoader<SRV>: Failed to load binary. \"%s\"", path.string().c_str());
            return nullptr;
        }

        // 2. 확장자를 확인하여 WIC 또는 DDS 로드 시도
        std::filesystem::path ext = path.extension();
        std::string extLower = ext.string();
        for (auto& c : extLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        HRESULT hr = E_FAIL;

        // DDS 파일인 경우
        if (extLower == ".dds")
        {
            hr = DirectX::CreateDDSTextureFromMemory(
                device,
                data.data(),
                static_cast<size_t>(data.size()),
                nullptr, // texture resource 필요시 인자 추가
                outSrv.GetAddressOf()
            );
        }
        else
        {
            // WIC로 로드 시도 (JPG, PNG, TGA 등)
            hr = DirectX::CreateWICTextureFromMemory(
                device,
                data.data(),
                static_cast<size_t>(data.size()),
                nullptr, // texture resource 필요시 인자 추가
                outSrv.GetAddressOf()
            );
        }

        if (FAILED(hr))
        {
            ALICE_LOG_ERRORF("ResourceLoader<SRV>: Failed to create texture from memory. \"%s\" HRESULT=0x%08X", 
                path.string().c_str(), static_cast<unsigned int>(hr));
            return nullptr;
        }

        return outSrv;
    }
}

