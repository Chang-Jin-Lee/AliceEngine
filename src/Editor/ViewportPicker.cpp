#include "ViewportPicker.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace Alice
{
    namespace
    {
        struct Ray
        {
            XMFLOAT3 origin;
            XMFLOAT3 direction; // 정규화된 방향 벡터
        };

        bool IntersectRaySphere(const Ray& ray,
                                const XMFLOAT3& center,
                                float radius,
                                float& outT)
        {
            XMVECTOR O = XMLoadFloat3(&ray.origin);
            XMVECTOR D = XMLoadFloat3(&ray.direction);
            XMVECTOR C = XMLoadFloat3(&center);

            XMVECTOR m = XMVectorSubtract(O, C);

            float b = XMVectorGetX(XMVector3Dot(m, D));
            float c = XMVectorGetX(XMVector3Dot(m, m)) - radius * radius;

            if (c > 0.0f && b > 0.0f)
                return false;

            float disc = b * b - c;
            if (disc < 0.0f)
                return false;

            float t = -b - std::sqrt(disc);
            if (t < 0.0f)
                t = 0.0f;

            outT = t;
            return true;
        }
    }

    EntityId ViewportPicker::Pick(const World& world,
                                  const Camera& camera,
                                  float u,
                                  float v) const
    {
        // 1) NDC 좌표 (-1~1) 변환
        const float ndcX = 2.0f * u - 1.0f;
        const float ndcY = 1.0f - 2.0f * v;

        XMMATRIX view       = camera.GetViewMatrix();
        XMMATRIX projection = camera.GetProjectionMatrix();
        XMMATRIX viewProj   = XMMatrixMultiply(view, projection);
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        // 2) 클립 공간 → 월드 공간
        XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

        nearPoint = XMVector3TransformCoord(nearPoint, invViewProj);
        farPoint  = XMVector3TransformCoord(farPoint,  invViewProj);

        XMVECTOR dir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));

        Ray ray {};
        XMStoreFloat3(&ray.origin,    nearPoint);
        XMStoreFloat3(&ray.direction, dir);

        const auto& transforms = world.GetTransforms();
        if (transforms.empty())
            return InvalidEntityId;

        float   nearestT   = FLT_MAX;
        EntityId hitEntity = InvalidEntityId;

        for (const auto& [entityId, transform] : transforms)
        {
            // 큐브의 바운딩 스피어 ([-1,1]^3 → 반지름 sqrt(3))
            float maxScale = (std::max)(transform.scale.x,
                                        (std::max)(transform.scale.y, transform.scale.z));
            const float baseRadius = std::sqrt(3.0f);
            const float radius     = baseRadius * maxScale;

            float tHit = 0.0f;
            if (IntersectRaySphere(ray, transform.position, radius, tHit))
            {
                if (tHit < nearestT)
                {
                    nearestT  = tHit;
                    hitEntity = entityId;
                }
            }
        }

        return hitEntity;
    }
}