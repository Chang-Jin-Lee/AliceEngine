#include "Rendering/Camera.h"

#include <cmath>

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

    float Camera::GetFovXRadians() const
    {
        return 2.0f * std::atan(std::tan(m_fovYRadians * 0.5f) * m_aspectRatio);
    }

    XMMATRIX Camera::GetViewProjectionMatrix() const
    {
        return GetViewMatrix() * GetProjectionMatrix();
    }

    void Camera::GetFrustumPlanes(DirectX::XMFLOAT4 outPlanes[6]) const
    {
        if (!outPlanes) return;

        const XMMATRIX vp = GetViewProjectionMatrix();
        XMFLOAT4X4 m{};
        XMStoreFloat4x4(&m, vp);

        // 좌, 우, 하, 상, 근, 원 (LH 기준)
        XMVECTOR planes[6];
        planes[0] = XMPlaneNormalize(XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41)); // Left
        planes[1] = XMPlaneNormalize(XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41)); // Right
        planes[2] = XMPlaneNormalize(XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42)); // Bottom
        planes[3] = XMPlaneNormalize(XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42)); // Top
        planes[4] = XMPlaneNormalize(XMVectorSet(m._13,         m._23,         m._33,         m._43));          // Near
        planes[5] = XMPlaneNormalize(XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43)); // Far

        for (int i = 0; i < 6; ++i)
        {
            XMStoreFloat4(&outPlanes[i], planes[i]);
        }
    }

    DirectX::XMFLOAT2 Camera::WorldToScreen(const DirectX::XMFLOAT3& worldPos,
                                            float viewportWidth,
                                            float viewportHeight) const
    {
        const XMMATRIX vp = GetViewProjectionMatrix();
        const XMVECTOR p = XMLoadFloat3(&worldPos);
        XMVECTOR clip = XMVector3TransformCoord(p, vp);

        const float ndcX = XMVectorGetX(clip);
        const float ndcY = XMVectorGetY(clip);

        const float screenX = (ndcX * 0.5f + 0.5f) * viewportWidth;
        const float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportHeight;

        return DirectX::XMFLOAT2(screenX, screenY);
    }

    bool Camera::ScreenToWorldRay(float screenX,
                                  float screenY,
                                  float viewportWidth,
                                  float viewportHeight,
                                  DirectX::XMFLOAT3& outOrigin,
                                  DirectX::XMFLOAT3& outDir) const
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return false;

        const float ndcX = (screenX / viewportWidth) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screenY / viewportHeight) * 2.0f;

        const XMMATRIX invView = XMMatrixInverse(nullptr, GetViewMatrix());
        const XMMATRIX invProj = XMMatrixInverse(nullptr, GetProjectionMatrix());

        XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

        nearPoint = XMVector3TransformCoord(nearPoint, invProj);
        farPoint  = XMVector3TransformCoord(farPoint, invProj);

        nearPoint = XMVector3TransformCoord(nearPoint, invView);
        farPoint  = XMVector3TransformCoord(farPoint, invView);

        XMVECTOR dir = XMVector3Normalize(farPoint - nearPoint);
        XMStoreFloat3(&outOrigin, nearPoint);
        XMStoreFloat3(&outDir, dir);
        return true;
    }
}


