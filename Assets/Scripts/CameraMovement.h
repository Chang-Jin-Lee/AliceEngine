#pragma once

#include "Core/Script.h"

namespace Alice
{
	// 방향키로 카메라를 이동시키는 스크립트
	class CameraMovement : public IScript
	{
	public:
		// 엔진에서 식별할 스크립트 이름
		const char* GetName() const override { return "CameraMovement"; }

		// 초기화 및 매 프레임 업데이트
		void Start() override;
		void Update(float deltaTime) override;

	public:
		// 리플렉션(속성창) 연동을 위한 Getter/Setter
		// (엔진 내부 매크로가 Get_변수명 / Set_변수명 패턴을 사용할 경우를 대비함)
		float Get_m_moveSpeed() const { return m_moveSpeed; }
		void Set_m_moveSpeed(float val) { m_moveSpeed = val; }

	private:
		// 이동 속도 (기본값 설정)
		float m_moveSpeed = 10.0f;
	};
}