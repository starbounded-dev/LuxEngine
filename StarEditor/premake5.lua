project "StarEditor"
	kind "ConsoleApp"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
		"%{wks.location}/StarEngine/vendor/spdlog/include",
		"%{wks.location}/StarEngine/src",
		"%{wks.location}/StarEngine/vendor",
		"%{IncludeDir.filewatch}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.Tracy}",
		"%{IncludeDir.GLAD}",
		"%{IncludeDir.Coral}",
		"%{IncludeDir.miniaudio}"
	}

	links
	{
		"StarEngine",
		"yaml-cpp",
		"GLAD",
		"%{Library.Tracy}"
	}

	defines
	{
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10",
		"GLM_FORCE_DEPTH_ZERO_TO_ONE"
	}

	filter "system:windows"
		systemversion "latest"

		defines { "SE_PLATFORM_WINDOWS" }

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "SE_DEBUG" }

	filter { "system:windows", "configurations:Debug-AS" }
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
        vectorextensions "AVX2"
        isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "SE_RELEASE", }

	filter "configurations:Debug or configurations:Debug-AS or configurations:Release"
		defines {
			"SE_TRACK_MEMORY",
			
            "JPH_DEBUG_RENDERER",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_EXTERNAL_PROFILE"
		}

	filter "configurations:Dist"
        flags { "ExcludeFromBuild" }
		optimize "On"
		symbols "Off"
		defines { "SE_DIST" }

	filter "action:vs2022"
    	buildoptions { "/utf-8" }

	filter "system:linux"
		defines { "SE_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

		result, err = os.outputof("pkg-config --libs gtk+-3.0")
		linkoptions { result }

project "Coral.Native"
	dependson "Coral.Managed"

	filter { "configurations:Debug" }
		postbuildcommands
		{
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Coral.Managed/Coral.Managed.runtimeconfig.json" "%{wks.location}StarEditor/DotNet/Coral.Managed.runtimeconfig.json"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Debug/Coral.Managed.dll" "%{wks.location}StarEditor/DotNet/Coral.Managed.dll"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Debug/Coral.Managed.pdb" "%{wks.location}StarEditor/DotNet/Coral.Managed.pdb"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Debug/Coral.Managed.deps.json" "%{wks.location}StarEditor/DotNet/Coral.Managed.deps.json"',
		}

	filter { "configurations:Release" }
		postbuildcommands
		{
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Coral.Managed/Coral.Managed.runtimeconfig.json" "%{wks.location}StarEditor/DotNet/Coral.Managed.runtimeconfig.json"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Release/Coral.Managed.dll" "%{wks.location}StarEditor/DotNet/Coral.Managed.dll"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Release/Coral.Managed.pdb" "%{wks.location}StarEditor/DotNet/Coral.Managed.pdb"',
			'{COPYFILE} "%{wks.location}StarEngine/vendor/Coral/Build/Release/Coral.Managed.deps.json" "%{wks.location}StarEditor/DotNet/Coral.Managed.deps.json"',
		}
