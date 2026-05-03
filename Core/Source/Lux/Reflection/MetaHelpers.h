#pragma once

#include <type_traits>

namespace meta {

	template<class...>
	struct All_same : std::true_type {};

	template<class First, class... Rest>
	struct All_same<First, Rest...> : std::bool_constant<std::conjunction_v<std::is_same<First, Rest>...>> {};

	template<class To, class... From>
	struct Convertible_from_all : std::bool_constant<std::conjunction_v<std::is_convertible<From, To>...>> {};

	template<class...>
	struct Meta_list {};

	struct Meta_nil {};

	template<auto... I>
	struct Meta_list_i {};

	template<class List>
	struct Meta_front_;

	template<class List>
	using Meta_front = typename Meta_front_<List>::type;

	template<template<class...> class List, class First, class... Rest>
	struct Meta_front_<List<First, Rest...>>
	{
		using type = First;
	};

	template<class List>
	struct Meta_pop_front_;

	template<class List>
	using Meta_pop_front = typename Meta_pop_front_<List>::type;

	template<template<class...> class List, class First, class... Rest>
	struct Meta_pop_front_<List<First, Rest...>>
	{
		using type = List<Rest...>;
	};

	template<class List, class Ty>
	struct Meta_push_front_;

	template<class List, class Ty>
	using Meta_push_front = typename Meta_push_front_<List, Ty>::type;

	template<template<class...> class List, class... Types, class Ty>
	struct Meta_push_front_<List<Types...>, Ty>
	{
		using type = List<Ty, Types...>;
	};

	template<template<class...> class Fn>
	struct Meta_quote
	{
		template<class... Types>
		using Invoke = Fn<Types...>;
	};

	template<template<auto...> class Fn>
	struct Meta_quote_i
	{
		template<auto... Values>
		using Invoke = Fn<Values...>;
	};

	template<class Fn, class... Args>
	using Meta_invoke = typename Fn::template Invoke<Args...>;

	template<class Fn, auto... Args>
	using Meta_invoke_i = typename Fn::template Invoke<Args...>;

	template<class Fn, class... Args>
	struct Meta_bind_back
	{
		template<class... Types>
		using Invoke = Meta_invoke<Fn, Types..., Args...>;
	};

	template<class Fn, class List>
	struct Meta_apply_;

	template<class Fn, class List>
	using Meta_apply = typename Meta_apply_<Fn, List>::type;

	template<class Fn, template<class...> class List, class... Types>
	struct Meta_apply_<Fn, List<Types...>>
	{
		using type = Meta_invoke<Fn, Types...>;
	};

	template<class Fn, class List>
	struct Meta_transform_;

	template<class Fn, class List>
	using Meta_transform = typename Meta_transform_<Fn, List>::type;

	template<class Fn, template<class...> class List, class... Types>
	struct Meta_transform_<Fn, List<Types...>>
	{
		using type = List<Meta_invoke<Fn, Types>...>;
	};

	template<class List, class Ty, template<class...> class Continue>
	struct Meta_push_back_;

	template<template<class...> class List, class... Types, class Ty, template<class...> class Continue>
	struct Meta_push_back_<List<Types...>, Ty, Continue>
	{
		using type = Continue<Types..., Ty>;
	};

	template<class...>
	struct Meta_concat_;

	template<class... Types>
	using Meta_concat = typename Meta_concat_<Types...>::type;

	template<template<class...> class List, class... Types>
	struct Meta_concat_<List<Types...>>
	{
		using type = List<Types...>;
	};

	template<template<class...> class List, class... A, class... B, class... Rest>
	struct Meta_concat_<List<A...>, List<B...>, Rest...>
	{
		using type = Meta_concat<List<A..., B...>, Rest...>;
	};

	template<class ListOfLists>
	using Meta_join = Meta_apply<Meta_quote<Meta_concat>, ListOfLists>;

	inline constexpr auto Meta_npos = ~size_t{ 0 };

	template<class List, class Ty>
	struct Meta_find_index_
	{
		using type = std::integral_constant<size_t, Meta_npos>;
	};

	template<class List, class Ty>
	using Meta_find_index = typename Meta_find_index_<List, Ty>::type;

	template<template<class...> class List, class First, class... Rest, class Ty>
	struct Meta_find_index_<List<First, Rest...>, Ty>
	{
	private:
		static constexpr bool Matches[] = { std::is_same_v<First, Ty>, std::is_same_v<Rest, Ty>... };
		static constexpr size_t Find()
		{
			for (size_t i = 0; i < sizeof...(Rest) + 1; i++)
			{
				if (Matches[i])
					return i;
			}
			return Meta_npos;
		}

	public:
		using type = std::integral_constant<size_t, Find()>;
	};
}
