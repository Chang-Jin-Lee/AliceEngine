# UIWorldManager Engine 통합 가이드

## ✅ 완료된 작업

1. **Engine::Impl에 UIWorldManager 참조 추가**
   - `UIWorldManager* m_uiWorldManager{ nullptr };` 추가됨

2. **Engine::Initialize()에 UI 초기화 코드 추가** (주석 처리됨)
   - UIWorldManager 헤더 포함 후 주석 해제 필요

3. **Engine::Update()에 UI 업데이트 추가**
   - `pImpl->m_uiWorldManager->Update(width, height);` 호출

4. **Engine::Render()에 UI 렌더링 추가**
   - 톤매핑 이후 UI 렌더링 호출

5. **Engine::OnResize()에 UI 리사이즈 처리 추가**

---

## 📋 추가 작업 필요

### 1. UIWorldManager 헤더 포함

`Engine.cpp` 파일 상단에 UIWorldManager 헤더를 포함하세요:

```cpp
// Engine.cpp 상단에 추가
#include "Core/UIWorldManager.h"  // 또는 실제 경로
```

그리고 `Engine::Initialize()`에서 주석 처리된 부분을 해제:

```cpp
// ============================================= UI 시스템 =============================================
if (auto* uiManager = UIWorldManager::GetInstance())
{
    auto* device = pImpl->m_renderDevice->GetDevice();
    auto* context = pImpl->m_renderDevice->GetImmediateContext();
    if (device && context)
    {
        uiManager->Initalize(device, context, pImpl->m_width, pImpl->m_height, pImpl->m_inputSystem);
        pImpl->m_uiWorldManager = uiManager;
        ALICE_LOG_INFO("Engine::Initialize: UIWorldManager initialized.");
    }
}
```

### 2. RenderUITex 함수 추가

사용자가 제공한 `RenderUITex` 함수를 Engine에 추가해야 합니다. 두 가지 옵션이 있습니다:

#### 옵션 A: Engine의 private 멤버 함수로 추가

`Engine.h`에 선언:
```cpp
private:
    void RenderUITex();  // UI 텍스처를 최종 렌더 타겟에 합성
```

`Engine.cpp`에 구현:
```cpp
void Engine::RenderUITex()
{
    if (!pImpl->m_uiWorldManager || !pImpl->m_renderDevice) return;
    
    auto* context = pImpl->m_renderDevice->GetImmediateContext();
    if (!context) return;

    // 백버퍼를 렌더 타겟으로 설정
    ID3D11RenderTargetView* backBufferRTV = pImpl->m_renderDevice->GetBackBufferRTV();
    if (!backBufferRTV) return;

    context->OMSetRenderTargets(1, &backBufferRTV, nullptr);

    // 알파 블렌딩 설정
    // TODO: 알파 블렌드 상태를 Engine::Impl에 저장하거나 가져오기
    // float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    // context->OMSetBlendState(m_pAlphaBlendState, blendFactor, 0xffffffff);

    // 뷰포트 설정
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(pImpl->m_width);
    viewport.Height = static_cast<float>(pImpl->m_height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    // 깊이 테스트 비활성화
    context->OMSetDepthStencilState(nullptr, 0);

    // UI 텍스처는 이미 UIWorldManager::Render()에서 SRV로 바인딩됨
    // 여기서는 풀스크린 쿼드를 그리기만 하면 됨

    // TODO: 풀스크린 쿼드 렌더링
    // - Vertex Shader: m_pHDRVertexShader (또는 풀스크린 쿼드용)
    // - Pixel Shader: m_pUIPixelShader
    // - Draw(6, 0) - 풀스크린 쿼드 (2개 삼각형)

    // 알파 블렌딩 해제
    // context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

    // SRV 해제 (UIWorldManager에서 바인딩한 것)
    // ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    // context->PSSetShaderResources(102, 1, nullSRV);  // m_bindSlot = 102
}
```

#### 옵션 B: UIRenderSystem으로 분리 (권장)

`Rendering/UIRenderSystem.h` 생성:
```cpp
#pragma once
#include <d3d11.h>

namespace Alice
{
    class ID3D11RenderDevice;
    class UIWorldManager;

    class UIRenderSystem
    {
    public:
        bool Initialize(ID3D11RenderDevice* renderDevice);
        void Render(UIWorldManager* uiManager, ID3D11RenderTargetView* targetRTV, UINT width, UINT height);
        void Shutdown();

    private:
        // 알파 블렌드 상태, 셰이더 등 저장
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_alphaBlendState;
        // ... 기타 리소스
    };
}
```

### 3. 필요한 리소스 준비

`RenderUITex` 함수에서 사용하는 리소스들:

1. **알파 블렌드 상태** (`m_pAlphaBlendState`)
   - Engine::Impl에 저장하거나 UIRenderSystem에 저장

2. **셰이더**
   - `m_pHDRVertexShader` - 풀스크린 쿼드용 버텍스 셰이더
   - `m_pUIPixelShader` - UI 텍스처를 그리는 픽셀 셰이더

3. **풀스크린 쿼드**
   - 버텍스 버퍼 또는 Draw(6, 0)로 직접 그리기

### 4. 렌더링 순서 확인

현재 렌더링 순서:
1. 게임 렌더링 (Forward/Deferred)
2. 톤매핑 (게임 모드에서만)
3. **UI 렌더링** ← 여기에 추가됨
4. 디버그 드로우
5. ImGui (에디터 모드)

---

## 🔧 수정 사항

### Engine.cpp의 Render() 함수

현재 코드:
```cpp
// ============================================= UI 렌더링 =============================================
if (pImpl->m_uiWorldManager)
{
    auto* context = pImpl->m_renderDevice->GetImmediateContext();
    if (context)
    {
        pImpl->m_uiWorldManager->Render();
        // TODO: RenderUITex 호출
    }
}
```

수정 후:
```cpp
// ============================================= UI 렌더링 =============================================
if (pImpl->m_uiWorldManager)
{
    // 1. UIWorldManager::Render() - D2D로 UI를 텍스처에 렌더링
    pImpl->m_uiWorldManager->Render();
    
    // 2. RenderUITex() - UI 텍스처를 최종 렌더 타겟에 합성
    RenderUITex();
}
```

---

## 📝 체크리스트

- [ ] UIWorldManager 헤더 경로 확인 및 포함
- [ ] Engine::Initialize()에서 UI 초기화 주석 해제
- [ ] RenderUITex 함수 구현 (옵션 A 또는 B 선택)
- [ ] 알파 블렌드 상태 생성 및 저장
- [ ] UI 셰이더 로드 (버텍스/픽셀 셰이더)
- [ ] 풀스크린 쿼드 렌더링 로직 구현
- [ ] 테스트 및 디버깅

---

## 💡 참고사항

1. **UIWorldManager는 Singleton**
   - `UIWorldManager::GetInstance()`로 접근
   - Engine::Impl에는 포인터만 저장

2. **UI 렌더링 순서**
   - D2D로 UI를 텍스처에 렌더링 (UIWorldManager::Render())
   - 텍스처를 SRV로 바인딩 (UIWorldManager 내부에서 처리)
   - 풀스크린 쿼드로 최종 렌더 타겟에 합성 (RenderUITex)

3. **리사이즈 처리**
   - UIWorldManager::Update()에서 자동으로 처리되거나
   - 별도 Resize() 함수가 필요할 수 있음
