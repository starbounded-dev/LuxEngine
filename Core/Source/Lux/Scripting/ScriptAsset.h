#pragma once

#include "Lux/Asset/Asset.h"

#include <string>

namespace Lux {

	// A C# script class treated as an asset (so it shows in the content browser / asset registry).
	// The runtime script identity is an FNV hash of the full type name (computed in
	// ScriptEngine::BuildAssemblyCache), not this AssetHandle. (Ported from Hazel's ScriptFileAsset.)
	class ScriptFileAsset : public Asset
	{
	public:
		ScriptFileAsset() = default;
		ScriptFileAsset(const char* classNamespace, const char* className)
			: m_ClassNamespace(classNamespace), m_ClassName(className)
		{
		}

		const std::string& GetClassNamespace() const { return m_ClassNamespace; }
		const std::string& GetClassName() const { return m_ClassName; }

		static AssetType GetStaticType() { return AssetType::ScriptFile; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

	private:
		std::string m_ClassNamespace = "";
		std::string m_ClassName = "";
	};

}
