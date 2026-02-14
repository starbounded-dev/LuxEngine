project "Editor"
    kind "ConsoleApp"

    debuggertype "NativeWithManagedCore"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	links { "Core" }

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE", }

	files  { 
		"Source/**.h",
		"Source/**.c",
		"Source/**.hpp",
		"Source/**.cpp",
		
		-- Shaders
		"Resources/Shaders/**.glsl",
		"Resources/Shaders/**.glslh",
		"Resources/Shaders/**.hlsl",
		"Resources/Shaders/**.hlslh",
		"Resources/Shaders/**.slh",
	}

	includedirs  {
		"Source/",

		"../Core/Source/",
		"../Core/vendor/"
	}

	filter "system:windows"
		systemversion "latest"

		defines { "LUX_PLATFORM_WINDOWS" }

	filter "system:linux"
		defines { "LUX_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

		result, err = os.outputof("pkg-config --libs gtk+-3.0")
		linkoptions { result }

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "LUX_DEBUG" }

		ProcessDependencies("Debug")

	filter { "system:windows", "configurations:Debug-AS" }
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
        vectorextensions "AVX2"
        isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_RELEASE", }

		ProcessDependencies("Release")

	filter "configurations:Debug or configurations:Debug-AS or configurations:Release"
		defines {
			"LUX_TRACK_MEMORY",
			
            "JPH_DEBUG_RENDERER",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_EXTERNAL_PROFILE"
		}

	filter "files:**.hlsl"
		flags {"ExcludeFromBuild"}

    filter "configurations:Dist"
        flags { "ExcludeFromBuild" }