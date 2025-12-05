project "StarEditor"
	kind "ConsoleApp"

	debuggertype "NativeWithManagedCore"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	links { "StarEngine" }

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE", }

	files  { 
		"src/**.h",
		"src/**.c",
		"src/**.hpp",
		"src/**.cpp",
		
		-- Shaders
		"Resources/Shaders/**.glsl",
		"Resources/Shaders/**.glslh",
		"Resources/Shaders/**.hlsl",
		"Resources/Shaders/**.hlslh",
		"Resources/Shaders/**.slh",
	}

	includedirs  {
		"src/",

		"../StarEngine/src/",
		"../StarEngine/vendor/"
	}

	filter "system:windows" 
		systemversion "latest"
		defines { "SE_PLATFORM_WINDOWS" }

	filter "system:linux"
		defines { "SE_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

		result, err = os.outputof("pkg-config --libs gtk+-3.0")
		linkoptions { result }

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "SE_DEBUG" }

		ProcessDependencies("Debug")

	filter { "system:windows", "configurations:Debug-AS" }
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
        vectorextensions "AVX2"
        isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "SE_RELEASE", }

		ProcessDependencies("Release")

	filter "configurations:Debug or configurations:Debug-AS or configurations:Release"
		defines {
			"SE_TRACK_MEMORY",
			
            "JPH_DEBUG_RENDERER",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_EXTERNAL_PROFILE"
		}

	filter "files:**.hlsl"
		flags {"ExcludeFromBuild"}

    filter "configurations:Dist"
        flags { "ExcludeFromBuild" }
