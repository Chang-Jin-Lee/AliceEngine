#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

#include <DirectXMath.h>

namespace Alice
{
    /// 간단한 3D 카메라 클래스입니다.
    /// - 위치, 타깃, 업 벡터를 기반으로 뷰 행렬을 계산합니다.
    /// - FOV, 종횡비, near/far 를 기반으로 투영 행렬을 계산합니다.
    class Camera
    {
    public:
        Camera() = default;

        /// 카메라의 위치와 바라보는 타깃, 업 벡터를 설정합니다.
        void SetLookAt(const DirectX::XMFLOAT3& position,
                       const DirectX::XMFLOAT3& target,
                       const DirectX::XMFLOAT3& up);

        /// 원근 투영 설정을 합니다.
        void SetPerspective(float fovYRadians,
                            float aspectRatio,
                            float nearPlane,
                            float farPlane);

        /// 현재 카메라 위치를 반환합니다.
        const DirectX::XMFLOAT3& GetPosition() const { return m_position; }

        /// 현재 FOV (라디안)를 반환합니다.
        float GetFovYRadians() const { return m_fovYRadians; }
        /// 현재 종횡비를 반환합니다.
        float GetAspectRatio() const { return m_aspectRatio; }
        /// 현재 near/far 평면을 반환합니다.
        float GetNearPlane() const { return m_nearPlane; }
        float GetFarPlane()  const { return m_farPlane; }

        /// 뷰 행렬을 반환합니다.
        DirectX::XMMATRIX GetViewMatrix() const;

        /// 투영 행렬을 반환합니다.
        DirectX::XMMATRIX GetProjectionMatrix() const;

    private:
        DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, -5.0f };
        DirectX::XMFLOAT3 m_target   { 0.0f, 0.0f,  0.0f };
        DirectX::XMFLOAT3 m_up       { 0.0f, 1.0f,  0.0f };

        float m_fovYRadians { DirectX::XM_PIDIV4 }; // 45도
        float m_aspectRatio { 16.0f / 9.0f };
        float m_nearPlane   { 0.1f };
        float m_farPlane    { 1000.0f };
    };
}


