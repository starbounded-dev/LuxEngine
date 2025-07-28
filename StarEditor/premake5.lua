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

		"../StarEngine/vendor/glm/glm/**.hpp",
		
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
		"../StarEngine/vendor/",
		"../StarEngine/vendor/spdlog/include",
		"../StarEngine/vendor/glm",
	}

	filter "system:windows"
		systemversion "latest"

		defines { "SE_PLATFORM_WINDOWS" }

		postbuildcommands {
			--'{COPY} "../StarEngine/vendor/NvidiaAftermath/lib/x64/windows/GFSDK_Aftermath_Lib.x64.dll" "%{cfg.targetdir}"',
		}

	filter { "system:windows", "configurations:Debug or configurations:Debug-AS" }
		postbuildcommands {
			--'{COPY} "../StarEngine/vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.dll" "%{cfg.targetdir}"',
		}

	filter { "system:windows", "configurations:Release or configurations:Dist" }
		postbuildcommands {
			--'{COPY} "../StarEngine/vendor/assimp/bin/windows/Release/assimp-vc143-mt.dll" "%{cfg.targetdir}"',
		}

	filter "system:linux"
		defines { "SE_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

		result, err = os.outputof("pkg-config --libs gtk+-3.0")
		linkoptions { result }

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

	filter "files:**.hlsl"
		flags {"ExcludeFromBuild"}

    filter "configurations:Dist"
        flags { "ExcludeFromBuild" }
