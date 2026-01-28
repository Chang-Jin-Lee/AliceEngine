#pragma once

#include <string>
#include <DirectXMath.h>
#include "AliceUI/UICommon.h"

namespace Alice
{
	struct UIButtonComponent
	{
		bool enabled{ true };
		AliceUI::UIButtonState state{ AliceUI::UIButtonState::Normal };

		DirectX::XMFLOAT4 normalTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 hoveredTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 pressedTint{ 0.85f, 0.85f, 0.85f, 1.0f };
		DirectX::XMFLOAT4 disabledTint{ 0.4f, 0.4f, 0.4f, 1.0f };

		std::string normalTexture;
		std::string hoveredTexture;
		std::string pressedTexture;
		std::string disabledTexture;

		// 클릭 이벤트
		bool clicked{ false };
		bool wasPressed{ false };

		bool ConsumeClick()
		{
			const bool wasClicked = clicked;
			clicked = false;
			return wasClicked;
		}
	};
}
