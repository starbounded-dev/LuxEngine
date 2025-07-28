-- Non-Windows split premake project

StarEngineRootDirectory = os.getenv("STARENGINE_DIR")
include (path.join(StarEngineRootDirectory, "StarEngine", "vendor", "Coral", "Premake", "CSExtensions.lua"))

workspace "StarEngine-ScriptCore"
	configurations { "Debug", "Release" }

	filter "configurations:Debug or configurations:Debug-AS"
		optimize "Off"
		symbols "On"

	filter "configurations:Release"
		optimize "On"
		symbols "Default"

	filter "configurations:Dist"
		optimize "Full"
		symbols "Off"

	include "../StarEngine/vendor/Coral/Coral.Managed"

	project "StarEngine-ScriptCore"
		kind "SharedLib"
		language "C#"
		dotnetframework "net8.0"
		clr "Unsafe"
		targetdir ("../StarEditor/Resources/Scripts")
		objdir ("../StarEditor/Resources/Scripts/Intermediates")

		--linkAppReferences(false)

		--links { "Coral.Managed" }

		propertytags {
			{ "AppendTargetFrameworkToOutputPath", "false" },
			{ "Nullable", "enable" },
		}

		files {
			"Source/**.cs",
			"Properties/**.cs"
		}
