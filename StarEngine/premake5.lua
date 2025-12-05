project "StarEngine"
	kind "StaticLib"
	--dependson "Coral.Managed"

	debuggertype "NativeWithManagedCore"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "sepch.h"
	pchsource "src/sepch.cpp"

	files {
		"src/**.h",
		"src/**.c",
		"src/**.hpp",
		"src/**.cpp",

		"Platform/" .. firstToUpper(os.target()) .. "/**.hpp",
		"Platform/" .. firstToUpper(os.target()) .. "/**.cpp", 

		"vendor/FastNoise/**.cpp",

		"vendor/yaml-cpp/src/**.cpp",
		"vendor/yaml-cpp/src/**.h",
		"vendor/yaml-cpp/include/**.h",
		"vendor/VulkanMemoryAllocator/**.h",
		"vendor/VulkanMemoryAllocator/**.cpp",

		"vendor/imgui/misc/cpp/imgui_stdlib.cpp",
		"vendor/imgui/misc/cpp/imgui_stdlib.h"
	}

	removefiles {
		"src/StarEngine/Platform/DX11/**.cpp",
		"src/StarEngine/Platform/DX12/**.cpp",
	}

	includedirs { "src/", "vendor/", }

	IncludeDependencies()

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE" }

	filter "files:vendor/FastNoise/**.cpp or files:vendor/yaml-cpp/src/**.cpp or files:vendor/imgui/misc/cpp/imgui_stdlib.cpp or files:src/StarEngine/Tiering/TieringSerializer.cpp or files:src/StarEngine/Core/ApplicationSettings.cpp"
	flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"
		defines { "SE_PLATFORM_WINDOWS", }

	filter "system:linux"
		defines { "SE_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "SE_DEBUG", "_DEBUG", "ACL_ON_ASSERT_ABORT", }

	filter { "system:windows", "configurations:Debug-AS" }	
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "SE_RELEASE", "NDEBUG", }

	filter { "configurations:Debug or configurations:Debug-AS or configurations:Release" }
		defines {
			"SE_TRACK_MEMORY",

			"JPH_DEBUG_RENDERER",
			"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
			"JPH_EXTERNAL_PROFILE"
		}

	filter "configurations:Dist"
		optimize "On"
		symbols "Off"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "SE_DIST" }

		removefiles {
			"src/StarEngine/Platform/Vulkan/ShaderCompiler/**.cpp",
			"src/StarEngine/Platform/Vulkan/Debug/**.cpp",

			"src/StarEngine/Asset/AssimpAnimationImporter.cpp",
			"src/StarEngine/Asset/AssimpMeshImporter.cpp",
		}
