StarEngineRootDirectory = os.getenv("STARENGINE_DIR")

--include (path.join(StarEngineRootDirectory, "StarEngine", "vendor", "Coral", "Premake", "CSExtensions.lua"))
--include (path.join(StarEngineRootDirectory, "StarEngine", "vendor", "Coral", "Coral.Managed"))

project "StarEngine-ScriptCore"
	kind "SharedLib"
	language "C#"
	dotnetframework "net8.0"
	clr "Unsafe"
	targetdir "%{StarEngineRootDirectory}/StarEditor/Resources/Scripts"
	objdir "%{StarEngineRootDirectory}/StarEditor/Resources/Scripts/Intermediates"

	--links { "Coral.Managed" }

	--propertytags {{ "AppendTargetFrameworkToOutputPath", "false" },{ "Nullable", "enable" },}

	files {
		"%{StarEngineRootDirectory}/StarEngine-ScriptCore/Source/**.cs",
		"%{StarEngineRootDirectory}/StarEngine-ScriptCore/Properties/**.cs"
	}