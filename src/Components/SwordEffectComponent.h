#pragma once

#include <DirectXMath.h>
#include <vector>

namespace Alice
{
	/// 검기 이펙트 컴포넌트 (스플라인 라인 이펙트)
	struct SwordEffectComponent
	{
		DirectX::XMFLOAT3 color{ 0.8f, 0.2f, 0.9f };  // 이펙트 색상 (기본값: 보라색)
		float alpha{ 1.0f };                          // 알파 값 (투명도)
		bool enabled{ true };                         // 이펙트 활성화 여부
		
		// 스플라인 제어점들
		DirectX::XMFLOAT3 startPoint{ -2.0f, 1.5f, 0.0f };
		DirectX::XMFLOAT3 endPoint{ 2.0f, 1.5f, 0.0f };
		DirectX::XMFLOAT3 controlPoint1{ -1.0f, 1.5f, -0.5f };
		DirectX::XMFLOAT3 controlPoint2{ 1.0f, 1.5f, 0.5f };
		
		// 스플라인 설정
		int segmentCount{ 64 };                       // 스플라인 세그먼트 수
		
		// 스플라인 점들 (매 프레임 계산됨)
		std::vector<DirectX::XMFLOAT3> splinePoints;
	};
}
