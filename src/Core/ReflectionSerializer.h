#pragma once

// Windows.h의 min/max 매크로 충돌 방지 (RTTR 헤더와의 충돌 방지)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <rttr/type.h>
#include <rttr/variant.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <functional>
#include <vector>
#include <algorithm>

namespace Alice
{
    /// @note RTTR 기반으로 직렬화하는 유틸리티 클래스
    namespace ReflectionSerializer
    {
        namespace Detail
        {
            /// @param s 문자열 트림림
            inline void Trim(std::string& s)
            {
                const char* ws = " \t\r\n";
                const auto  b  = s.find_first_not_of(ws);
                const auto  e  = s.find_last_not_of(ws);
                if (b == std::string::npos)
                {
                    s.clear();
                    return;
                }
                s = s.substr(b, e - b + 1);
            }

            // 프로퍼티 저장
            /// @param ofs 출력 스트림
            /// @param propName 프로퍼티 이름
            /// @param value 프로퍼티 값
            inline void SaveProperty(std::ofstream& ofs, const std::string& propName, const rttr::variant& value)
            {
                const rttr::type valueType = value.get_type();
                ofs << propName << ": ";

                if (valueType.is_arithmetic())
                {
                    if (valueType == rttr::type::get<bool>())
                        ofs << (value.to_bool() ? "1" : "0");
                    else if (valueType == rttr::type::get<int>())
                        ofs << value.to_int();
                    else if (valueType == rttr::type::get<float>())
                        ofs << value.to_float();
                    else if (valueType == rttr::type::get<double>())
                        ofs << value.to_double();
                    else
                        ofs << value.to_string();
                }
                else if (valueType == rttr::type::get<std::string>())
                {
                    ofs << value.to_string();
                }
                else if (valueType.is_class())
                {
                    // 프로퍼티 값 저장
                    // XMFLOAT3, XMFLOAT4 타입 저장
                    rttr::instance inst = value;
                    rttr::type t = inst.get_type();
                    bool first = true;

                    for (auto& prop : t.get_properties())
                    {
                        if (!first) ofs << " ";
                        first = false;
                        rttr::variant propValue = prop.get_value(inst);
                        
                        if (propValue.get_type().is_arithmetic())
                        {
                            if (propValue.get_type() == rttr::type::get<bool>())
                                ofs << (propValue.to_bool() ? "1" : "0");
                            else if (propValue.get_type() == rttr::type::get<int>())
                                ofs << propValue.to_int();
                            else if (propValue.get_type() == rttr::type::get<float>())
                                ofs << propValue.to_float();
                            else
                                ofs << propValue.to_string();
                        }
                        else
                        {
                            ofs << propValue.to_string();
                        }
                    }
                }
                else
                {
                    ofs << value.to_string();
                }

                ofs << "\n";
            }

            /// @param propName 프로퍼티 이름
            /// @param valueStr 프로퍼티 값 문자열
            /// @param obj 인스턴스
            /// @return 로드 성공 여부
            inline bool LoadProperty(const std::string& propName, const std::string& valueStr, rttr::instance& obj)
            {
                rttr::type t = obj.get_type();
                rttr::property prop = t.get_property(propName);
                if (!prop.is_valid())
                    return false;

                rttr::type propType = prop.get_type();

                if (propType.is_arithmetic())
                {
                    if (propType == rttr::type::get<bool>())
                    {
                        int val = std::stoi(valueStr);
                        prop.set_value(obj, val != 0);
                    }
                    else if (propType == rttr::type::get<int>())
                    {
                        prop.set_value(obj, std::stoi(valueStr));
                    }
                    else if (propType == rttr::type::get<float>())
                    {
                        prop.set_value(obj, std::stof(valueStr));
                    }
                    else if (propType == rttr::type::get<double>())
                    {
                        prop.set_value(obj, std::stod(valueStr));
                    }
                }
                else if (propType == rttr::type::get<std::string>())
                {
                    prop.set_value(obj, valueStr);
                }
                else if (propType.is_class())
                {
                    // 프로퍼티 값 로드
                    std::istringstream iss(valueStr);
                    std::vector<std::string> tokens;
                    std::string token;
                    while (iss >> token)
                        tokens.push_back(token);

                    // XMFLOAT3, XMFLOAT4 타입 로드
                    rttr::variant propValue = prop.get_value(obj);
                    if (!propValue.is_valid())
                    {
                        // 새 인스턴스 생성
                        rttr::variant newInst = propType.create();
                        if (newInst.is_valid())
                        {
                            rttr::instance inst = newInst;
                            int index = 0;
                            for (auto& subProp : propType.get_properties())
                            {
                                if (index >= static_cast<int>(tokens.size()))
                                    break;

                                const std::string& valStr = tokens[index];
                                rttr::type subType = subProp.get_type();

                                if (subType.is_arithmetic())
                                {
                                    if (subType == rttr::type::get<bool>())
                                        subProp.set_value(inst, std::stoi(valStr) != 0);
                                    else if (subType == rttr::type::get<int>())
                                        subProp.set_value(inst, std::stoi(valStr));
                                    else if (subType == rttr::type::get<float>())
                                        subProp.set_value(inst, std::stof(valStr));
                                }
                                ++index;
                            }
                            prop.set_value(obj, newInst);
                        }
                    }
                    else
                    {
                        rttr::instance inst = propValue;
                        int index = 0;
                        for (auto& subProp : propType.get_properties())
                        {
                            if (index >= static_cast<int>(tokens.size()))
                                break;

                            const std::string& valStr = tokens[index];
                            rttr::type subType = subProp.get_type();

                            if (subType.is_arithmetic())
                            {
                                if (subType == rttr::type::get<bool>())
                                    subProp.set_value(inst, std::stoi(valStr) != 0);
                                else if (subType == rttr::type::get<int>())
                                    subProp.set_value(inst, std::stoi(valStr));
                                else if (subType == rttr::type::get<float>())
                                    subProp.set_value(inst, std::stof(valStr));
                            }
                            ++index;
                        }
                        prop.set_value(obj, propValue);
                    }
                }

                return true;
            }
        }

        /// @param path 파일 경로
        /// @param obj 인스턴스
        /// @return 저장 성공 여부
        template<typename T>
        bool Save(const std::filesystem::path& path, const T& obj)
        {
            auto parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent))
            {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }

            std::ofstream ofs(path);
            if (!ofs.is_open())
                return false;

            // 템플릿 파라미터로 타입 가져오기
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                // 실패하면 객체로부터 타입 가져오기 시도
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = const_cast<T&>(obj);

            // 프로퍼티 저장
            for (auto& prop : t.get_properties())
            {
                rttr::variant value = prop.get_value(inst);
                if (value.is_valid())
                {
                    Detail::SaveProperty(ofs, prop.get_name().to_string(), value);
                }
            }

            return true;
        }

        /// @param path 파일 경로
        /// @param obj 인스턴스
        /// @return 로드 성공 여부
        template<typename T>
        bool Load(const std::filesystem::path& path, T& obj)
        {
            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            // 템플릿 파라미터로 타입 가져오기
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                // 실패하면 객체로부터 타입 가져오기 시도
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = obj;

            std::string line;
            while (std::getline(ifs, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;

                std::istringstream iss(line);
                std::string key;
                if (!std::getline(iss, key, ':'))
                    continue;

                std::string value;
                std::getline(iss, value);

                Detail::Trim(key);
                Detail::Trim(value);

                Detail::LoadProperty(key, value, inst);
            }

            return true;
        }

        /// @param obj 인스턴스
        /// @param filter 필터 함수
        /// @return 저장 성공 여부
        template<typename T>
        bool SaveFiltered(const std::filesystem::path& path, const T& obj, 
                         const std::function<bool(const std::string&)>& filter)
        {
            auto parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent))
            {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }

            std::ofstream ofs(path);
            if (!ofs.is_open())
                return false;

            // 템플릿 파라미터로 타입 가져오기
            rttr::type t = rttr::type::get<T>();
            if (!t.is_valid())
            {
                // 실패하면 객체로부터 타입 가져오기 시도
                t = rttr::type::get(obj);
                if (!t.is_valid())
                    return false;
            }
            
            rttr::instance inst = const_cast<T&>(obj);

            for (auto& prop : t.get_properties())
            {
                std::string propName = prop.get_name().to_string();
                if (filter(propName))
                {
                    rttr::variant value = prop.get_value(inst);
                    if (value.is_valid())
                    {
                        Detail::SaveProperty(ofs, propName, value);
                    }
                }
            }

            return true;
        }
    }
}
