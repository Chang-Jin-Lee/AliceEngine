#pragma once

#include <string>
#include <vector>
#include <functional>
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

		// 이벤트 델리게이트 (스크립트에서 등록)
		using ButtonDelegate = std::function<void()>;
		std::vector<ButtonDelegate> onPressed;
		std::vector<ButtonDelegate> onReleased;
		std::vector<ButtonDelegate> onHovered;

		void AddOnPressed(ButtonDelegate fn) { if (fn) onPressed.push_back(std::move(fn)); }
		void AddOnReleased(ButtonDelegate fn) { if (fn) onReleased.push_back(std::move(fn)); }
		void AddOnHovered(ButtonDelegate fn) { if (fn) onHovered.push_back(std::move(fn)); }
		void ClearDelegates()
		{
			onPressed.clear();
			onReleased.clear();
			onHovered.clear();
		}

		bool ConsumeClick()
		{
			const bool wasClicked = clicked;
			clicked = false;
			return wasClicked;
		}
	};
}
