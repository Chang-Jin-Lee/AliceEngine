#include "Core/ResourceManager.h"

#include <fstream>

namespace Alice
{
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

    bool ResourceManager::CookAndSave(const std::filesystem::path& srcPath,
                                      const std::filesystem::path& cookedPath) const
    {
        std::vector<std::uint8_t> data;
        if (!LoadBinary(srcPath, data, false)) return false;

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

