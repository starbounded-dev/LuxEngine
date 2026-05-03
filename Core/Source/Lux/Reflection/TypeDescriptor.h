#pragma once

#include "Lux/Utilities/StringUtils.h"
#include "TypeName.h"
#include "TypeUtils.h"

#include <array>
#include <iostream>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Lux::Type {

	struct TDummyTag {};

	template<typename T, typename TTag = TDummyTag>
	struct Description;

	template<typename T, typename TTag = TDummyTag>
	using Described = is_specialized<Description<std::remove_cvref_t<T>, TTag>>;

	template<auto... MemberPointers>
	struct MemberList
	{
	public:
		using TTuple = decltype(std::tuple(MemberPointers...));

	private:
		template<size_t MemberIndex>
		using TMemberType = typename member_pointer::return_type<std::remove_cvref_t<decltype(std::get<MemberIndex>(TTuple()))>>::type;

		template<typename TMemberPtr>
		using TMemberPtrType = typename member_pointer::return_type<std::remove_cvref_t<TMemberPtr>>::type;

	public:
		using TVariant = std::variant<filter_void_t<TMemberPtrType<decltype(MemberPointers)>>...>;

	public:
		static constexpr size_t Count() { return sizeof...(MemberPointers); }

		template<typename TObj, typename TFunc>
		static constexpr auto Apply(TFunc func, TObj& obj)
		{
			return func(obj.*MemberPointers...);
		}

		template<typename TObj, typename TFunc>
		static constexpr auto Apply(TFunc func, const TObj& obj)
		{
			return func(obj.*MemberPointers...);
		}

		template<typename TObj, typename TFunc>
		static constexpr auto ApplyForEach(TFunc func, TObj& obj)
		{
			return (ApplyIfMemberNotFunction(func, MemberPointers, obj), ...);
		}

		template<typename TFunc>
		static constexpr auto ApplyToStaticType(TFunc f)
		{
			return f(MemberPointers...);
		}

	private:
		template<typename TFunc, typename TMemberPtr, typename TObj>
		static constexpr auto ApplyIfMemberNotFunction(TFunc func, TMemberPtr member, TObj& obj)
		{
			if constexpr (!std::is_member_function_pointer_v<decltype(member)>)
				func(obj.*member);
		}

		template<typename TObj, typename TFunc>
		static constexpr auto ApplyToMember(size_t memberIndex, TFunc&& f, TObj&& obj)
		{
			int memberCounter = 0;

			auto unwrapWithCounter = [&memberCounter, &f, memberIndex]()
			{
				auto unwrap = [memberCounter, &f, memberIndex](auto& memb)
				{
					if (memberCounter == memberIndex)
						f(memb);
				};

				memberCounter++;
				return unwrap;
			};

			(ApplyIfMemberNotFunction(unwrapWithCounter(), MemberPointers, std::forward<TObj>(obj)), ...);
		}

		template<size_t MemberIndex, typename TObj, typename TFunc>
		constexpr static auto ApplyToMember(TFunc&& f, TObj&& obj)
		{
			f(obj.*Type::nth_element<MemberIndex>(MemberPointers...));
		}

	public:
		template<typename TValue, typename TObj>
		static constexpr bool SetMemberValue(size_t memberIndex, const TValue& value, TObj&& obj)
		{
			bool valueSet = false;

			ApplyToMember(memberIndex,
				[&](auto& memb)
				{
					using TMemberNoCVR = std::remove_cvref_t<decltype(memb)>;
					using TValueNoCVR = std::remove_cvref_t<decltype(value)>;

					if constexpr (std::is_same_v<TValueNoCVR, TMemberNoCVR>)
					{
						memb = std::forward<decltype(value)>(value);
						valueSet = true;
					}
				}, std::forward<decltype(obj)>(obj));

			return valueSet;
		}

		template<size_t MemberIndex, typename TValue, typename TObj>
		static constexpr bool SetMemberValue(const TValue& value, TObj&& obj)
		{
			bool valueSet = false;

			ApplyToMember<MemberIndex>(
				[&](auto& memb)
				{
					using TMemberNoCVR = std::remove_cvref_t<decltype(memb)>;
					using TValueNoCVR = std::remove_cvref_t<decltype(value)>;

					if constexpr (std::is_same_v<TValueNoCVR, TMemberNoCVR>)
					{
						memb = std::forward<decltype(value)>(value);
						valueSet = true;
					}
				}, std::forward<decltype(obj)>(obj));

			return valueSet;
		}

		template<size_t MemberIndex, typename TObj>
		static constexpr auto GetMemberValue(const TObj& obj)
		{
			static_assert(Count() > MemberIndex);

			auto filter = [&obj](auto member)
			{
				if constexpr (std::is_member_function_pointer_v<decltype(member)>)
					return;
				else
					return obj.*member;
			};
			return filter(Type::nth_element<MemberIndex>(MemberPointers...));
		}

		template<typename TValue, typename TObj>
		static constexpr std::optional<TValue> GetMemberValueOfType(size_t memberIndex, const TObj& obj)
		{
			std::optional<TValue> value{};

			if (Count() > memberIndex)
			{
				ApplyToMember(memberIndex,
					[&](const auto& memb)
					{
						using TMember = std::remove_cvref_t<decltype(memb)>;

						if constexpr (std::is_same_v<TValue, TMember>)
							value = memb;
					}, std::forward<decltype(obj)>(obj));
			}

			return value;
		}

		template<size_t MemberIndex, typename TVariantType, typename TObj>
		static constexpr TVariantType GetMemberValue(const TObj& obj)
		{
			return TVariantType(GetMemberValue<MemberIndex>(obj));
		}

		template<typename TVariantType, typename TObj>
		static constexpr TVariantType GetMemberValue(size_t memberIndex, const TObj& obj)
		{
			TVariantType variant{};
			if (Count() > memberIndex)
			{
				ApplyToMember(memberIndex,
					[&](const auto& memb)
					{
						variant = TVariantType(memb);
					}, std::forward<decltype(obj)>(obj));
			}

			return variant;
		}

		template<size_t MemberIndex>
		static constexpr auto IsFunction()
		{
			static_assert(Count() > MemberIndex);
			return std::is_member_function_pointer_v<decltype(Type::nth_element<MemberIndex>(MemberPointers...))>;
		}

		static constexpr std::optional<bool> IsFunction(size_t memberIndex)
		{
			std::optional<bool> isFunction;

			if (Count() > memberIndex)
			{
				int memberCounter = 0;
				auto unwrap = [&isFunction, memberIndex](auto memb, int counter)
				{
					if (counter == memberIndex)
						isFunction = std::is_member_function_pointer_v<decltype(memb)>;
				};

				(unwrap(MemberPointers, memberCounter++), ...);
			}

			return isFunction;
		}

		template<size_t MemberIndex>
		static constexpr auto GetMemberSize()
		{
			return sizeof(TMemberType<MemberIndex>);
		}

		static constexpr auto GetMemberSize(size_t memberIndex)
		{
			std::optional<size_t> size;

			if (Count() > memberIndex)
			{
				int memberCounter = 0;
				auto unwrap = [&size, memberIndex](auto memb, int counter)
				{
					if (counter == memberIndex)
						size = sizeof(filter_void_t<TMemberPtrType<decltype(memb)>>);
				};

				(unwrap(MemberPointers, memberCounter++), ...);
			}

			return size;
		}

		template<size_t MemberIndex>
		static constexpr auto GetTypeName()
		{
			static_assert(Count() > MemberIndex);
			return type::type_name<TMemberType<MemberIndex>>();
		}

		static constexpr auto GetTypeName(size_t memberIndex)
		{
			std::optional<std::string_view> name;

			if (Count() > memberIndex)
			{
				int memberCounter = 0;
				auto unwrap = [&name, memberIndex](auto memb, int counter)
				{
					if (counter == memberIndex)
						name = type::type_name<TMemberPtrType<decltype(memb)>>();
				};

				(unwrap(MemberPointers, memberCounter++), ...);
			}

			return name;
		}
	};

	template<class TDescription, class TObjType, class TTag, class TList>
	struct DescriptionInterface;

#ifndef DESCRIBED
#define DESCRIBED_TAGGED(Class, Tag, ...) template<>\
struct Lux::Type::Description<Class, Tag> : Lux::Type::MemberList<__VA_ARGS__>, Lux::Type::DescriptionInterface<Lux::Type::Description<Class, Tag>, Class, Tag, Lux::Type::MemberList<__VA_ARGS__>>\
{\
private:\
	using MemberListType = Lux::Type::MemberList<__VA_ARGS__>;\
	static constexpr size_t NumberOfMembers = MemberListType::Count();\
	static constexpr std::string_view MemberStr{ #__VA_ARGS__ };\
	static constexpr std::string_view ClassStr{ #Class };\
	static constexpr std::string_view Delimiter{ ", " };\
public:\
	static constexpr std::string_view Namespace{ ClassStr.data(), ClassStr.find("::") == std::string_view::npos ? 0 : ClassStr.find_last_of(':') - 1 };\
	static constexpr std::string_view ClassName{ ClassStr.data() + (Namespace.size() ? Namespace.size() + 2 : 0) };\
	static constexpr std::array<std::string_view, NumberOfMembers> MemberNames = Lux::Utils::RemoveNamespace<NumberOfMembers>(Lux::Utils::SplitString<NumberOfMembers>(MemberStr, Delimiter));\
};

#define DESCRIBED(Class, ...) DESCRIBED_TAGGED(Class, Lux::Type::TDummyTag, __VA_ARGS__)
#endif

	template<class TDescription, class TObjType, class TTag, class TList>
	struct DescriptionInterface
	{
		static_assert(std::is_same_v<TDescription, Description<TObjType, TTag>>);

		static constexpr size_t NumberOfMembers = TList::Count();
		static constexpr size_t INVALID_INDEX = size_t(-1);

		static constexpr size_t IndexOf(std::string_view memberName)
		{
			for (size_t i = 0; i < TDescription::Count(); ++i)
			{
				if (TDescription::MemberNames[i] == memberName)
					return i;
			}

			return INVALID_INDEX;
		}

		static constexpr bool HasMember(std::string_view memberName)
		{
			for (const auto& name : TDescription::MemberNames)
			{
				if (name == memberName)
					return true;
			}
			return false;
		}

		static constexpr std::optional<std::string_view> GetMemberName(size_t memberIndex)
		{
			return (NumberOfMembers > memberIndex) ? std::optional<std::string_view>(TDescription::MemberNames[memberIndex]) : std::nullopt;
		}

		template<size_t MemberIndex>
		static constexpr std::string_view GetMemberName()
		{
			static_assert(NumberOfMembers > MemberIndex);
			return TDescription::MemberNames[MemberIndex];
		}

		template<typename TValue>
		static constexpr std::optional<TValue> GetMemberValueByName(std::string_view memberName, const TObjType& object)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? TList::template GetMemberValueOfType<TValue>(index, object) : std::nullopt;
		}

		template<typename TVariantType>
		static constexpr TVariantType GetMemberValueVariantByName(std::string_view memberName, const TObjType& object)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? TList::template GetMemberValue<TVariantType>(index, object) : TVariantType{};
		}

		template<typename TValue>
		static constexpr bool SetMemberValueByName(std::string_view memberName, const TValue& value, TObjType& object)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? TList::template SetMemberValue<TValue>(index, value, object) : false;
		}

		static constexpr std::optional<bool> IsFunctionByName(std::string_view memberName)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? TList::IsFunction(index) : std::nullopt;
		}

		static constexpr std::optional<size_t> GetMemberSizeByName(std::string_view memberName)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? TList::GetMemberSize(index) : std::nullopt;
		}

		static constexpr std::optional<std::string_view> GetTypeNameByName(std::string_view memberName)
		{
			const auto index = IndexOf(memberName);
			return (NumberOfMembers > index) ? std::optional<std::string_view>(TList::GetTypeName(index)) : std::nullopt;
		}

		static void Print(std::ostream& stream)
		{
			stream << "Class Name: '" << TDescription::ClassName << '\'' << '\n';
			stream << "Namespace: '" << TDescription::Namespace << '\'' << '\n';
			stream << "Number of members: " << NumberOfMembers << '\n';
			stream << "Members:" << '\n';
			stream << "---" << '\n';

			for (int i = 0; i < (int)NumberOfMembers; ++i)
			{
				stream << *TList::GetTypeName(i) << ' ' << TDescription::MemberNames[i] << ' ' << "(" << *TList::GetMemberSize(i) << " bytes)";

				if (*TList::IsFunction(i))
					stream << " (function)";

				stream << '\n';
			}
			stream << "---" << '\n';
		}

		static void Print(std::ostream& stream, const TObjType& obj)
		{
			stream << "Class Name: '" << TDescription::ClassName << '\'' << '\n';
			stream << "Namespace: '" << TDescription::Namespace << '\'' << '\n';
			stream << "Number of members: " << NumberOfMembers << '\n';
			stream << "Members:" << '\n';
			stream << "---" << '\n';

			auto unwrapOuter = [&stream, &obj](auto... members)
			{
				int memberCounter = 0;
				auto unwrap = [&stream, &obj](auto memb, int index)
				{
					stream << *TList::GetTypeName(index) << ' ';
					stream << TDescription::MemberNames[index];

					if constexpr (!std::is_member_function_pointer_v<decltype(memb)> && is_streamable_v<typename member_pointer::return_type<decltype(memb)>::type>)
						stream << "{ " << (obj.*memb) << " }";

					stream << ' ';
					stream << "(" << *TList::GetMemberSize(index) << " bytes)";

					if (*TList::IsFunction(index))
						stream << " (function)";

					stream << '\n';
				};
				(unwrap(members, memberCounter++), ...);
			};
			TList::ApplyToStaticType(unwrapOuter);

			stream << "---" << '\n';
		}
	};

	template<typename T, typename... Ts>
	std::ostream& operator<<(std::ostream& os, const std::variant<T, Ts...>& v)
	{
		std::visit([&os](auto&& arg)
		{
			os << arg;
		}, v);
		return os;
	}
} // namespace Lux::Type
