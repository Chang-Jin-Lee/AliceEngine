#pragma once

namespace Alice
{
    class SceneManager;
    class ResourceManager;
    class SkinnedMeshRegistry;
    class InputSystem;

    enum class KeyCode
    {
        Alpha1,
        Alpha2,
        Alpha3,
        F1,
        F2,
        Space,
    };

    /// 스크립트에서 사용하는 입력 API (GetKeyDown 등)
    class IScriptInput
    {
    public:
        virtual ~IScriptInput() = default;
        virtual bool GetKey(KeyCode key) const = 0;
        virtual bool GetKeyDown(KeyCode key) const = 0;
        virtual bool GetKeyUp(KeyCode key) const = 0;
    };

    /// 스크립트에서 사용하는 씬 전환 API (즉시 로드 대신 "요청" → 프레임 끝에 처리)
    class IScriptScene
    {
    public:
        virtual ~IScriptScene() = default;
        virtual void SwitchTo(const char* sceneName) = 0;              // 코드 씬 (SceneManager::SwitchTo)
        virtual void LoadSceneFile(const char* scenePathUtf8) = 0;      // .scene 파일 로드 (SceneFile::Load)
    };

    struct ScriptServices
    {
        IScriptInput*        input { nullptr };
        IScriptScene*        scene { nullptr };
        SkinnedMeshRegistry* skinnedRegistry { nullptr };
        ResourceManager*     resources { nullptr };
    };
}



