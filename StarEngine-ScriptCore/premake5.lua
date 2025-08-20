project "StarEngine-ScriptCore"
	filter { "not action:vs*" }
        kind "StaticLib"
	filter { "action:vs*" }
		kind "SharedLib"
		language "C#"
		dotnetframework "net9.0"
		clr "Unsafe"
		targetdir ("%{wks.location}/StarEditor/Resources/Scripts")
		objdir ("%{wks.location}/StarEditor/Resources/Scripts/Intermediates")

		links {
			"Coral.Managed"
		}

		vsprops {
			AppendTargetFrameworkToOutputPath = "false",
			Nullable = "enable",
			CopyLocalLockFileAssemblies = "true",
			EnableDynamicLoading = "true",
		}

	files 
	{
		"Source/**.cs"
	}

	links
	{
		"Coral.Managed"
	}
	
	filter "configurations:Debug"
		optimize "Off"
		symbols "Default"

	filter "configurations:Release"
		optimize "On"
		symbols "Default"

	filter "configurations:Dist"
		optimize "Full"
		symbols "Off"
