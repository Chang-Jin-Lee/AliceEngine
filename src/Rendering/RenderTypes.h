#pragma once

#include <cstdint>
#include <string>
#include <d3d11.h>
#include <DirectXMath.h>

namespace Alice
{
    /// 포스트 프로세스 파라미터 구조체
    /// Forward/Deferred 렌더링 시스템에서 공통으로 사용됩니다.
    struct PostProcessParams
    {
        float exposure = 0.0f;        // Exposure 값 (기본값: 0 = 1.0배)
        float maxHDRNits = 1000.0f;   // HDR 모니터 최대 밝기 (nits)
    };

    /// 조명/재질 파라미터 구조체
    /// Forward/Deferred 렌더링 시스템에서 공통으로 사용됩니다.
    struct LightingParameters
    {
        // 재질 색상/하이라이트 (레거시 쉐이더용)
        DirectX::XMFLOAT3 diffuseColor  { 0.7f, 0.7f, 0.9f };
        DirectX::XMFLOAT3 specularColor { 1.0f, 1.0f, 1.0f };
        float             shininess     { 32.0f };

        // PBR 재질 파라미터
        DirectX::XMFLOAT3 baseColor     { 0.3f, 0.3f, 0.3f };  // PBR Base Color (Albedo)
        float             metalness    { 0.0f };                // 0.0 = 비금속, 1.0 = 금속
        float             roughness    { 0.5f };                // 0.0 = 거울, 1.0 = 거친 표면
        float             ambientOcclusion { 1.0f };            // AO (0.0 ~ 1.0)

        // 광원 세기
        float             keyIntensity  { 1.0f };
        float             fillIntensity { 0.0f };

        // 광원 방향 (월드 기준)
        DirectX::XMFLOAT3 keyDirection  {  0.5f, -1.0f,  0.5f };
        DirectX::XMFLOAT3 fillDirection { -0.5f, -0.5f, -0.2f };
    };

     /// 스키닝 메시를 그리기 위한 드로우 커맨드입니다.
    struct SkinnedDrawCommand
    {
        ID3D11Buffer*     vertexBuffer { nullptr };
        ID3D11Buffer*     indexBuffer  { nullptr };
        UINT              stride       { 0 };
        UINT              indexCount   { 0 };
        UINT              startIndex   { 0 };
        INT               baseVertex   { 0 };

        DirectX::XMMATRIX world       { DirectX::XMMatrixIdentity() };
        const DirectX::XMFLOAT4X4* bones { nullptr };
        std::uint32_t     boneCount   { 0 };

        DirectX::XMFLOAT3 color       { 0.7f, 0.7f, 0.7f };
        float             roughness   { 0.5f };
        float             metalness   { 0.0f };

        // 선택적인 알베도 텍스처 경로 (.alice 단일 포맷 또는 원본 이미지 경로)
        std::string       albedoTexturePath;
        // 어떤 스키닝 메시(레지스트리 키)를 사용할지 나타내는 논리 키
        std::string       meshKey;
    };


    struct ShadowSettings
	{
		// 튜토리얼(34_ToneMapping)과 동일한 기본값 스케일
		std::uint32_t mapSizePx = 2048;   // 섀도우맵 해상도(한 변)
		float         bias = 0.0015f;
		float         pcfRadius = 1.0f;   // texel 단위(0~3 권장)
		float         orthoRadius = 20.0f; // 월드 단위(씬 크기에 맞게 조절)
		bool          enabled = true;
	};

	struct PostProcessCB
	{
		float exposure;
		float maxHDRNits;
		float padding[2];
	};

	// 디퍼드에서 쓰이는 중
	// ConstantBuffer (register b0) 업데이트
	// HLSL의 ConstantBuffer 구조체와 일치해야 함
	struct ConstantBufferData
	{
		DirectX::XMMATRIX g_World;
		DirectX::XMMATRIX g_View;
		DirectX::XMMATRIX g_Proj;
		DirectX::XMMATRIX g_WorldInvTranspose;
		DirectX::XMFLOAT4 g_Material_ambient;
		DirectX::XMFLOAT4 g_Material_diffuse;
		DirectX::XMFLOAT4 g_Material_specular;
		DirectX::XMFLOAT4 g_Material_reflect;
		DirectX::XMFLOAT4 g_DirLight_ambient;
		DirectX::XMFLOAT4 g_DirLight_diffuse;
		DirectX::XMFLOAT4 g_DirLight_specular;
		DirectX::XMFLOAT3 g_DirLight_direction;
		float    g_DirLight_intensity;
		DirectX::XMFLOAT3 g_EyePosW;
		int      g_ShadingMode;
		int      g_EnableNormalMap;
		int      g_UseSpecularMap;
		int      g_UseDiffuseMap;
		float    g_Pad;
		int      g_UseTextureColor;
		DirectX::XMFLOAT3 g_PBRPad;
		DirectX::XMFLOAT4 g_PBRBaseColor;
		float    g_PBRMetalness;
		float    g_PBRRoughness;
		float    g_PBRAmbientOcclusion;
		float    g_PBRPad2;
		float    g_OutlineWidth;
		float    g_OutlinePow;
		float    g_OutlineThickness;
		float    g_OutlineStrength;
		DirectX::XMFLOAT4 g_OutlineColor;
		DirectX::XMMATRIX g_LightViewProj;
		float    g_ShadowBias;
		float    g_ShadowMapSize;
		float    g_ShadowPCFRadius;
		int      g_ShadowEnabled;
		int      g_BoundsBoneIndex;
		DirectX::XMFLOAT3 g_BoundsPad;

		// 생성자: 기본값 초기화
		ConstantBufferData()
		{
			// 1. 행렬 초기화 (Identity)
			g_World = DirectX::XMMatrixIdentity();
			g_View = DirectX::XMMatrixIdentity();
			g_Proj = DirectX::XMMatrixIdentity();
			g_WorldInvTranspose = DirectX::XMMatrixIdentity();
			g_LightViewProj = DirectX::XMMatrixIdentity(); // 외부 값(lightViewProj) 의존 -> Identity로 초기화

			// 2. 머티리얼 기본값
			g_Material_ambient = DirectX::XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
			g_Material_diffuse = DirectX::XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
			g_Material_specular = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			g_Material_reflect = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

			// 3. 라이트 기본값
			g_DirLight_ambient = DirectX::XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f);
			g_DirLight_diffuse = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			g_DirLight_specular = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
			g_DirLight_direction = DirectX::XMFLOAT3(0.5f, -1.0f, 0.5f);
			g_DirLight_intensity = 1.0f;

			// 4. 카메라 위치 (외부 값 의존 -> 0으로 초기화)
			g_EyePosW = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

			// 5. 셰이딩 및 텍스처 옵션
			g_ShadingMode = 0; // 혹은 기본 모드값
			g_EnableNormalMap = 0;
			g_UseSpecularMap = 0;
			g_UseDiffuseMap = 0;
			g_UseTextureColor = 1;
			g_Pad = 0.0f;

			// 6. PBR 파라미터
			g_PBRBaseColor = DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
			g_PBRMetalness = 0.0f;
			g_PBRRoughness = 0.5f;
			g_PBRAmbientOcclusion = 1.0f;
			g_PBRPad = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
			g_PBRPad2 = 0.0f;

			// 7. 아웃라인 (사용 안 함)
			g_OutlineWidth = 0.0f;
			g_OutlinePow = 0.0f;
			g_OutlineThickness = 0.0f;
			g_OutlineStrength = 0.0f;
			g_OutlineColor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

			// 8. 섀도우 (외부 설정 의존 -> 안전한 기본값)
			g_ShadowBias = 0.005f; // 일반적인 바이어스 값 예시
			g_ShadowMapSize = 2048.0f; // 일반적인 크기 예시
			g_ShadowPCFRadius = 1.0f;
			g_ShadowEnabled = 0; // 기본적으로 끔 (외부에서 켜야 함)
			g_BoundsBoneIndex = -1;
			g_BoundsPad = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		}
	};

	// Directional Light CB 업데이트 (b3)
	struct DirectionalLightData
	{
		DirectX::XMFLOAT4 direction;
		DirectX::XMFLOAT4 color;
		float intensity;
		float pad[3];
	};


	// 스키닝용 본 행렬 상수 버퍼
		// - D3D11 상수버퍼 최대 크기(64KB)에 맞춰 1023개(= 1023 * 64B = 65472B)를 사용합니다.
		// - D3D11-AliceTutorial/31_IBL 과 동일한 스케일.
	static constexpr std::uint32_t MaxBones = 1023;
	struct CBBones
	{
		DirectX::XMMATRIX bones[1023];
		std::uint32_t     boneCount{ 0 };
		std::uint32_t     pad[3]{ 0, 0, 0 }; // 16바이트 정렬
	};


	struct SimpleVertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT2 texcoord;
	};

	struct CBPerObject
	{
		DirectX::XMMATRIX world;
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX projection;
		DirectX::XMFLOAT4 materialColor; // per-object 베이스 컬러

		float             roughness;     // 0~1
		float             metalness;     // 0~1
		int               useTexture;   // 0: 색만, 1: 디퓨즈 텍스처 사용
		int               enableNormalMap; // 0/1: 노말맵 사용
	};

	/// 단순 Directional Light 2개와 재질 파라미터를 담는 구조체입니다.
	struct LightData
	{
		DirectX::XMFLOAT3 direction;
		float             pad0;

		DirectX::XMFLOAT3 color;
		float             intensity;
	};

	struct CBLighting
	{
		LightData         keyLight;
		LightData         fillLight;

		DirectX::XMFLOAT3 cameraPosition;
		float             pad1;

		DirectX::XMFLOAT4 materialDiffuse;   // rgb: 색상, a: 사용 안 함
		DirectX::XMFLOAT4 materialSpecular;  // rgb: 색상, a: shininess

		int               shadingMode;       // 0: Lambert, 1: Phong, 2: Blinn-Phong, 3: Toon
		int               pad2[3];           // 16바이트 정렬

		DirectX::XMMATRIX lightViewProj;     // 섀도우 맵 계산용 라이트 뷰-프로젝션

		// Shadow params (34_ToneMapping 방식)
		float             shadowBias;        // 깊이 바이어스(0~)
		float             shadowMapSize;     // 섀도우맵 한 변(px)
		float             shadowPcfRadius;   // PCF 반경(texel)
		int               shadowEnabled;     // 0/1
	};

}
