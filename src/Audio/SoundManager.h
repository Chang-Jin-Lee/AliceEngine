#pragma once

#include <string>
#include <filesystem>
#include <DirectXMath.h>

namespace Alice
{
    class ResourceManager;
}

namespace Alice::Sound
{
    enum class Type
    {
        BGM,
        SFX
    };

    bool Initialize();
    void Shutdown();
    void Update();

    bool Load(const std::wstring& key, const std::wstring& path, Type type);
    bool LoadAuto(const ResourceManager& resources,
                  const std::wstring& key,
                  const std::filesystem::path& logicalPath,
                  Type type);

    // BGM
    void PlayBGM(const std::wstring& key);
    void PauseBGM(bool pause);
    void StopBGM();
    void SetBGMVolume(float volume);
    bool IsBGMPlaying();
    bool IsBGMPaused();
    bool SetBGMTimeSeconds(float sec);
    float GetBGMTimeSeconds();
    float GetBGMLengthSeconds();
    std::wstring GetCurrentBGMKey();

    // SFX
    bool PlaySFX(const std::wstring& key, float volume = 1.0f, float pitch = 1.0f, bool loop = false);
    bool IsSfxPlaying(const std::wstring& key);
    void StopSfx(const std::wstring& key);
    void StopAllSFX();
    void StopLastSFX();
    void SetSFXVolume(float volume);
    void SetSFXPitch(float pitch);
    void SetSfxVolume(const std::wstring& key, float volume);
    void SetSfxPitch(const std::wstring& key, float pitch);

    // 3D
    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up);
    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMVECTOR& forward, const DirectX::XMVECTOR& up);
    bool Play3DInstance(const std::wstring& instanceId, const std::wstring& soundKey, bool loop = true);
    void Stop3DInstance(const std::wstring& instanceId);
    void Update3DInstance(const std::wstring& instanceId,
                          const DirectX::XMFLOAT3& pos,
                          float volume01,
                          float minDist,
                          float maxDist);
}

