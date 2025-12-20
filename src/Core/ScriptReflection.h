#pragma once

// Script 전용 RTTR 등록/SerializeField 유틸
// - C++은 "변수 위에 UPROPERTY" 같은 코드 분석이 없으므로, 등록은 RTTR_REGISTRATION에서 합니다.
// - 대신 매크로로 (이름 문자열/Getter/Setter/메타데이터)를 한 번에 묶어서 짧게 씁니다.

#include <rttr/registration.h>

// ---- Field declaration helpers (in .h) ----
// private 필드는 RTTR이 직접 주소를 못 잡으므로, getter/setter를 자동 생성해서 등록합니다.
#define ALICE_SERIALIZE_FIELD(Type, Name, DefaultValue) \
private: \
    Type Name = DefaultValue; \
public: \
    Type Get_##Name() const { return Name; } \
    void Set_##Name(Type v) { Name = v; }

// ---- RTTR registration helpers (in .cpp) ----
#define ALICE_SCRIPT_REFLECT_BEGIN(Type) \
RTTR_REGISTRATION \
{ \
    rttr::registration::class_<Type>(#Type).constructor<>()

// public 필드 등록 (public 멤버만)
#define ALICE_SCRIPT_PUBLIC_FIELD(Type, Name) \
    .property(#Name, &Type::Name)

// SerializeField 등록 (ALICE_SERIALIZE_FIELD로 만든 getter/setter 대상)
#define ALICE_SCRIPT_SERIALIZE_FIELD(Type, Name) \
    .property(#Name, &Type::Get_##Name, &Type::Set_##Name)(rttr::metadata("SerializeField", true))

// EntityId 필드에 "이 컴포넌트를 가진 엔티티만 선택" 같은 필터를 걸고 싶을 때 사용(문자열 기반)
#define ALICE_SCRIPT_ENTITY_FIELD(Type, Name, RequiredComponentName) \
    .property(#Name, &Type::Get_##Name, &Type::Set_##Name) \
    (rttr::metadata("SerializeField", true), rttr::metadata("EntityRef", true), rttr::metadata("RequiredComponent", RequiredComponentName))

#define ALICE_SCRIPT_REFLECT_END() \
    ; \
}

// ---- Access helpers (anywhere) ----
// 문자열 오타 방지: 변수명 토큰을 그대로 씁니다.
#define ALICE_GET_PROP(Instance, Name) \
    (rttr::type::get(Instance).get_property(#Name).get_value(Instance))

#define ALICE_SET_PROP(Instance, Name, Value) \
    (rttr::type::get(Instance).get_property(#Name).set_value(Instance, (Value)))

#define ALICE_GET_PROP_AS(Instance, Name, Type) \
    (rttr::type::get(Instance).get_property(#Name).get_value(Instance).get_value<Type>())


