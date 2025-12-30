#include "sepch.h"
#include "EditorApplicationSettings.h"
#include "StarEngine/Utilities/FileSystem.h"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <filesystem>

namespace StarEngine {

	static std::filesystem::path s_EditorSettingsPath;
	
	EditorApplicationSettings& EditorApplicationSettings::Get()
	{
		static EditorApplicationSettings s_Settings;
		return s_Settings;
	}

	void EditorApplicationSettingsSerializer::Init()
	{
		s_EditorSettingsPath = std::filesystem::absolute("Config");

		if (!FileSystem::Exists(s_EditorSettingsPath))
			FileSystem::CreateDirectory(s_EditorSettingsPath);
		s_EditorSettingsPath /= "EditorApplicationSettings.yaml";

		LoadSettings();
	}

#define SE_ENTER_GROUP(name) currentGroup = rootNode[name];
#define SE_READ_VALUE(name, type, var, defaultValue) var = currentGroup[name].as<type>(defaultValue)

	void EditorApplicationSettingsSerializer::LoadSettings()
	{
		// Generate default settings file if one doesn't exist
		if (!FileSystem::Exists(s_EditorSettingsPath))
		{
			SaveSettings();
			return;
		}

		std::ifstream stream(s_EditorSettingsPath);
		SE_CORE_VERIFY(stream);
		std::stringstream ss;
		ss << stream.rdbuf();

		YAML::Node data = YAML::Load(ss.str());
		if (!data["EditorApplicationSettings"])
			return;

		YAML::Node rootNode = data["EditorApplicationSettings"];
		YAML::Node currentGroup = rootNode;

		auto& settings = EditorApplicationSettings::Get();

		SE_ENTER_GROUP("StarEditor");
		{
			SE_READ_VALUE("AdvancedMode", bool, settings.AdvancedMode, false);
			SE_READ_VALUE("HighlightUnsetMeshes", bool, settings.HighlightUnsetMeshes, true);
			SE_READ_VALUE("TranslationSnapValue", float, settings.TranslationSnapValue, 0.5f);
			SE_READ_VALUE("RotationSnapValue", float, settings.RotationSnapValue, 45.0f);
			SE_READ_VALUE("ScaleSnapValue", float, settings.ScaleSnapValue, 0.5f);
		}

		SE_ENTER_GROUP("Scripting");
		{
			SE_READ_VALUE("ShowHiddenFields", bool, settings.ShowHiddenFields, false);
			SE_READ_VALUE("DebuggerListenPort", int, settings.ScriptDebuggerListenPort, 2550);
		}

		SE_ENTER_GROUP("ContentBrowser");
		{
			SE_READ_VALUE("ShowAssetTypes", bool, settings.ContentBrowserShowAssetTypes, true);
			SE_READ_VALUE("ThumbnailSize", int, settings.ContentBrowserThumbnailSize, 128);
		}

		stream.close();
	}

#define SE_BEGIN_GROUP(name)\
		out << YAML::Key << name << YAML::Value << YAML::BeginMap;
#define SE_END_GROUP() out << YAML::EndMap;

#define SE_SERIALIZE_VALUE(name, value) out << YAML::Key << name << YAML::Value << value;

	void EditorApplicationSettingsSerializer::SaveSettings()
	{
		const auto& settings = EditorApplicationSettings::Get();

		YAML::Emitter out;
		out << YAML::BeginMap;
		SE_BEGIN_GROUP("EditorApplicationSettings");
		{
			SE_BEGIN_GROUP("StarEditor");
			{
				SE_SERIALIZE_VALUE("AdvancedMode", settings.AdvancedMode);
				SE_SERIALIZE_VALUE("HighlightUnsetMeshes", settings.HighlightUnsetMeshes);
				SE_SERIALIZE_VALUE("TranslationSnapValue", settings.TranslationSnapValue);
				SE_SERIALIZE_VALUE("RotationSnapValue", settings.RotationSnapValue);
				SE_SERIALIZE_VALUE("ScaleSnapValue", settings.ScaleSnapValue);
			}
			SE_END_GROUP(); 
			
			SE_BEGIN_GROUP("Scripting");
			{
				SE_SERIALIZE_VALUE("ShowHiddenFields", settings.ShowHiddenFields);
				SE_SERIALIZE_VALUE("DebuggerListenPort", settings.ScriptDebuggerListenPort);
			}
			SE_END_GROUP();

			SE_BEGIN_GROUP("ContentBrowser");
			{
				SE_SERIALIZE_VALUE("ShowAssetTypes", settings.ContentBrowserShowAssetTypes);
				SE_SERIALIZE_VALUE("ThumbnailSize", settings.ContentBrowserThumbnailSize);
			}
			SE_END_GROUP();
		}
		SE_END_GROUP();
		out << YAML::EndMap;

		std::ofstream fout(s_EditorSettingsPath);
		fout << out.c_str();
		fout.close();
	}


}
