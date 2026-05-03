#pragma once

#include "TypeDescriptor.h"

#include <string>
#include <vector>

namespace Lux {
	namespace Reflection {

		struct Member
		{
			std::string Name;
			size_t Size = 0;
			std::string TypeName;

			enum EType
			{
				Function,
				Data
			} Type = Data;

			bool operator==(const Member& other) const
			{
				return Name == other.Name
					&& Size == other.Size
					&& TypeName == other.TypeName
					&& Type == other.Type;
			}
		};

		struct ClassInfo
		{
			std::string Name;
			size_t Size = 0;
			std::vector<Member> Members;

			template<class T>
			static ClassInfo Of()
			{
				static_assert(Type::Described<T>::value, "Type must be 'Described'.");

				ClassInfo info;
				using Descr = Type::Description<T>;
				info.Name = Descr::ClassName;
				info.Size = sizeof(T);

				for (const auto& memberName : Descr::MemberNames)
				{
					const bool isFunction = *Descr::IsFunctionByName(memberName);
					const auto typeName = *Descr::GetTypeNameByName(memberName);

					info.Members.push_back(Member{
						std::string(memberName),
						*Descr::GetMemberSizeByName(memberName) * !isFunction,
						std::string(typeName),
						isFunction ? Member::Function : Member::Data });
				}

				return info;
			}

			bool operator==(const ClassInfo& other) const
			{
				return Name == other.Name
					&& Size == other.Size
					&& Members == other.Members;
			}
		};
	}
}
