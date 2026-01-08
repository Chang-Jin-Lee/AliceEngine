#pragma once

#include <DirectXMath.h>
#include <string>

namespace Alice {
    /// 머티리얼 컴포넌트
    /// - 현재는 베이스 컬러 + 러프니스/메탈니스만 가집니다.
    /// - 추후 더 많은 파라미터를 확장할 수 있습니다.
    struct MaterialComponent 
    {
        DirectX::XMFLOAT3 color{ 0.7f, 0.7f, 0.7f }; // 베이스 색상 (albedo)
        float roughness{ 0.5f };                     // 0~1 러프니스 (PBR)
        float metalness{ 0.0f };                     // 0~1 메탈니스 (PBR)
        std::string assetPath;                     // 선택된 머티리얼 에셋 경로 (옵션)
        std::string albedoTexturePath; // 알베도 텍스처 경로 (.alice 또는 원본)
    };
}