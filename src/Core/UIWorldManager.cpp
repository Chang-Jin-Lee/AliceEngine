#include "UIWorldManager.h"
#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#pragma comment(lib, "d2d1.lib")

#include <dxgi1_6.h> // IDXGIFactory7
#pragma comment(lib, "dxgi.lib")



#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <stdexcept>




#define IMGUI_IMPL_API 
#include "imgui.h"


#include "Core/Helper.h"

// TODO: UIRenderStruct와 UISceneManager 헤더 파일이 생성되면 아래 주석을 해제하고 전방 선언을 제거하세요
// #include "UIRenderStruct.h"
// #include "UISceneManager.h"



//UIWorldManager::~UIWorldManager()
//{
//    m_nowManager = nullptr;
//
//    sceneStorages.clear();
//
//    if (m_d2DdevCon)
//    {
//        //드로잉 종료 보장
//        m_d2DdevCon->SetTarget(nullptr);
//        m_d2DdevCon->Flush();
//    }
//
//    //D2D 리소스 (자식 → 부모 순)
//    m_d2dTargetBitmap.Reset();
//    m_brush.Reset();
//
//    //DXGI 리소스
//    m_dxgiSurface.Reset();
//
//    //D2D Core
//    m_d2DdevCon.Reset();
//    m_d2DDevice.Reset();
//    m_d2DFactory.Reset();
//
//    //리소스
//    m_shaderRV.Reset();
//    m_RenderTV.Reset();
//    m_tex2D.Reset();
//
//    //WIC
//    m_wicFactory.Reset();
//    
//
//    
//}

// inputSystem은 추후에 싱글톤인 경우 SceneManager에서 변경하기
void UIWorldManager::Initalize(ID3D11Device* pDev, ID3D11DeviceContext* pDevCon, UINT w, UINT h, Alice::InputSystem& tmpInput)
{
    m_d3dDev = pDev;
    m_devCon = pDevCon;


    //3D에 합성할 2D Tex 생성
    Create2DTex(w, h);

    // D2D Factory
    D2D1_FACTORY_OPTIONS options = {};
    HR_T(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory8),
        &options,
        reinterpret_cast<void**>(m_d2DFactory.GetAddressOf())
    ));

    // DXGI device
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HR_T(m_d3dDev->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())));;


    // D2D 디바이스
    m_d2DFactory->CreateDevice((dxgiDevice.Get()), m_d2DDevice.GetAddressOf());
    m_d2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_d2DdevCon.GetAddressOf());


    // DWrite
    HR_T(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_D3DWFactory.GetAddressOf())));

    // brush 생성
    HR_T(m_d2DdevCon->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DeepSkyBlue, 0.5f), &m_brush));

    // Tex -> DXGI Surface
    HR_T(m_tex2D.As(&m_dxgiSurface));

    // 이미지 -> bitmap
     HR_T(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)
    ));

    // D2D에 target bitmap 생성
    D2D1_BITMAP_PROPERTIES1 bmpProps = {};
    bmpProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bmpProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bmpProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    bmpProps.dpiX = 96.0f;   //인치당 픽셸수
    bmpProps.dpiY = 96.0f;
    m_d2DdevCon->CreateBitmapFromDxgiSurface(m_dxgiSurface.Get(), &bmpProps, m_d2dTargetBitmap.GetAddressOf());
    m_d2DdevCon->SetTarget(m_d2dTargetBitmap.Get());

    //일단 임시로 로우 포인터로 받음
    m_inputSystem = &tmpInput;

 
    // 하위 Manager나 Object들에게 변수를 넘겨주기 위해 struct 구조로 넘겨줄 예정
  /* m_RenderStruct.m_d2DFactory = m_d2DFactory;
    m_RenderStruct.m_d2DDevice = m_d2DDevice;
    m_RenderStruct.m_d2DdevCon = m_d2DdevCon;
    m_RenderStruct.m_D3DWFactory = m_D3DWFactory;
    m_RenderStruct.m_brush = m_brush;
    m_RenderStruct.m_wicImageFactory = m_wicFactory;
    m_RenderStruct.m_d2dTargetBitmap = m_d2dTargetBitmap;
    m_RenderStruct.m_width = w;
    m_RenderStruct.m_height = h;*/
}


void UIWorldManager::Update(UINT w, UINT h)
{
    m_curWidth = w;
    m_curHeight = h;

    // 현재 매니저 포인터 저장
    //if (sceneStorages.size() == 0) { return; }
    //m_nowManager = sceneStorages[m_nowSceneID].get();
    //m_nowManager->Update();
}


void UIWorldManager::Render()
{
    // if (sceneStorages.size() == 0) { return; }
    // D2D 렌더링 시작 + 2D 텍스처에 렌더링 + 바인딩
    //    m_nowManager->Render();

    // RTV 해제 
    ID3D11RenderTargetView* nullRTV[1] = { nullptr };
    m_devCon->OMSetRenderTargets(1, nullRTV, nullptr);


    ID3D11ShaderResourceView* srvs[] = { m_shaderRV.Get() };
    m_devCon->PSSetShaderResources(m_bindSlot, 1, srvs);
}



 //3D에 합성할 2D Tex 생성
void UIWorldManager::Create2DTex(UINT w, UINT h)
{
    m_curWidth = w; m_curHeight = h;

	// 2D 텍스처 구조체
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HR_T(m_d3dDev->CreateTexture2D(&desc, nullptr, m_tex2D.GetAddressOf()));
    HR_T(m_d3dDev->CreateShaderResourceView(m_tex2D.Get(), nullptr, &m_shaderRV));
    HR_T(m_d3dDev->CreateRenderTargetView(m_tex2D.Get(), nullptr, &m_RenderTV));

}


void UIWorldManager::ChangeScene(UINT nowSceneID) {
    // 추후에 SceneManager 추가시 주셕 변경ㄴ
    //if (m_nowSceneID < nowSceneID && sceneStorages.size() == 0)
    //{
    //    auto CreateUI = [&](UINT ID) -> UISceneManager* {
    //        auto pObj = std::make_unique<UISceneManager>();

    //        //없는경우 생성하면서 해당 매니저 initalize()
    //        UISceneManager* ptr = pObj.get();
    //        ptr->initalize(m_d3dDev, m_devCon, &m_RenderStruct, m_inputSystem);
    //        m_nowManager = ptr;
    //        sceneStorages.emplace(ID, std::move(pObj));
    //        m_SceneID++;
    //        return ptr;
    //        };
    //    CreateUI(nowSceneID);
    //}
    //else
    //{
    //    m_nowManager = sceneStorages[nowSceneID].get();
    //}

    m_nowSceneID = nowSceneID;
}
