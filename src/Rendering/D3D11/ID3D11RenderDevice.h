#pragma once

#include "Rendering/IRenderDevice.h"

// DirectX 11 전방 선언 (헤더 의존성 최소화)
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace Alice
{
    /// Direct3D11 전용 렌더 디바이스 인터페이스입니다.
    /// - IRenderDevice 위에 DX11 타입을 노출하기 위한 얇은 계층입니다.
    struct ID3D11RenderDevice : public IRenderDevice
    {
        virtual ~ID3D11RenderDevice() = default;

        /// 내부 D3D11 디바이스 포인터를 반환합니다.
        virtual ID3D11Device* GetDevice() = 0;

        /// 내부 D3D11 디바이스 컨텍스트 포인터를 반환합니다.
        virtual ID3D11DeviceContext* GetImmediateContext() = 0;
    };
}


