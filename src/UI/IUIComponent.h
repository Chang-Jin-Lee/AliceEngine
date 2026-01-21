#pragma once
#include <cstdint>

class UIBase; // forward declaration

struct IUIComponent
{
    virtual ~IUIComponent() = default;

    // ������
    UIBase* Owner = nullptr;
    unsigned long OwnerID = 0;

    // ���� �ʱ�ȭ ��(����): ������Ʈ�� owner �ʿ� �� �������̵�
    virtual void OnAdded() {}

    // ���� ��� (���� ȣȯ�� ����)
    long unsigned id{ 0 };
    long unsigned owner{ 0 };
};