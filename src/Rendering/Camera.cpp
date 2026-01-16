#include "Rendering/Camera.h"

using namespace DirectX;

namespace Alice
{
    void Camera::SetLookAt(const XMFLOAT3& position,
                           const XMFLOAT3& target,
                           const XMFLOAT3& up)
    {
        m_position = position;
        m_target   = target;
        m_up       = up;
    }

    void Camera::SetPerspective(float fovYRadians,
                                float aspectRatio,
                                float nearPlane,
                                float farPlane)
    {
        m_fovYRadians = fovYRadians;
        m_aspectRatio = aspectRatio;
        m_nearPlane   = nearPlane;
        m_farPlane    = farPlane;
    }

    XMMATRIX Camera::GetViewMatrix() const
    {
        XMVECTOR eye    = XMLoadFloat3(&m_position);
        XMVECTOR target = XMLoadFloat3(&m_target);
        XMVECTOR up     = XMLoadFloat3(&m_up);

        return XMMatrixLookAtLH(eye, target, up);
    }

    XMMATRIX Camera::GetProjectionMatrix() const
    {
        return XMMatrixPerspectiveFovLH(m_fovYRadians, m_aspectRatio, m_nearPlane, m_farPlane);
    }
}


