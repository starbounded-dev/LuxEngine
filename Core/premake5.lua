project "Core"
	kind "StaticLib"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "lpch.h"
	pchsource "Source/lpch.cpp"

	files {
		"Source/**.h",
		"Source/**.c",
		"Source/**.hpp",
		"Source/**.cpp",

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
		"Source/Lux/Platform/DX11/**.cpp",
		"Source/Lux/Platform/DX12/**.cpp",
	}

	includedirs { "Source/", "vendor/", }

	IncludeDependencies()

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE" }

	filter "files:vendor/FastNoise/**.cpp or files:vendor/yaml-cpp/src/**.cpp or files:vendor/imgui/misc/cpp/imgui_stdlib.cpp or files:Source/Lux/Core/ApplicationSettings.cpp"
	flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"
		defines { "LUX_PLATFORM_WINDOWS", }

	filter "system:linux"
		defines { "LUX_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "LUX_DEBUG", "_DEBUG", "ACL_ON_ASSERT_ABORT", }
		IncludeDependencies("Debug")

	filter { "system:windows", "configurations:Debug-AS" }	
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_RELEASE", "NDEBUG", }
		IncludeDependencies("Release")

	filter { "configurations:Debug or configurations:Debug-AS or configurations:Release" }
		defines {
			"LUX_TRACK_MEMORY",

			"JPH_DEBUG_RENDERER",
			"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
			"JPH_EXTERNAL_PROFILE"
		}

	filter "configurations:Dist"
		optimize "On"
		symbols "Off"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_DIST" }
		IncludeDependencies("Dist")

		removefiles {
			"Source/Lux/Platform/Vulkan/ShaderCompiler/**.cpp",
			"Source/Lux/Platform/Vulkan/Debug/**.cpp",

			"Source/Lux/Asset/AssimpAnimationImporter.cpp",
			"Source/Lux/Asset/AssimpMeshImporter.cpp",
		}
