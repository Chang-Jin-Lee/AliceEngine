#include "Audio/SoundManager.h"

#include <fmod.hpp>
#include <fmod_errors.h>
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
    FMOD::ChannelGroup* g_MasterGroup = nullptr;
    FMOD::ChannelGroup* g_BgmGroup = nullptr;
    FMOD::ChannelGroup* g_SfxGroup = nullptr;

    struct SoundData
    {
        FMOD::Sound* fmodSound = nullptr;
        std::vector<std::uint8_t> memoryBuffer; // 메모리 로드 시 버퍼 유지용
    };
    std::map<std::wstring, SoundData> g_SoundBank;

    FMOD::Channel* g_ChannelBGM = nullptr;
    float g_VolBGM = 1.0f;
    std::wstring g_CurrentBGMKey;

    struct SfxEntry
    {
        FMOD::Channel* channel = nullptr;
        bool loop = false;
    };
    std::map<std::wstring, SfxEntry> g_SfxChannels;
    std::vector<FMOD::Channel*> g_ChannelsSFX; // OneShot SFX 채널들 (중첩 재생용)
    float g_VolSFX = 1.0f;
    float g_PitchSFX = 1.0f;

    struct Inst3D { FMOD::Channel* ch = nullptr; };
    std::map<std::wstring, Inst3D> g_Inst3D;

    inline bool Check(FMOD_RESULT r, const char* msg = "")
    {
        if (r != FMOD_OK)
        {
            ALICE_LOG_ERRORF("[FMOD Error] %s: %s", msg, FMOD_ErrorString(r));
            return false;
        }
        return true;
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

        FMOD_MODE mode = FMOD_OPENMEMORY;
        mode |= (type == Alice::Sound::Type::BGM) ? (FMOD_CREATESTREAM | FMOD_LOOP_NORMAL) : (FMOD_CREATESAMPLE | FMOD_LOOP_OFF);
        if (type != Alice::Sound::Type::BGM) mode |= FMOD_3D; // SFX는 3D 지원

        FMOD::Sound* newSound = nullptr;
        FMOD_RESULT r = g_System->createSound(reinterpret_cast<const char*>(bytes.data()), mode, &exinfo, &newSound);
        if (!Check(r, "CreateSound") || !newSound) return false;

        SoundData data;
        data.fmodSound = newSound;
        data.memoryBuffer = bytes; // FMOD가 데이터를 참조하므로 버퍼 유지 필수
        g_SoundBank[key] = std::move(data);
        return true;
    }
}

namespace Alice::Sound
{
    bool Initialize()
    {
        if (g_System) return true;
        
        FMOD_RESULT r = FMOD::System_Create(&g_System);
        if (!Check(r, "System Create") || !g_System) return false;

        // 최대 채널 512, 초기화 플래그
        r = g_System->init(512, FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED, nullptr);
        if (!Check(r, "System Init"))
        {
            g_System->release();
            g_System = nullptr;
            return false;
        }

        // 채널 그룹 생성
        Check(g_System->getMasterChannelGroup(&g_MasterGroup), "Get Master Group");
        Check(g_System->createChannelGroup("BGM", &g_BgmGroup), "Create BGM Group");
        Check(g_System->createChannelGroup("SFX", &g_SfxGroup), "Create SFX Group");

        // 그룹 계층 설정 (Master 하위에 BGM, SFX)
        if (g_MasterGroup)
        {
            if (g_BgmGroup) g_MasterGroup->addGroup(g_BgmGroup);
            if (g_SfxGroup) g_MasterGroup->addGroup(g_SfxGroup);
        }

        // 3D 설정 (거리 계수 1.0)
        g_System->set3DSettings(1.0f, 1.0f, 1.0f);

        ALICE_LOG_INFO("[SoundManager] Initialized FMOD System with ChannelGroups.");
        return true;
    }

    void Shutdown()
    {
        // 모든 소리 정지
        if (g_MasterGroup) g_MasterGroup->stop();

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
        g_ChannelsSFX.clear();

        // 사운드 해제
        for (auto& pair : g_SoundBank)
        {
            if (pair.second.fmodSound)
            {
                pair.second.fmodSound->release();
                pair.second.fmodSound = nullptr;
            }
        }
        g_SoundBank.clear();

        // 그룹 해제
        if (g_BgmGroup) { g_BgmGroup->release(); g_BgmGroup = nullptr; }
        if (g_SfxGroup) { g_SfxGroup->release(); g_SfxGroup = nullptr; }

        if (g_System)
        {
            g_System->close();
            g_System->release();
            g_System = nullptr;
        }

        ALICE_LOG_INFO("[SoundManager] Shutdown.");
    }

    void Update()
    {
        if (!g_System) return;
        
        // 죽은 채널 정리 (3D 인스턴스 맵에서)
        auto it = g_Inst3D.begin();
        while (it != g_Inst3D.end())
        {
            bool playing = false;
            if (it->second.ch)
            {
                it->second.ch->isPlaying(&playing);
            }

            if (!playing)
            {
                it = g_Inst3D.erase(it);
            }
            else
            {
                ++it;
            }
        }

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

        g_SoundBank[key].fmodSound = newSound;
        return true;
    }

    bool LoadAuto(const ResourceManager& resources,
                  const std::wstring& key,
                  const std::filesystem::path& logicalPath,
                  Type type)
    {
        if (!g_System && !Initialize()) return false;
        if (key.empty()) return false;

        // 이미 로드됨
        if (g_SoundBank.find(key) != g_SoundBank.end()) return true;

        // 1. ResourceManager를 통해 바이너리 로드 (경로 문제 해결)
        std::vector<std::uint8_t> bytes;
        
        // Resource/Sound/File.wav 같은 경로는 ResourceManager가 알아서 
        // EditorMode: Project/Resource/Sound/File.wav
        // GameMode: Cooked/Chunks/...
        // 로 매핑해줍니다.
        if (!resources.LoadBinaryAuto(logicalPath, bytes) || bytes.empty())
        {
            ALICE_LOG_ERRORF("[SoundManager] LoadAuto Failed: Path=\"%s\" Key=\"%ls\"", 
                logicalPath.string().c_str(), key.c_str());
            return false;
        }

        // 2. FMOD 사운드 생성 (메모리 방식)
        if (!CreateSoundFromMemory(key, bytes, type))
        {
            ALICE_LOG_ERRORF("[SoundManager] CreateSoundFromMemory Failed: Key=\"%ls\" Size=%zu", 
                key.c_str(), bytes.size());
            return false;
        }

        ALICE_LOG_INFO("[SoundManager] Loaded: Key=\"%ls\" Path=\"%s\" Size=%zu", 
            key.c_str(), logicalPath.string().c_str(), bytes.size());
        return true;
    }

    void SetMasterVolume(float volume)
    {
        if (g_MasterGroup) g_MasterGroup->setVolume(std::clamp(volume, 0.0f, 1.0f));
    }

    void SetBGMVolume(float volume)
    {
        g_VolBGM = std::clamp(volume, 0.0f, 1.0f);
        if (g_BgmGroup) g_BgmGroup->setVolume(g_VolBGM);
        if (g_ChannelBGM) g_ChannelBGM->setVolume(g_VolBGM);
    }

    void SetSFXVolume(float volume)
    {
        g_VolSFX = std::clamp(volume, 0.0f, 1.0f);
        if (g_SfxGroup) g_SfxGroup->setVolume(g_VolSFX);
    }

    void PauseAll(bool pause)
    {
        if (g_MasterGroup) g_MasterGroup->setPaused(pause);
    }

    void PlayBGM(const std::wstring& key, float /*fadeTime*/)
    {
        if (!g_System) return;
        
        // 이미 재생 중이면 무시
        if (g_CurrentBGMKey == key && IsBGMPlaying()) return;

        StopBGM(0.0f); // 이전 BGM 정지

        auto it = g_SoundBank.find(key);
        if (it == g_SoundBank.end())
        {
            ALICE_LOG_WARN("[SoundManager] PlayBGM Failed: Key not found \"%ls\"", key.c_str());
            return;
        }

        FMOD_RESULT r = g_System->playSound(it->second.fmodSound, g_BgmGroup, false, &g_ChannelBGM);
        if (!Check(r, "PlayBGM")) return;
        if (g_ChannelBGM) g_ChannelBGM->setVolume(g_VolBGM);
        g_CurrentBGMKey = key;
    }

    void PauseBGM(bool pause)
    {
        if (!g_ChannelBGM) return;
        g_ChannelBGM->setPaused(pause);
    }

    void StopBGM(float /*fadeTime*/)
    {
        if (g_ChannelBGM)
        {
            g_ChannelBGM->stop(); // FadeOut 구현 생략 (필요 시 DSP 사용)
            g_ChannelBGM = nullptr;
        }
        g_CurrentBGMKey.clear();
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
        if (it == g_SoundBank.end() || !it->second.fmodSound) return 0.0f;
        unsigned int ms = 0;
        if (it->second.fmodSound->getLength(&ms, FMOD_TIMEUNIT_MS) != FMOD_OK) return 0.0f;
        return (float)ms / 1000.0f;
    }

    std::wstring GetCurrentBGMKey()
    {
        return g_CurrentBGMKey;
    }

    void PlaySFX(const std::wstring& key, float volume, float pitch, bool loop)
    {
        if (!g_System) return;

        auto it = g_SoundBank.find(key);
        if (it == g_SoundBank.end())
        {
            ALICE_LOG_WARN("[SoundManager] PlaySFX Failed: Key not found \"%ls\"", key.c_str());
            return;
        }

        FMOD::Channel* channel = nullptr;
        FMOD_RESULT r = g_System->playSound(it->second.fmodSound, g_SfxGroup, true, &channel); // Paused 상태로 시작
        
        if (!Check(r, "PlaySFX") || !channel) return;

        channel->setVolume(std::clamp(volume, 0.0f, 1.0f) * g_VolSFX);
        channel->setPitch(std::clamp(pitch, 0.5f, 2.0f) * g_PitchSFX);
        channel->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        channel->setPaused(false); // 설정 후 재생

        // Loop인 경우에만 추적 (중첩 재생 방지), OneShot은 Fire-and-forget
        if (loop)
        {
            g_SfxChannels[key] = { channel, true };
        }
        else
        {
            // OneShot: 중첩 재생 가능 (추적만 하고 제어는 안함)
            g_ChannelsSFX.push_back(channel);
        }
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

    bool Play3D(const std::wstring& instanceId, const std::wstring& key,
                const DirectX::XMFLOAT3& pos, float volume, float pitch, bool loop)
    {
        if (!g_System) return false;

        auto it = g_SoundBank.find(key);
        if (it == g_SoundBank.end()) return false;

        // 이미 재생 중인 인스턴스 확인
        // Loop 사운드는 중복 재생 방지 (기존 것 유지)
        if (loop)
        {
            auto instIt = g_Inst3D.find(instanceId);
            if (instIt != g_Inst3D.end())
            {
                bool playing = false;
                if (instIt->second.ch)
                {
                    instIt->second.ch->isPlaying(&playing);
                }
                if (playing) return true; // 이미 재생 중
            }
        }

        FMOD::Channel* channel = nullptr;
        FMOD_RESULT r = g_System->playSound(it->second.fmodSound, g_SfxGroup, true, &channel);
        
        if (!Check(r, "Play3D") || !channel) return false;

        FMOD_VECTOR p = ToFmod(pos);
        FMOD_VECTOR v = { 0, 0, 0 };
        channel->set3DAttributes(&p, &v);
        channel->setVolume(std::clamp(volume, 0.0f, 1.0f));
        channel->setPitch(std::clamp(pitch, 0.5f, 2.0f));
        channel->setMode(loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        channel->set3DMinMaxDistance(1.0f, 50.0f); // 기본값
        
        channel->setPaused(false);

        // Loop나 추적이 필요한 사운드만 맵에 저장
        if (loop || !instanceId.empty())
        {
            g_Inst3D[instanceId] = { channel };
        }

        return true;
    }

    void Stop3D(const std::wstring& instanceId)
    {
        auto it = g_Inst3D.find(instanceId);
        if (it != g_Inst3D.end())
        {
            if (it->second.ch) it->second.ch->stop();
            g_Inst3D.erase(it);
        }
    }

    void Update3D(const std::wstring& instanceId,
                  const DirectX::XMFLOAT3& pos,
                  float volume,
                  float minDistance,
                  float maxDistance)
    {
        auto it = g_Inst3D.find(instanceId);
        if (it != g_Inst3D.end() && it->second.ch)
        {
            FMOD_VECTOR p = ToFmod(pos);
            FMOD_VECTOR v = { 0, 0, 0 };
            it->second.ch->set3DAttributes(&p, &v);
            it->second.ch->setVolume(std::clamp(volume, 0.0f, 1.0f));
            it->second.ch->set3DMinMaxDistance(std::max(0.1f, minDistance), std::max(minDistance, maxDistance));
        }
    }

    // 하위 호환성 (기존 코드용)
    bool Play3DInstance(const std::wstring& instanceId, const std::wstring& soundKey, bool loop)
    {
        return Play3D(instanceId, soundKey, DirectX::XMFLOAT3(0, 0, 0), 1.0f, 1.0f, loop);
    }

    void Stop3DInstance(const std::wstring& instanceId)
    {
        Stop3D(instanceId);
    }

    void Update3DInstance(const std::wstring& instanceId,
                          const DirectX::XMFLOAT3& pos,
                          float volume01,
                          float minDist,
                          float maxDist)
    {
        Update3D(instanceId, pos, volume01, minDist, maxDist);
    }
}

