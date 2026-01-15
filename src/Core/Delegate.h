#pragma once
#include <functional>

namespace Alice
{
	// 나중에 파라미터가 생기면 템플릿 인자만 수정하면 됩니다.
	template<typename... Args>
	class Delegate
	{
	public:
		using FunctionType = std::function<void(Args...)>;

		Delegate() = default;

		// 1. 멤버 함수 바인딩 (Unreal의 BindUObject / BindRaw)
		// 사용법: delegate.BindObject(this, &MyClass::MyFunc);
		template <typename T>
		void BindObject(T* instance, void(T::* method)(Args...))
		{
			// 람다로 래핑해서 멤버 함수 호출
			m_callback = [instance, method](Args... args)
			{
				(instance->*method)(args...);
			};
		}

		// 2. 람다 바인딩 (Unreal의 BindLambda)
		// 사용법: delegate.BindLambda([](){ ... });
		void BindLambda(FunctionType&& func)
		{
			m_callback = std::move(func);
		}

		// 3. 바인딩 해제 (Unbind)
		void Unbind()
		{
			m_callback = nullptr;
		}

		// 4. 실행 (Execute) - 바인딩 안되어 있으면 터질 수 있음
		void Execute(Args... args) const
		{
			if (IsBound())
			{
				m_callback(args...);
			}
		}

		// 바인딩 여부 확인
		bool IsBound() const { return m_callback != nullptr; }

	private:
		FunctionType m_callback;
	};

	// 사용법: ALICE_DECLARE_DELEGATE(이름)

	// 파라미터 0개
#define ALICE_DECLARE_DELEGATE(DelegateName) \
        using DelegateName = Alice::Delegate<>; 

	// 파라미터 1개
#define ALICE_DECLARE_DELEGATE_OneParam(DelegateName, Param1Type) \
        using DelegateName = Alice::Delegate<Param1Type>;

	// 파라미터 2개
#define ALICE_DECLARE_DELEGATE_TwoParams(DelegateName, Param1Type, Param2Type) \
        using DelegateName = Alice::Delegate<Param1Type, Param2Type>;
}