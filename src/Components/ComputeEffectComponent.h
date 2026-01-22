#pragma once

#include <DirectXMath.h>
#include <string>

namespace Alice {
    /// 컴퓨트 셰이더 이펙트 컴포넌트
    /// - 엔티티에 컴퓨트 셰이더 이펙트를 적용하기 위한 컴포넌트
    struct ComputeEffectComponent 
    {
        bool enabled{ true };                          // 이펙트 활성화 여부
        std::string shaderName;                        // 사용할 컴퓨트 셰이더 이름
        DirectX::XMFLOAT3 effectParams{ 1.0f, 1.0f, 1.0f }; // 이펙트 파라미터
        float intensity{ 1.0f };                       // 이펙트 강도
    };
}
