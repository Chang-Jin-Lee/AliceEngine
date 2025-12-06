#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <wrl/client.h>
#include <d3d11.h>

#include "3Dmodel/FbxTypes.h"

namespace Alice
{
    /// GPU 상의 스키닝 메시 1개를 표현하는 구조입니다.
    /// - 정점/인덱스 버퍼 + 정점 포맷 정보
    /// - FBX 로부터 생성된 서브셋/머티리얼/스켈레톤 메타데이터를 함께 보관합니다.
    struct SkinnedMeshGPU
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;

        UINT stride      { 0 };
        UINT indexCount  { 0 };
        UINT startIndex  { 0 };
        INT  baseVertex  { 0 };

        // === FBX 서브셋/머티리얼 ===
        std::vector<FbxSubset> subsets; // 인덱스 범위 + 머티리얼 인덱스
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> materialSRVs; // FBX 기본 디퓨즈 텍스처
        std::vector<std::string> materialOverridePaths; // 에디터에서 교체한 텍스처 경로 (선택 사항)

        // === FBX 스켈레톤 ===
        std::vector<FbxSkeletonNode> skeleton; // 전체 노드 캐시
        int                          skeletonRoot { -1 }; // 루트 인덱스
        std::string                  skeletonText;         // 간단한 트리 텍스트 (Inspector에서 표시)
    };

    /// FBX 로부터 만들어진 스키닝 메시 자산을
    /// 문자열 키(논리 경로)로 보관하는 레지스트리입니다.
    /// - 엔진(Rendering 계층)의 일부로, 게임/에디터 양쪽에서 공유합니다.
    class SkinnedMeshRegistry
    {
    public:
        void Register(const std::string& assetPath,
                      std::shared_ptr<SkinnedMeshGPU> mesh)
        {
            m_meshes[assetPath] = std::move(mesh);
        }

        std::shared_ptr<SkinnedMeshGPU> Find(const std::string& assetPath) const
        {
            auto it = m_meshes.find(assetPath);
            if (it == m_meshes.end())
                return nullptr;
            return it->second;
        }

    private:
        std::unordered_map<std::string, std::shared_ptr<SkinnedMeshGPU>> m_meshes;
    };
}


