#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <rttr/type.h>
#include <rttr/variant.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <imgui.h>
#include <DirectXMath.h>
#include <string>
#include <functional>
#include <unordered_map>

namespace Alice
{
    /// @note RTTR 기반으로 렌더링하는 유틸리티 클래스
    namespace ReflectionUI
    {
        namespace Detail
        {
            /// @param obj 인스턴스
            /// @param label 렌더링할 라벨
            /// @return 변경 여부
            inline bool RenderProperty(const rttr::property& prop, rttr::instance& obj, 
                                      const std::string& label = "")
            {
                rttr::type propType = prop.get_type();
                std::string propName = prop.get_name().to_string();
                std::string displayName = label.empty() ? propName : label;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid())
                    return false;

                bool changed = false;

                if (propType == rttr::type::get<bool>())
                {
                    bool val = value.to_bool();
                    if (ImGui::Checkbox(displayName.c_str(), &val))
                    {
                        prop.set_value(obj, val);
                        changed = true;
                    }
                }
                else if (propType == rttr::type::get<int>())
                {
                    int val = value.to_int();
                    if (ImGui::DragInt(displayName.c_str(), &val))
                    {
                        prop.set_value(obj, val);
                        changed = true;
                    }
                }
                else if (propType == rttr::type::get<uint32_t>())
                {
                    // uint32_t는 비트마스크로 처리 가능하지만, 일단 일반 int로 표시
                    int val = static_cast<int>(value.to_uint32());
                    if (ImGui::DragInt(displayName.c_str(), &val, 1.0f, 0, INT_MAX))
                    {
                        prop.set_value(obj, static_cast<uint32_t>(val));
                        changed = true;
                    }
                }
                else if (propType == rttr::type::get<float>())
                {
                    float val = value.to_float();
                    if (ImGui::DragFloat(displayName.c_str(), &val, 0.01f))
                    {
                        prop.set_value(obj, val);
                        changed = true;
                    }
                }
                else if (propType == rttr::type::get<double>())
                {
                    float val = static_cast<float>(value.to_double());
                    if (ImGui::DragFloat(displayName.c_str(), &val, 0.01f))
                    {
                        prop.set_value(obj, static_cast<double>(val));
                        changed = true;
                    }
                }
                else if (propType == rttr::type::get<std::string>())
                {
                    std::string val = value.to_string();
                    char buffer[512] = {};
                    strncpy_s(buffer, val.c_str(), sizeof(buffer) - 1);
                    if (ImGui::InputText(displayName.c_str(), buffer, sizeof(buffer)))
                    {
                        prop.set_value(obj, std::string(buffer));
                        changed = true;
                    }
                }
                else if (propType.is_class())
                {
                    // 프로퍼티 타입이 클래스인 경우 렌더링
                    rttr::type classType = propType;
                    std::string className = classType.get_name().to_string();

                    // XMFLOAT3 타입 렌더링
                    if (className == "XMFLOAT3")
                    {
                        rttr::instance inst = value;
                        DirectX::XMFLOAT3* float3 = inst.try_convert<DirectX::XMFLOAT3>();
                        if (float3)
                        {
                            // "color" 또는 "Color"가 포함된 경우 색상 편집 컨트롤로 렌더링
                            bool isColor = propName.find("color") != std::string::npos || 
                                          propName.find("Color") != std::string::npos;
                            
                            if (isColor)
                            {
                                if (ImGui::ColorEdit3(displayName.c_str(), &float3->x))
                                {
                                    prop.set_value(obj, *float3);
                                    changed = true;
                                }
                            }
                            else
                            {
                                if (ImGui::DragFloat3(displayName.c_str(), &float3->x, 0.1f))
                                {
                                    prop.set_value(obj, *float3);
                                    changed = true;
                                }
                            }
                        }
                    }
                    // XMFLOAT4 타입 렌더링
                    else if (className == "XMFLOAT4")
                    {
                        rttr::instance inst = value;
                        DirectX::XMFLOAT4* float4 = inst.try_convert<DirectX::XMFLOAT4>();
                        if (float4)
                        {
                            bool isColor = propName.find("color") != std::string::npos || 
                                          propName.find("Color") != std::string::npos;
                            
                            if (isColor)
                            {
                                if (ImGui::ColorEdit4(displayName.c_str(), &float4->x))
                                {
                                    prop.set_value(obj, *float4);
                                    changed = true;
                                }
                            }
                            else
                            {
                                if (ImGui::DragFloat4(displayName.c_str(), &float4->x, 0.1f))
                                {
                                    prop.set_value(obj, *float4);
                                    changed = true;
                                }
                            }
                        }
                    }
                    else
                    {
                        // 트리 노드로 렌더링
                        if (ImGui::TreeNode(displayName.c_str()))
                        {
                            rttr::instance inst = value;
                            for (auto& subProp : classType.get_properties())
                            {
                                RenderProperty(subProp, inst);
                            }
                            ImGui::TreePop();
                        }
                    }
                }

                return changed;
            }

            /// @param prop 프로퍼티
            /// @param obj 인스턴스
            /// @param minVal 최소값
            /// @param maxVal 최대값
            /// @param label 렌더링할 라벨
            /// @return 변경 여부
            inline bool RenderPropertyWithRange(const rttr::property& prop, rttr::instance& obj,
                                               float minVal, float maxVal,
                                               const std::string& label = "")
            {
                rttr::type propType = prop.get_type();
                if (propType != rttr::type::get<float>() && propType != rttr::type::get<double>())
                {
                    return RenderProperty(prop, obj, label);
                }

                std::string propName = prop.get_name().to_string();
                std::string displayName = label.empty() ? propName : label;

                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid())
                    return false;

                float val = propType == rttr::type::get<float>() ? 
                           value.to_float() : static_cast<float>(value.to_double());
                
                if (ImGui::SliderFloat(displayName.c_str(), &val, minVal, maxVal))
                {
                    if (propType == rttr::type::get<float>())
                        prop.set_value(obj, val);
                    else
                        prop.set_value(obj, static_cast<double>(val));
                    return true;
                }
                return false;
            }
        }

        /// @param obj 인스턴스
        /// @param filter 필터 함수
        /// @return 변경 여부
        template<typename T>
        bool RenderInspector(T& obj, const std::function<bool(const std::string&)>& filter = nullptr)
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;

            bool changed = false;
            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                
                // roughness, metalness 프로퍼티는 자동으로 SliderFloat로 렌더링
                if (filter && !filter(propName))
                    continue;

                // 그 외 프로퍼티는 자동으로 렌더링
                if (propName == "roughness" || propName == "metalness")
                {
                    changed |= Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f);
                }
                else
                {
                    changed |= Detail::RenderProperty(prop, inst);
                }
            }

            return changed;
        }

        /// @param obj 인스턴스
        /// @param propName 프로퍼티 이름
        /// @param label 렌더링할 라벨
        /// @return 변경 여부
        // 프로퍼티 렌더링
        template<typename T>
        bool RenderProperty(T& obj, const std::string& propName, const std::string& label = "")
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;
            rttr::property prop = t.get_property(propName);
            
            if (!prop.is_valid())
            {
                OutputDebugStringA(("Type Not Registered: " + std::string(typeid(T).name()) + "\n").c_str());
                return false;
            }

            return Detail::RenderProperty(prop, inst, label.empty() ? propName : label);
        }

        // 프로퍼티 렌더링
        /// @param obj 인스턴스
        /// @param labelMap 렌더링할 라벨 맵
        /// @return 변경 여부
        template<typename T>
        bool RenderInspectorWithLabels(T& obj, const std::unordered_map<std::string, std::string>& labelMap)
        {
            rttr::type t = rttr::type::get(obj);
            rttr::instance inst = obj;

            bool changed = false;
            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                std::string displayLabel = propName;
                
                auto it = labelMap.find(propName);
                if (it != labelMap.end())
                    displayLabel = it->second;

                // roughness, metalness는 자동으로 SliderFloat로 렌더링
                if (propName == "roughness" || propName == "metalness")
                {
                    changed |= Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, displayLabel);
                }
                else
                {
                    changed |= Detail::RenderProperty(prop, inst, displayLabel);
                }
            }

            return changed;
        }
    }
}