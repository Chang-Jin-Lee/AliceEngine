#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Core/Entity.h"
#include "Core/Script.h"

namespace Alice
{
    /// 간단한 ECS 스타일의 월드(World) 구현입니다.
    /// - 엔티티 생성/삭제 책임
    /// - Transform / Script / Material 컴포넌트 관리 책임
    ///   (필요 시 다른 컴포넌트 컨테이너를 추가 확장)

    struct TransformComponent
    {
        // 위치, 회전(라디안), 스케일
        DirectX::XMFLOAT3 position { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale    { 1.0f, 1.0f, 1.0f };

        TransformComponent& SetPosition(float x, float y, float z)
        {
            position = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetScale(float x, float y, float z)
        {
            scale = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetRotation(float x, float y, float z)
        {
            rotation = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }
    };

    /// 머티리얼 컴포넌트
    /// - 현재는 베이스 컬러 + 러프니스/메탈니스만 가집니다.
    /// - 추후 더 많은 파라미터를 확장할 수 있습니다.
    struct MaterialComponent
    {
        DirectX::XMFLOAT3 color     { 0.7f, 0.7f, 0.7f };  // 베이스 색상 (albedo)
        float             roughness { 0.5f };              // 0~1 러프니스 (PBR)
        float             metalness { 0.0f };              // 0~1 메탈니스 (PBR)
        std::string       assetPath;                       // 선택된 머티리얼 에셋 경로 (옵션)
        std::string       albedoTexturePath;               // 알베도 텍스처 경로 (.alice 또는 원본)
    };

    /// Skinned FBX 메시에 대한 최소 정보만 담는 컴포넌트입니다.
    /// - 실제 FBX 파싱/애니메이션은 게임(샘플) 레벨에서 처리합니다.
    /// - 엔진은 bone 행렬 배열과 본 개수만 사용합니다.
    struct SkinnedMeshComponent
    {
        std::string meshAssetPath;                         // FBX/메시 에셋 경로 (SkinnedMeshRegistry 키)
        std::string instanceAssetPath;                     // .fbxasset 인스턴스 에셋 경로 (씬/프로젝트 저장용)
        const DirectX::XMFLOAT4X4* boneMatrices { nullptr }; // 외부에서 관리하는 본 행렬 배열
        std::uint32_t              boneCount    { 0 };       // 사용 중인 본 개수
    };

    /// 스키닝 애니메이션 재생 상태(엔티티 단위)
    /// - 실제 평가/팔레트 계산은 SkinnedAnimationSystem 이 수행합니다.
    struct SkinnedAnimationComponent
    {
        int   clipIndex { 0 };   // 현재 재생 클립 인덱스
        bool  playing   { true };
        float speed     { 1.0f }; // 배속(1.0 = 정상)
        double timeSec  { 0.0 };  // 현재 시간(초)

        // CPU 본 팔레트(ForwardRenderSystem이 여기서 읽어 VS CB로 업로드)
        std::vector<DirectX::XMFLOAT4X4> palette;
    };

    class World
    {
    public:
        World() = default;

        void Clear();

        /// 새로운 엔티티를 생성합니다.
        EntityId CreateEntity();

        /// 엔티티를 제거하고, 연결된 컴포넌트도 정리합니다.
        void DestroyEntity(EntityId id);

        /// Transform 컴포넌트를 추가합니다.
        TransformComponent& AddTransform(EntityId id);

        /// Transform 컴포넌트를 가져옵니다. (없으면 nullptr)
        TransformComponent* GetTransform(EntityId id);

        /// Transform 컴포넌트의 읽기 전용 포인터를 가져옵니다.
        const TransformComponent* GetTransform(EntityId id) const;

        /// 현재 등록된 Transform 컴포넌트 목록을 읽기 전용으로 반환합니다.
        /// - 에디터 하이러키 뷰에서 엔티티를 나열할 때 사용합니다.
        const std::unordered_map<EntityId, TransformComponent>& GetTransforms() const { return m_transforms; }

        // ==== Script 컴포넌트 관련 ====

        /// Script 컴포넌트를 추가합니다.
        /// \param id         대상 엔티티 ID
        /// \param scriptName ScriptFactory 에 등록된 스크립트 이름
        ScriptComponent& AddScript(EntityId id, const std::string& scriptName);

        /// Script 컴포넌트를 가져옵니다. (없으면 nullptr)
        ScriptComponent* GetScript(EntityId id);
        const ScriptComponent* GetScript(EntityId id) const;

        /// 전체 Script 컴포넌트 컨테이너 (ScriptSystem 이 사용)
        const std::unordered_map<EntityId, ScriptComponent>& GetScripts() const { return m_scripts; }
        std::unordered_map<EntityId, ScriptComponent>& GetScripts() { return m_scripts; }

        /// Script 컴포넌트를 제거합니다.
        void RemoveScript(EntityId id);

        void RemoveAllScript();

        // ==== Material 컴포넌트 관련 ====

        /// 머티리얼 컴포넌트를 추가합니다.
        /// \param id        대상 엔티티 ID
        /// \param color     기본 베이스 컬러
        /// \param assetPath 이 머티리얼이 참조하는 에셋 경로(선택 사항)
        MaterialComponent& AddMaterial(EntityId id,
                                       const DirectX::XMFLOAT3& color,
                                       const std::string& assetPath = {});

        /// 머티리얼 컴포넌트를 가져옵니다. (없으면 nullptr)
        MaterialComponent* GetMaterial(EntityId id);
        const MaterialComponent* GetMaterial(EntityId id) const;

        /// 전체 머티리얼 컴포넌트 컨테이너 (렌더링/에디터에서 사용)
        const std::unordered_map<EntityId, MaterialComponent>& GetMaterials() const { return m_materials; }

        /// 머티리얼 컴포넌트를 제거합니다.
        void RemoveMaterial(EntityId id);

        // ==== Skinned Mesh 컴포넌트 관련 ====

        /// 스키닝 메시 컴포넌트를 추가합니다.
        SkinnedMeshComponent& AddSkinnedMesh(EntityId id, const std::string& meshAssetPath);

        /// 스키닝 메시 컴포넌트를 가져옵니다. (없으면 nullptr)
        SkinnedMeshComponent* GetSkinnedMesh(EntityId id);
        const SkinnedMeshComponent* GetSkinnedMesh(EntityId id) const;

        /// 전체 스키닝 메시 컨테이너 (렌더링/에디터에서 사용)
        const std::unordered_map<EntityId, SkinnedMeshComponent>& GetSkinnedMeshes() const { return m_skinnedMeshes; }

        /// 스키닝 메시 컴포넌트를 제거합니다.
        void RemoveSkinnedMesh(EntityId id);

        // ==== Skinned Animation 컴포넌트 관련 ====

        SkinnedAnimationComponent& AddSkinnedAnimation(EntityId id);
        SkinnedAnimationComponent* GetSkinnedAnimation(EntityId id);
        const SkinnedAnimationComponent* GetSkinnedAnimation(EntityId id) const;
        const std::unordered_map<EntityId, SkinnedAnimationComponent>& GetSkinnedAnimations() const { return m_skinnedAnimations; }
        void RemoveSkinnedAnimation(EntityId id);

    private:
        EntityId m_nextEntityId { 1 };

        std::unordered_map<EntityId, TransformComponent> m_transforms;
        std::unordered_map<EntityId, ScriptComponent>    m_scripts;
        std::unordered_map<EntityId, MaterialComponent>  m_materials;
        std::unordered_map<EntityId, SkinnedMeshComponent> m_skinnedMeshes;
        std::unordered_map<EntityId, SkinnedAnimationComponent> m_skinnedAnimations;
    };
}


