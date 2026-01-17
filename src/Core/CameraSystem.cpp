#include "Core/CameraSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

#include <DirectXMath.h>

#include "Core/World.h"
#include "Core/InputSystem.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "PhysX/IPhysicsWorld.h"

namespace Alice
{
    namespace
    {
        static float DegToRad(float deg) { return deg * (DirectX::XM_PI / 180.0f); }
        static float RadToDeg(float rad) { return rad * (180.0f / DirectX::XM_PI); }

        static float ExpSmooth(float damping, float dt)
        {
            if (damping <= 0.0f) return 1.0f;
            return 1.0f - std::exp(-damping * dt);
        }

        static DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        static DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir)
        {
            const float yaw = std::atan2(dir.x, dir.z);
            const float distXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            const float pitch = -std::atan2(dir.y, distXZ);
            return DirectX::XMFLOAT3(pitch, yaw, 0.0f);
        }

        static DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
        {
            const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
            DirectX::XMFLOAT4 out{};
            DirectX::XMStoreFloat4(&out, q);
            return out;
        }

        static DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat)
        {
            // DirectXMath 컨벤션에 맞게 수정: (pitch=X, yaw=Y, roll=Z)
            using namespace DirectX;

            const XMVECTOR q = XMLoadFloat4(&quat);

            // DirectX 기본: +Z forward, +Y up
            const XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
            const XMVECTOR u = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);

            XMFLOAT3 f3{};
            XMStoreFloat3(&f3, f);

            const float yaw   = std::atan2(f3.x, f3.z);
            const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));

            // roll: (yaw,pitch)만으로 만든 기준 up(u0)과 실제 up(u)의 차이를 forward축 기준으로 측정
            XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
            XMVECTOR r0 = XMVector3Cross(worldUp, f);
            if (XMVectorGetX(XMVector3LengthSq(r0)) < 1e-6f)
                r0 = XMVector3Cross(XMVectorSet(1, 0, 0, 0), f);

            r0 = XMVector3Normalize(r0);
            const XMVECTOR u0 = XMVector3Cross(f, r0);

            const float roll = std::atan2(
                XMVectorGetX(XMVector3Dot(r0, u)),
                XMVectorGetX(XMVector3Dot(u0, u))
            );

            return { pitch, yaw, roll };
        }

        static DirectX::XMFLOAT3 GetForward(float yawRad, float pitchRad)
        {
            // DirectionToEuler()와 부호 규칙을 맞춤:
            // - pitch가 +면 아래를 보는 상태(카메라는 위), pitch가 -면 위를 보는 상태(카메라는 아래)
            const float cosPitch = std::cos(pitchRad);
            return {
                std::sin(yawRad) * cosPitch,
                -std::sin(pitchRad),  // DirectionToEuler()와 부호 일치
                std::cos(yawRad) * cosPitch
            };
        }

        static std::string Trim(const std::string& s)
        {
            std::size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
            std::size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return s.substr(start, end - start);
        }

        static std::vector<EntityId> ResolveCameraList(World& world, const std::string& csv)
        {
            std::vector<EntityId> result;
            std::stringstream ss(csv);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                const std::string name = Trim(item);
                if (name.empty())
                    continue;
                auto go = world.FindGameObject(name);
                if (go.IsValid())
                    result.push_back(go.id());
            }
            return result;
        }

        static EntityId FindPrimaryCamera(World& world)
        {
            EntityId camId = InvalidEntityId;
            for (const auto& [id, cam] : world.GetComponents<CameraComponent>())
            {
                if (cam.primary) { camId = id; break; }
                if (camId == InvalidEntityId) camId = id;
            }
            return camId;
        }

        static bool GetCameraSnapshot(World& world, EntityId id,
                                      DirectX::XMFLOAT3& outPos,
                                      DirectX::XMFLOAT3& outRot,
                                      float& outFovY,
                                      float& outNear,
                                      float& outFar)
        {
            if (id == InvalidEntityId) return false;
            const auto* tr = world.GetComponent<TransformComponent>(id);
            const auto* cam = world.GetComponent<CameraComponent>(id);
            if (!tr || !cam) return false;
            outPos = tr->position;
            outRot = tr->rotation;
            outFovY = cam->fovYRad;
            outNear = cam->nearPlane;
            outFar = cam->farPlane;
            return true;
        }

        static void ApplyCameraSnapshot(World& world, EntityId id,
                                        const DirectX::XMFLOAT3& pos,
                                        const DirectX::XMFLOAT3& rot,
                                        float fovY,
                                        float nearPlane,
                                        float farPlane)
        {
            if (id == InvalidEntityId) return;
            auto* tr = world.GetComponent<TransformComponent>(id);
            auto* cam = world.GetComponent<CameraComponent>(id);
            if (!tr || !cam) return;
            tr->position = pos;
            tr->rotation = rot;
            cam->fovYRad = fovY;
            cam->nearPlane = nearPlane;
            cam->farPlane = farPlane;
        }
    }

    void CameraSystem::Update(World& world, InputSystem& input, float deltaTime)
    {
        const EntityId outputId = FindPrimaryCamera(world);
        if (outputId == InvalidEntityId)
            return;

        auto* outputTr = world.GetComponent<TransformComponent>(outputId);
        auto* outputCam = world.GetComponent<CameraComponent>(outputId);
        if (!outputTr || !outputCam)
            return;

        auto* inputComp = world.GetComponent<CameraInputComponent>(outputId);
        auto* blendComp = world.GetComponent<CameraBlendComponent>(outputId);
        auto* shakeComp = world.GetComponent<CameraShakeComponent>(outputId);
        auto* followComp = world.GetComponent<CameraFollowComponent>(outputId);
        auto* springComp = world.GetComponent<CameraSpringArmComponent>(outputId);
        auto* lookAtComp = world.GetComponent<CameraLookAtComponent>(outputId);

        // === 입력 처리 ===
        if (inputComp && inputComp->enabled)
        {
            const auto cameraList = ResolveCameraList(world, inputComp->cameraListCsv);

            auto TriggerCut = [&](std::size_t idx)
            {
                if (idx >= cameraList.size()) return;
                DirectX::XMFLOAT3 pos{}, rot{};
                float fovY{}, nearP{}, farP{};
                if (!GetCameraSnapshot(world, cameraList[idx], pos, rot, fovY, nearP, farP))
                    return;
                ApplyCameraSnapshot(world, outputId, pos, rot, fovY, nearP, farP);
                if (blendComp) blendComp->active = false;
            };

            auto EnsureBlendComponent = [&]()
            {
                if (!blendComp)
                    blendComp = &world.AddComponent<CameraBlendComponent>(outputId);
                return blendComp != nullptr;
            };

            auto TriggerBlend = [&](std::size_t idx, float duration)
            {
                if (idx >= cameraList.size()) return;
                if (!EnsureBlendComponent()) return;
                DirectX::XMFLOAT3 pos{}, rot{};
                float fovY{}, nearP{}, farP{};
                if (!GetCameraSnapshot(world, outputId, pos, rot, fovY, nearP, farP))
                    return;
                blendComp->sourcePosition = pos;
                blendComp->sourceRotation = rot;
                blendComp->sourceFovY = fovY;
                blendComp->sourceNear = nearP;
                blendComp->sourceFar = farP;
                blendComp->targetId = cameraList[idx];
                blendComp->targetName.clear();
                blendComp->duration = duration;
                blendComp->elapsed = 0.0f;
                blendComp->slowTriggered = false;
                blendComp->slowElapsed = 0.0f;
                blendComp->active = true;
            };

            if (input.IsKeyPressed(DirectX::Keyboard::D1)) TriggerCut(0);
            if (input.IsKeyPressed(DirectX::Keyboard::D2)) TriggerCut(1);
            if (input.IsKeyPressed(DirectX::Keyboard::D3)) TriggerBlend(2, inputComp->blendTimeKey3);
            if (input.IsKeyPressed(DirectX::Keyboard::D4))
            {
                TriggerBlend(3, inputComp->blendTimeKey4);
                if (!shakeComp)
                    shakeComp = &world.AddComponent<CameraShakeComponent>(outputId);
                if (shakeComp)
                {
                    shakeComp->amplitude = inputComp->shakeAmplitudeKey4;
                    shakeComp->frequency = inputComp->shakeFrequencyKey4;
                    shakeComp->duration = inputComp->shakeDurationKey4;
                    shakeComp->decay = inputComp->shakeDecayKey4;
                    shakeComp->elapsed = 0.0f;
                }
            }
            if (input.IsKeyPressed(DirectX::Keyboard::D5))
            {
                TriggerBlend(4, inputComp->blendTimeKey5);
                if (blendComp)
                {
                    blendComp->slowTriggerT = inputComp->slowTriggerTKey5;
                    blendComp->slowDuration = inputComp->slowDurationKey5;
                    blendComp->slowTimeScale = inputComp->slowTimeScaleKey5;
                }
            }

            if (input.IsKeyPressed(DirectX::Keyboard::L))
            {
                if (!lookAtComp)
                    lookAtComp = &world.AddComponent<CameraLookAtComponent>(outputId);
                if (lookAtComp)
                {
                    lookAtComp->enabled = !lookAtComp->enabled;
                    lookAtComp->targetName = inputComp->lookAtTargetName;
                }
            }

            if (!springComp)
                springComp = &world.AddComponent<CameraSpringArmComponent>(outputId);

            if (springComp && springComp->enabled && springComp->enableZoom)
            {
                const float wheel = input.GetMouseScrollDelta();
                if (wheel != 0.0f)
                {
                    springComp->desiredDistance = std::clamp(
                        springComp->desiredDistance - wheel * springComp->zoomSpeed,
                        springComp->minDistance,
                        springComp->maxDistance);
                }
            }
        }

        // === 블렌드 처리 ===
        if (blendComp && blendComp->active)
        {
            float dt = deltaTime;
            if (blendComp->slowDuration > 0.0f && !blendComp->slowTriggered)
            {
                const float t0 = (blendComp->duration > 0.0f)
                    ? (blendComp->elapsed / blendComp->duration)
                    : 1.0f;
                if (t0 >= blendComp->slowTriggerT)
                {
                    blendComp->slowTriggered = true;
                    blendComp->slowElapsed = 0.0f;
                }
            }
            if (blendComp->slowTriggered && blendComp->slowDuration > 0.0f)
            {
                blendComp->slowElapsed += dt;
                if (blendComp->slowElapsed < blendComp->slowDuration)
                    dt *= std::max(0.01f, blendComp->slowTimeScale);
            }

            blendComp->elapsed += dt;
            const float duration = (blendComp->duration > 0.0f) ? blendComp->duration : 0.0001f;
            float t01 = std::clamp(blendComp->elapsed / duration, 0.0f, 1.0f);
            if (blendComp->useSmoothStep)
                t01 = t01 * t01 * (3.0f - 2.0f * t01);

            DirectX::XMFLOAT3 targetPos{}, targetRot{};
            float targetFov{}, targetNear{}, targetFar{};
            EntityId targetId = blendComp->targetId;
            if (targetId == InvalidEntityId && !blendComp->targetName.empty())
            {
                auto go = world.FindGameObject(blendComp->targetName);
                if (go.IsValid()) targetId = go.id();
            }
            if (GetCameraSnapshot(world, targetId, targetPos, targetRot, targetFov, targetNear, targetFar))
            {
                const DirectX::XMFLOAT3 pos = LerpVec(blendComp->sourcePosition, targetPos, t01);
                const DirectX::XMFLOAT4 qA = EulerToQuaternion(blendComp->sourceRotation);
                const DirectX::XMFLOAT4 qB = EulerToQuaternion(targetRot);
                DirectX::XMFLOAT4 qOut{};
                DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(&qA),
                                                                         DirectX::XMLoadFloat4(&qB),
                                                                         t01));
                const DirectX::XMFLOAT3 rot = QuaternionToEuler(qOut);
                const float fovY = blendComp->sourceFovY + (targetFov - blendComp->sourceFovY) * t01;
                const float nearP = blendComp->sourceNear + (targetNear - blendComp->sourceNear) * t01;
                const float farP = blendComp->sourceFar + (targetFar - blendComp->sourceFar) * t01;
                ApplyCameraSnapshot(world, outputId, pos, rot, fovY, nearP, farP);
            }

            if (t01 >= 1.0f)
                blendComp->active = false;
        }

        const bool blending = (blendComp && blendComp->active);

        // === 팔로우 처리 (블렌드 중이 아닐 때) ===
        if (!blending && followComp && followComp->enabled)
        {
            float dt = deltaTime * std::max(0.0f, followComp->cameraTimeScale);

            if (followComp->enableInput)
            {
                if (!followComp->lockOnActive || followComp->allowManualOrbitInLockOn)
                {
                    if (input.IsLeftButtonDown())
                    {
                        followComp->yawDeg -= input.GetMouseDelta().x * followComp->sensitivity;
                        followComp->pitchDeg -= input.GetMouseDelta().y * followComp->sensitivity;
                        followComp->pitchDeg = std::clamp(followComp->pitchDeg,
                                                          followComp->pitchMinDeg,
                                                          followComp->pitchMaxDeg);
                    }
                }
            }

            // 타깃 찾기
            EntityId targetId = InvalidEntityId;
            if (!followComp->targetName.empty())
            {
                auto go = world.FindGameObject(followComp->targetName);
                if (go.IsValid()) targetId = go.id();
            }
            if (targetId == InvalidEntityId)
            {
                for (const auto& [id, skinned] : world.GetComponents<SkinnedMeshComponent>())
                {
                    if (!skinned.meshAssetPath.empty())
                    {
                        targetId = id;
                        break;
                    }
                }
            }
            if (targetId == InvalidEntityId)
                goto FollowDone;

            const auto* targetTr = world.GetComponent<TransformComponent>(targetId);
            if (!targetTr)
                goto FollowDone;

            // 모드 거리/시야각
            float modeDistance = followComp->baseDistance;
            float modeFovDeg = followComp->exploreFovDeg;
            switch (followComp->mode)
            {
            case 1: modeDistance = followComp->combatDistance; modeFovDeg = followComp->combatFovDeg; break;
            case 2: modeDistance = followComp->lockOnDistance; modeFovDeg = followComp->lockOnFovDeg; break;
            case 3: modeDistance = followComp->aimDistance; modeFovDeg = followComp->aimFovDeg; break;
            case 4: modeDistance = followComp->bossIntroDistance; modeFovDeg = followComp->bossIntroFovDeg; break;
            case 5: modeDistance = followComp->deathDistance; modeFovDeg = followComp->deathFovDeg; break;
            default: break;
            }

            if (springComp && springComp->enabled)
            {
                if (!springComp->initialized)
                {
                    springComp->distance = std::clamp(modeDistance, springComp->minDistance, springComp->maxDistance);
                    springComp->desiredDistance = springComp->distance;
                    springComp->initialized = true;
                }

                if (!springComp->enableZoom)
                {
                    springComp->desiredDistance = std::clamp(modeDistance, springComp->minDistance, springComp->maxDistance);
                }

                const float dAlpha = ExpSmooth(springComp->distanceDamping, dt);
                springComp->distance = springComp->distance + (springComp->desiredDistance - springComp->distance) * dAlpha;
            }

            if (!followComp->initialized)
            {
                followComp->smoothedPosition = outputTr->position;
                followComp->smoothedRotation = outputTr->rotation;
                followComp->initialized = true;
            }

            DirectX::XMFLOAT3 pivot = {
                targetTr->position.x,
                targetTr->position.y + followComp->heightOffset,
                targetTr->position.z
            };

            const float yawRad = DegToRad(followComp->yawDeg);
            const DirectX::XMFLOAT3 right = { std::cos(yawRad), 0.0f, -std::sin(yawRad) };
            pivot.x += right.x * followComp->shoulderOffset * followComp->shoulderSide;
            pivot.z += right.z * followComp->shoulderOffset * followComp->shoulderSide;

            DirectX::XMFLOAT3 desiredForward = GetForward(yawRad, DegToRad(followComp->pitchDeg));
            if (followComp->lockOnActive && followComp->lockOnTargetId != InvalidEntityId)
            {
                if (const auto* lockTr = world.GetComponent<TransformComponent>(followComp->lockOnTargetId))
                {
                    const DirectX::XMFLOAT3 to = {
                        lockTr->position.x - pivot.x,
                        lockTr->position.y - pivot.y,
                        lockTr->position.z - pivot.z
                    };
                    const float len = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                    if (len > 0.001f)
                        desiredForward = { to.x / len, to.y / len, to.z / len };
                }
            }

            const float dist = (springComp && springComp->enabled) ? springComp->distance : modeDistance;
            DirectX::XMFLOAT3 desiredPos = {
                pivot.x - desiredForward.x * dist,
                pivot.y - desiredForward.y * dist,
                pivot.z - desiredForward.z * dist
            };

            // 스프링 암 충돌
            if (springComp && springComp->enabled && springComp->enableCollision)
            {
                if (auto* physics = world.GetPhysicsWorld())
                {
                    const Vec3 origin(pivot.x, pivot.y, pivot.z);
                    const Vec3 dir(desiredPos.x - pivot.x, desiredPos.y - pivot.y, desiredPos.z - pivot.z);
                    const float maxDist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (maxDist > 0.001f)
                    {
                        const Vec3 dirN = dir / maxDist;

                        // TODO(PhysX): 카메라 전용 queryMask/layerMask를 추가해 사용하세요.
                        const uint32_t layerMask = 0xFFFFFFFFu;
                        const uint32_t queryMask = 0xFFFFFFFFu;

                        bool hitFound = false;
                        float hitDistance = maxDist;

                        if (springComp->probeRadius > 0.001f)
                        {
                            SweepHit hit{};
                            hitFound = physics->SweepSphere(origin, springComp->probeRadius, dirN, maxDist, hit, layerMask, queryMask, false);
                            if (hitFound) hitDistance = hit.distance;
                        }
                        else
                        {
                            RaycastHit hit{};
                            hitFound = physics->RaycastEx(origin, dirN, maxDist, hit, layerMask, queryMask, false);
                            if (hitFound) hitDistance = hit.distance;
                        }

                        if (hitFound)
                        {
                            const float minDist = springComp ? springComp->minDistance : followComp->minDistance;
                            const float adjusted = std::max(minDist, hitDistance - springComp->probePadding);
                            desiredPos.x = pivot.x + dirN.x * adjusted;
                            desiredPos.y = pivot.y + dirN.y * adjusted;
                            desiredPos.z = pivot.z + dirN.z * adjusted;
                        }
                    }
                }
            }

            if (springComp && desiredPos.y < springComp->minHeight)
                desiredPos.y = springComp->minHeight;

            const DirectX::XMFLOAT3 toTarget = {
                pivot.x - desiredPos.x,
                pivot.y - desiredPos.y,
                pivot.z - desiredPos.z
            };
            const DirectX::XMFLOAT3 desiredRot = DirectionToEuler(toTarget);

            const float posAlpha = ExpSmooth(followComp->positionDamping, dt);
            float rotDamping = followComp->rotationDamping;
            const float deltaYaw = std::abs(RadToDeg(desiredRot.y - followComp->smoothedRotation.y));
            if (deltaYaw > followComp->fastTurnYawThresholdDeg)
                rotDamping *= followComp->fastTurnMultiplier;
            const float rotAlpha = ExpSmooth(rotDamping, dt);

            followComp->smoothedPosition = LerpVec(followComp->smoothedPosition, desiredPos, posAlpha);

            const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(followComp->smoothedRotation);
            const DirectX::XMFLOAT4 qTo = EulerToQuaternion(desiredRot);
            DirectX::XMFLOAT4 qOut{};
            DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(&qFrom),
                                                                     DirectX::XMLoadFloat4(&qTo),
                                                                     rotAlpha));
            followComp->smoothedRotation = QuaternionToEuler(qOut);

            outputTr->position = followComp->smoothedPosition;
            outputTr->rotation = followComp->smoothedRotation;
            followComp->pitchDeg = RadToDeg(followComp->smoothedRotation.x);
            followComp->yawDeg = RadToDeg(followComp->smoothedRotation.y);

            const float fovAlpha = ExpSmooth(followComp->fovDamping, dt);
            const float targetFovRad = DegToRad(modeFovDeg);
            outputCam->fovYRad = outputCam->fovYRad + (targetFovRad - outputCam->fovYRad) * fovAlpha;
        }
    FollowDone:

        // === 룩앳 처리 ===
        if (!blending && lookAtComp && lookAtComp->enabled)
        {
            auto go = world.FindGameObject(lookAtComp->targetName);
            if (go.IsValid())
            {
                if (const auto* targetTr = world.GetComponent<TransformComponent>(go.id()))
                {
                    const DirectX::XMFLOAT3 to = {
                        targetTr->position.x - outputTr->position.x,
                        targetTr->position.y - outputTr->position.y,
                        targetTr->position.z - outputTr->position.z
                    };
                    const DirectX::XMFLOAT3 desiredRot = DirectionToEuler(to);
                    const float rotAlpha = ExpSmooth(lookAtComp->rotationDamping, deltaTime);

                    const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(outputTr->rotation);
                    const DirectX::XMFLOAT4 qTo = EulerToQuaternion(desiredRot);
                    DirectX::XMFLOAT4 qOut{};
                    DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(&qFrom),
                                                                             DirectX::XMLoadFloat4(&qTo),
                                                                             rotAlpha));
                    outputTr->rotation = QuaternionToEuler(qOut);
                }
            }
        }

        // === 쉐이크 처리 ===
        if (shakeComp && shakeComp->enabled && shakeComp->duration > 0.0f && shakeComp->amplitude > 0.0f)
        {
            shakeComp->elapsed += deltaTime;
            const float t01 = std::clamp(shakeComp->elapsed / shakeComp->duration, 0.0f, 1.0f);
            const float amp = shakeComp->amplitude * std::exp(-shakeComp->decay * t01);
            const float freq = shakeComp->frequency;

            DirectX::XMFLOAT3 offset{};
            offset.x = std::sin(shakeComp->elapsed * freq * 1.1f) * amp;
            offset.y = std::sin(shakeComp->elapsed * freq * 1.7f + 1.5f) * amp * 0.6f;
            offset.z = std::cos(shakeComp->elapsed * freq * 1.3f + 0.7f) * amp;

            outputTr->position.x += offset.x;
            outputTr->position.y += offset.y;
            outputTr->position.z += offset.z;

            if (t01 >= 1.0f)
            {
                shakeComp->duration = 0.0f;
                shakeComp->amplitude = 0.0f;
                shakeComp->elapsed = 0.0f;
            }
        }
    }
}
