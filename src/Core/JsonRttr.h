#pragma once

// RTTR <-> nlohmann::json 변환 유틸
// - 목적: 컴포넌트의 프로퍼티를 RTTR로 열거해서 JSON으로 저장/로드
// - 원칙: 짧고 단순하게, 실패는 즉시 false, 성공은 마지막 return true

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <rttr/type.h>
#include <rttr/enumeration.h>
#include <rttr/instance.h>
#include <rttr/property.h>
#include <rttr/variant.h>
#include <rttr/variant_associative_view.h>
#include <rttr/variant_sequential_view.h>

#include "json/json.hpp"

namespace Alice
{
    namespace JsonRttr
    {
        using json = nlohmann::json;

        inline bool EnsureParentDir(const std::filesystem::path& path)
        {
            const auto parent = path.parent_path();
            if (parent.empty())
                return true;

            if (std::filesystem::exists(parent))
                return true;

            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            return true;
        }

        inline bool LoadJsonFile(const std::filesystem::path& path, json& out)
        {
            out = json{};

            std::ifstream ifs(path);
            if (!ifs.is_open())
                return false;

            try
            {
                ifs >> out;
            }
            catch (...)
            {
                return false;
            }

            return true;
        }

        inline bool SaveJsonFile(const std::filesystem::path& path, const json& j, int indent = 4)
        {
            EnsureParentDir(path);

            std::ofstream ofs(path);
            if (!ofs.is_open())
                return false;

            ofs << j.dump(indent);
            return true;
        }

        inline json ToJsonVariant(const rttr::variant& v);
        inline bool FromJsonToProperty(rttr::instance obj, const rttr::property& prop, const json& jval);

        inline json ToJsonObject(rttr::instance obj)
        {
            json j = json::object();

            const rttr::type t = obj.get_type();
            for (const auto& prop : t.get_properties())
            {
                rttr::variant value = prop.get_value(obj);
                if (!value.is_valid())
                    continue;

                j[prop.get_name().to_string()] = ToJsonVariant(value);
            }

            return j;
        }

        inline bool FromJsonObject(rttr::instance obj, const json& j)
        {
            if (!j.is_object())
                return false;

            const rttr::type t = obj.get_type();
            for (const auto& prop : t.get_properties())
            {
                const std::string key = prop.get_name().to_string();
                auto it = j.find(key);
                if (it == j.end())
                    continue;

                if (!FromJsonToProperty(obj, prop, *it))
                    return false;
            }

            return true;
        }

        inline json ToJsonSequential(const rttr::variant_sequential_view& view)
        {
            json arr = json::array();
            for (std::size_t i = 0; i < view.get_size(); ++i)
            {
                rttr::variant item = view.get_value(i);
                arr.push_back(ToJsonVariant(item));
            }
            return arr;
        }

        inline json ToJsonAssociative(const rttr::variant_associative_view& view)
        {
            // 키가 문자열이면 object, 아니면 array of [k,v]
            const rttr::type keyType = view.get_key_type();
            if (keyType == rttr::type::get<std::string>())
            {
                json obj = json::object();
                for (auto it = view.begin(); it != view.end(); ++it)
                {
                    const std::string k = it.get_key().to_string();
                    obj[k] = ToJsonVariant(it.get_value());
                }
                return obj;
            }

            json arr = json::array();
            for (auto it = view.begin(); it != view.end(); ++it)
            {
                json pair = json::array();
                pair.push_back(ToJsonVariant(it.get_key()));
                pair.push_back(ToJsonVariant(it.get_value()));
                arr.push_back(pair);
            }
            return arr;
        }

        inline json ToJsonVariant(const rttr::variant& v)
        {
            if (!v.is_valid())
                return nullptr;

            const rttr::type t = v.get_type();

            if (t.is_arithmetic())
            {
                if (t == rttr::type::get<bool>())
                    return v.to_bool();
                if (t == rttr::type::get<int>())
                    return v.to_int();
                if (t == rttr::type::get<std::uint32_t>())
                    return static_cast<std::uint32_t>(v.to_int64());
                if (t == rttr::type::get<std::int64_t>())
                    return static_cast<std::int64_t>(v.to_int64());
                if (t == rttr::type::get<std::uint64_t>())
                    return static_cast<std::uint64_t>(v.to_int64());
                if (t == rttr::type::get<float>())
                    return v.to_double();
                if (t == rttr::type::get<double>())
                    return v.to_double();

                return v.to_string();
            }

            if (t.is_enumeration())
            {
                // 기본은 문자열 이름
                return v.to_string();
            }

            if (t == rttr::type::get<std::string>())
                return v.to_string();

            if (t.is_sequential_container())
                return ToJsonSequential(v.create_sequential_view());

            if (t.is_associative_container())
                return ToJsonAssociative(v.create_associative_view());

            if (t.is_class())
            {
                rttr::instance inst = v;
                return ToJsonObject(inst);
            }

            return v.to_string();
        }

        inline bool SetArithmetic(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            const rttr::type t = prop.get_type();

            if (t == rttr::type::get<bool>())
            {
                if (!jval.is_boolean() && !jval.is_number_integer())
                    return false;
                const bool b = jval.is_boolean() ? jval.get<bool>() : (jval.get<int>() != 0);
                prop.set_value(obj, b);
                return true;
            }

            if (t == rttr::type::get<int>())
            {
                if (!jval.is_number_integer())
                    return false;
                prop.set_value(obj, jval.get<int>());
                return true;
            }

            if (t == rttr::type::get<std::uint32_t>())
            {
                if (!jval.is_number_unsigned() && !jval.is_number_integer())
                    return false;
                prop.set_value(obj, static_cast<std::uint32_t>(jval.get<std::uint64_t>()));
                return true;
            }

            if (t == rttr::type::get<float>())
            {
                if (!jval.is_number())
                    return false;
                prop.set_value(obj, static_cast<float>(jval.get<double>()));
                return true;
            }

            if (t == rttr::type::get<double>())
            {
                if (!jval.is_number())
                    return false;
                prop.set_value(obj, jval.get<double>());
                return true;
            }

            if (t == rttr::type::get<std::int64_t>())
            {
                if (!jval.is_number_integer())
                    return false;
                prop.set_value(obj, static_cast<std::int64_t>(jval.get<std::int64_t>()));
                return true;
            }

            if (t == rttr::type::get<std::uint64_t>())
            {
                if (!jval.is_number_unsigned() && !jval.is_number_integer())
                    return false;
                prop.set_value(obj, static_cast<std::uint64_t>(jval.get<std::uint64_t>()));
                return true;
            }

            // 나머지 산술형은 문자열로라도 시도(최소 안전)
            prop.set_value(obj, rttr::variant(jval.dump()));
            return true;
        }

        inline bool SetString(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_string())
                return false;

            prop.set_value(obj, jval.get<std::string>());
            return true;
        }

        inline bool SetEnum(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_string() && !jval.is_number_integer())
                return false;

            const rttr::enumeration e = prop.get_type().get_enumeration();
            if (jval.is_string())
            {
                rttr::variant ev = e.name_to_value(jval.get<std::string>());
                if (!ev.is_valid())
                    return false;
                prop.set_value(obj, ev);
                return true;
            }

            // 숫자는 그대로 set_value()로 넣고, RTTR 변환에 맡깁니다.
            if (!prop.set_value(obj, jval.get<int>()))
                return false;

            return true;
        }

        inline bool SetClass(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!jval.is_object())
                return false;

            rttr::variant child = prop.get_value(obj);
            if (!child.is_valid())
            {
                child = prop.get_type().create();
                if (!child.is_valid())
                    return false;
            }

            rttr::instance childInst = child;
            if (!FromJsonObject(childInst, jval))
                return false;

            prop.set_value(obj, child);
            return true;
        }

        inline bool FromJsonToProperty(rttr::instance obj, const rttr::property& prop, const json& jval)
        {
            if (!prop.is_valid())
                return false;

            const rttr::type t = prop.get_type();

            if (t.is_arithmetic())
                return SetArithmetic(obj, prop, jval);

            if (t.is_enumeration())
                return SetEnum(obj, prop, jval);

            if (t == rttr::type::get<std::string>())
                return SetString(obj, prop, jval);

            if (t.is_class())
                return SetClass(obj, prop, jval);

            // 컨테이너는 현재 "읽기"는 최소 구현(필요하면 확장)
            return true;
        }
    }
}


