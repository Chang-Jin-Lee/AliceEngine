#include "Audio/SoundManager.h"

#include <fmod.hpp>
#include <map>
#include <vector>
#include <algorithm>

#include "Core/Helper.h"
#include "Core/Logger.h"
#include "Core/ResourceManager.h"

#pragma comment(lib, "fmod_vc.lib")

namespace
{
    FMOD::System* g_System = nullptr;

    std::map<std::wstring, FMOD::Sound*> g_SoundBank;
    std::map<std::wstring, std::vector<std::uint8_t>> g_SoundMemory;

    FMOD::Channel* g_ChannelBGM = nullptr;
    float g_VolBGM = 1.0f;
    std::wstring g_CurrentBGMKey;

    struct SfxEntry
    {
        FMOD::Channel* channel = nullptr;
        bool loop = false;
    };
    std::map<std::wstring, SfxEntry> g_SfxChannels;
    std::vector<FMOD::Channel*> g_ChannelsSFX;
    float g_VolSFX = 1.0f;
    float g_PitchSFX = 1.0f;

    struct Inst3D { FMOD::Channel* ch = nullptr; };
    std::map<std::wstring, Inst3D> g_Inst3D;

    inline bool Check(FMOD_RESULT r)
    {
        return (r == FMOD_OK);
    }

    inline FMOD_VECTOR ToFmod(const DirectX::XMFLOAT3& v) { return FMOD_VECTOR{ v.x, v.y, v.z }; }
    inline FMOD_VECTOR ToFmod(DirectX::XMVECTOR v)
    {
        DirectX::XMFLOAT3 f3;
        DirectX::XMStoreFloat3(&f3, v);
        return FMOD_VECTOR{ f3.x, f3.y, f3.z };
    }

    void CleanupSFX()
    {
        if (!g_ChannelsSFX.empty())
        {
            auto it = std::remove_if(g_ChannelsSFX.begin(), g_ChannelsSFX.end(),
                [](FMOD::Channel* c) {
                    if (!c) return true;
                    bool playing = false;
                    FMOD_RESULT r = c->isPlaying(&playing);
                    return (r != FMOD_OK) || !playing;
                });
            g_ChannelsSFX.erase(it, g_ChannelsSFX.end());
        }

        for (auto it = g_SfxChannels.begin(); it != g_SfxChannels.end();)
        {
            bool remove = false;
            if (it->second.channel)
            {
                bool playing = false;
                FMOD_RESULT r = it->second.channel->isPlaying(&playing);
                if (r != FMOD_OK || !playing)
                {
                    it->second.channel = nullptr;
                    if (!it->second.loop) remove = true;
                }
            }
            else
            {
                if (!it->second.loop) remove = true;
            }

            if (remove) it = g_SfxChannels.erase(it);
            else ++it;
        }
    }

    bool CreateSoundFromMemory(const std::wstring& key, const std::vector<std::uint8_t>& bytes, Alice::Sound::Type type)
    {
        if (!g_System) return false;
        if (bytes.empty()) return false;

        FMOD_CREATESOUNDEXINFO exinfo{};
        exinfo.cbsize = sizeof(exinfo);
        exinfo.length = static_cast<unsigned int>(bytes.size());

        const FMOD_MODE mode = (type == Alice::Sound::Type::BGM) ? (FMOD_CREATESTREAM | FMOD_OPENMEMORY) : FMOD_OPENMEMORY;

        FMOD::Sound* newSound = nullptr;
        FMOD_RESULT r = g_System->createSound(reinterpret_cast<const char*>(bytes.data()), mode, &exinfo, &newSound);
        if (!Check(r) || !newSound) return false;

        g_SoundBank[key] = newSound;
        return true;
    }
}

namespace Alice::Sound
{
    bool Initialize()
    {
        if (g_System) return true;
        FMOD_RESULT r = FMOD::System_Create(&g_System);
        if (!Check(r) || !g_System) return false;

        r = g_System->init(512, FMOD_INIT_NORMAL, nullptr);
        if (!Check(r))
        {
            g_System->release();
            g_System = nullptr;
            return false;
        }

        g_System->set3DSettings(1.0f, 1.0f, 1.0f);
        return true;
    }

    void Shutdown()
    {
        StopBGM();
        StopAllSFX();

        for (auto& pair : g_Inst3D)
        {
            if (pair.second.ch)
            {
                pair.second.ch->stop();
                pair.second.ch = nullptr;
            }
        }
        g_Inst3D.clear();

        for (auto& pair : g_SfxChannels)
        {
            if (pair.second.channel)
            {
                pair.second.channel->stop();
                pair.second.channel = nullptr;
            }
        }
        g_SfxChannels.clear();

        for (auto& pair : g_SoundBank)
        {
            if (pair.second) pair.second->release();
        }
        g_SoundBank.clear();
        g_SoundMemory.clear();

        if (g_System)
        {
            g_System->close();
            g_System->release();
            g_System = nullptr;
        }
    }

    void Update()
    {
        if (!g_System) return;
        g_System->update();
        CleanupSFX();
    }

    bool Load(const std::wstring& key, const std::wstring& path, Type type)
    {
        if (!g_System && !Initialize()) return false;

        if (g_SoundBank.find(key) != g_SoundBank.end())
            return true;

        FMOD_MODE mode = (type == Type::BGM) ? FMOD_CREATESTREAM : FMOD_DEFAULT;
        FMOD::Sound* newSound = nullptr;

        FMOD_RESULT r = g_System->createSound(Utf8FromWString(path).c_str(), mode, nullptr, &newSound);
        if (!Check(r) || !newSound) return false;

        g_SoundBank[key] = newSound;
        return true;
    }

    bool LoadAuto(const ResourceManager& resources,
                  const std::wstring& key,
                  const std::filesystem::path& logicalPath,
                  Type type)
    {
        if (!g_System && !Initialize()) return false;
        if (g_SoundBank.find(key) != g_SoundBank.end())
            return true;

        std::vector<std::uint8_t> bytes;
        if (!resources.LoadBinaryAuto(logicalPath, bytes) || bytes.empty())
        {
            ALICE_LOG_ERRORF("[Sound] LoadAuto failed: %s", logicalPath.generic_string().c_str());
            return false;
        }

        g_SoundMemory[key] = std::move(bytes);
        return CreateSoundFromMemory(key, g_SoundMemory[key], type);
    }

    void PlayBGM(const std::wstring& key)
    {
        if (!g_System) return;
        auto it = g_SoundBank.find(key);
        if (it == g_SoundBank.end()) return;

        if (g_ChannelBGM)
        {
            g_ChannelBGM->stop();
            g_ChannelBGM = nullptr;
        }

        FMOD_RESULT r = g_System->playSound(it->second, nullptr, false, &g_ChannelBGM);
        if (!Check(r)) return;
        if (g_ChannelBGM) g_ChannelBGM->setVolume(g_VolBGM);
        g_CurrentBGMKey = key;
    }

    void PauseBGM(bool pause)
    {
        if (!g_ChannelBGM) return;
        g_ChannelBGM->setPaused(pause);
    }

    void StopBGM()
    {
        if (!g_ChannelBGM) return;
        g_ChannelBGM->stop();
        g_ChannelBGM = nullptr;
        g_CurrentBGMKey.clear();
    }

    void SetBGMVolume(float volume)
    {
        g_VolBGM = std::clamp(volume, 0.0f, 1.0f);
        if (g_ChannelBGM) g_ChannelBGM->setVolume(g_VolBGM);
    }

    bool IsBGMPlaying()
    {
        if (!g_ChannelBGM) return false;
        bool playing = false;
        return (g_ChannelBGM->isPlaying(&playing) == FMOD_OK) && playing;
    }

    bool IsBGMPaused()
    {
        if (!g_ChannelBGM) return false;
        bool paused = false;
        return (g_ChannelBGM->getPaused(&paused) == FMOD_OK) && paused;
    }

    bool SetBGMTimeSeconds(float sec)
    {
        if (!g_ChannelBGM) return false;
        return (g_ChannelBGM->setPosition((unsigned int)(sec * 1000.0f), FMOD_TIMEUNIT_MS) == FMOD_OK);
    }

    float GetBGMTimeSeconds()
    {
        if (!g_ChannelBGM) return 0.0f;
        unsigned int ms = 0;
        if (g_ChannelBGM->getPosition(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0.0f;
        return (float)ms / 1000.0f;
    }

    float GetBGMLengthSeconds()
    {
        if (!g_ChannelBGM || g_CurrentBGMKey.empty()) return 0.0f;
        auto it = g_SoundBank.find(g_CurrentBGMKey);
        if (it == g_SoundBank.end()) return 0.0f;
        unsigned int ms = 0;
        if (it->second->getLength(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0.0f;
        return (float)ms / 1000.0f;
    }

    std::wstring GetCurrentBGMKey()
    {
        return g_CurrentBGMKey;
    }

    bool PlaySFX(const std::wstring& key, float volume, float pitch, bool loop)
    {
        if (!g_System) return false;
        auto it = g_SoundBank.find(key);
        if (it == g_SoundBank.end()) return false;

        FMOD::Channel* ch = nullptr;
        FMOD_RESULT r = g_System->playSound(it->second, nullptr, false, &ch);
        if (!Check(r) || !ch) return false;

        ch->setVolume(std::clamp(volume, 0.0f, 1.0f) * g_VolSFX);
        ch->setPitch(std::clamp(pitch, 0.5f, 2.0f) * g_PitchSFX);
        ch->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);

        if (loop)
        {
            g_SfxChannels[key] = { ch, true };
        }
        else
        {
            g_ChannelsSFX.push_back(ch);
        }
        return true;
    }

    bool IsSfxPlaying(const std::wstring& key)
    {
        auto it = g_SfxChannels.find(key);
        if (it == g_SfxChannels.end() || !it->second.channel) return false;
        bool playing = false;
        return (it->second.channel->isPlaying(&playing) == FMOD_OK) && playing;
    }

    void StopSfx(const std::wstring& key)
    {
        auto it = g_SfxChannels.find(key);
        if (it != g_SfxChannels.end() && it->second.channel)
        {
            it->second.channel->stop();
            it->second.channel = nullptr;
        }
    }

    void StopAllSFX()
    {
        for (auto& ch : g_ChannelsSFX)
            if (ch) ch->stop();
        g_ChannelsSFX.clear();

        for (auto& pair : g_SfxChannels)
            if (pair.second.channel) pair.second.channel->stop();
        g_SfxChannels.clear();
    }

    void StopLastSFX()
    {
        if (g_ChannelsSFX.empty()) return;
        FMOD::Channel* ch = g_ChannelsSFX.back();
        if (ch) ch->stop();
        g_ChannelsSFX.pop_back();
    }

    void SetSFXVolume(float volume)
    {
        g_VolSFX = std::clamp(volume, 0.0f, 1.0f);
    }

    void SetSFXPitch(float pitch)
    {
        g_PitchSFX = std::clamp(pitch, 0.5f, 2.0f);
    }

    void SetSfxVolume(const std::wstring& key, float volume)
    {
        auto it = g_SfxChannels.find(key);
        if (it != g_SfxChannels.end() && it->second.channel)
            it->second.channel->setVolume(std::clamp(volume, 0.0f, 1.0f) * g_VolSFX);
    }

    void SetSfxPitch(const std::wstring& key, float pitch)
    {
        auto it = g_SfxChannels.find(key);
        if (it != g_SfxChannels.end() && it->second.channel)
            it->second.channel->setPitch(std::clamp(pitch, 0.5f, 2.0f) * g_PitchSFX);
    }

    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up)
    {
        if (!g_System) return;
        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = ToFmod(vel);
        FMOD_VECTOR f = ToFmod(forward);
        FMOD_VECTOR u = ToFmod(up);
        g_System->set3DListenerAttributes(0, &p, &v, &f, &u);
    }

    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMVECTOR& forward, const DirectX::XMVECTOR& up)
    {
        if (!g_System) return;
        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = ToFmod(vel);
        FMOD_VECTOR f = ToFmod(forward);
        FMOD_VECTOR u = ToFmod(up);
        g_System->set3DListenerAttributes(0, &p, &v, &f, &u);
    }

    bool Play3DInstance(const std::wstring& instanceId, const std::wstring& soundKey, bool loop)
    {
        if (!g_System) return false;
        auto it = g_SoundBank.find(soundKey);
        if (it == g_SoundBank.end()) return false;

        FMOD::Channel* ch = nullptr;
        FMOD_RESULT r = g_System->playSound(it->second, nullptr, false, &ch);
        if (!Check(r) || !ch) return false;
        ch->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        g_Inst3D[instanceId].ch = ch;
        return true;
    }

    void Stop3DInstance(const std::wstring& instanceId)
    {
        auto it = g_Inst3D.find(instanceId);
        if (it == g_Inst3D.end()) return;
        if (it->second.ch) it->second.ch->stop();
        it->second.ch = nullptr;
    }

    void Update3DInstance(const std::wstring& instanceId,
                          const DirectX::XMFLOAT3& pos,
                          float volume01,
                          float minDist,
                          float maxDist)
    {
        auto it = g_Inst3D.find(instanceId);
        if (it == g_Inst3D.end() || !it->second.ch) return;

        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v{ 0.0f, 0.0f, 0.0f };
        it->second.ch->set3DAttributes(&p, &v);
        it->second.ch->setVolume(std::clamp(volume01, 0.0f, 1.0f));
        it->second.ch->set3DMinMaxDistance(std::max(0.01f, minDist), std::max(minDist, maxDist));
    }
}

