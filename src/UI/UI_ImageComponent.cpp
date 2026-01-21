#include "UI_ImageComponent.h"
#include "UIBase.h"

void UI_ImageComponent::Render()
{
    if (!m_UIRenderStruct) return;
    auto tmpTransform = Owner->GetTransform();
    const D2D1::Matrix3x2F& tmpMat = tmpTransform.ConVertD2DPos();
    

    // 소스 영역 계산 (이미지 내부의 어느 영역을 그릴 것인가)
    m_srcRect = D2D1::RectF(
        m_srcPos.x - SrcWidthHeight.x,
        m_srcPos.y - SrcWidthHeight.y,
        m_srcPos.x + SrcWidthHeight.x,
        m_srcPos.y + SrcWidthHeight.y
    );

    m_UIRenderStruct->m_d2DdevCon->SetTransform(tmpMat);

    // 이미지가 없거나 Index가 0이면 기본 브러시로 사각형 출력
    if (m_nowIndex == 0 || !m_texture)
    {
        m_UIRenderStruct->m_d2DdevCon->FillRectangle(m_rect, m_UIRenderStruct->m_brush.Get());
    }
    else
    {
        // 1장만 로드된 m_texture를 바로 사용
        m_UIRenderStruct->m_d2DdevCon->DrawBitmap(
            m_texture.Get(),
            m_rect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            m_srcRect
        );
    }
}

UINT UI_ImageComponent::SetImagePath(const wchar_t* path, XMFLOAT2& transform_size)
{
    if (!m_UIRenderStruct) return 0;

    m_path = path;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

    // WIC 디코더 생성
    HRESULT hr = m_UIRenderStruct->m_wicImageFactory.Get()->CreateDecoderFromFilename(
        path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return 0;

    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return 0;

    UINT imgWidth = 0, imgHeight = 0;
    frame->GetSize(&imgWidth, &imgHeight);

    // 사이즈 및 중점 설정
    m_size = { (float)imgWidth, (float)imgHeight };
    transform_size = m_size;
    m_srcPos = { (float)imgWidth / 2.0f, (float)imgHeight / 2.0f };
    SrcWidthHeight = m_srcPos;

    // 포맷 컨버터 초기화 (32bppPBGRA 필수)
    hr = m_UIRenderStruct->m_wicImageFactory.Get()->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return 0;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return 0;

    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    // 기존 텍스처 초기화 후 새 비트맵 생성
    m_texture.Reset();
    hr = m_UIRenderStruct->m_d2DdevCon->CreateBitmapFromWicBitmap(converter.Get(), &bmpProps, &m_texture);

    if (SUCCEEDED(hr)) {
        m_nowIndex = 1; // 로드 성공 시 1번 인덱스로 간주
    }

    return m_nowIndex;
}


void UI_ImageComponent::Initalize(UIRenderStruct& UIRenderStruct) {
    m_UIRenderStruct = &UIRenderStruct;
}

void UI_ImageComponent::Update() {
    auto tmpTransform = Owner->GetTransform();
    m_size = tmpTransform.m_size;
    m_pivot = tmpTransform.m_pivot;
    CalRect();
    Owner->m_rect = m_rect; // 오너에 갱신된 m_rect 값 추출
}

void UI_ImageComponent::CalRect()
{
    // 피벗을 기준으로 로컬 좌표계 상의 사각형 영역 계산
    float px = m_size.x * m_pivot.x;
    float py = m_size.y * m_pivot.y;
    m_rect = D2D1::RectF(-px, -py, m_size.x - px, m_size.y - py);
}