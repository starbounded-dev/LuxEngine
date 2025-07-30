project "StarEngine"
	kind "StaticLib"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "sepch.h"
	pchsource "src/sepch.cpp"

	files {
		"src/**.h",
		"src/**.c",
		"src/**.hpp",
		"src/**.cpp",

		"vendor/stb_image/**.h",
		"vendor/stb_image/**.cpp",
		"vendor/glm/glm/**.hpp",
		"vendor/glm/glm/**.inl",

		"vendor/imguizmo/ImGuizmo.h",
		"vendor/imguizmo/ImGuizmo.cpp"
	}

	defines {
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE",
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10",
		"YAML_CPP_STATIC_DEFINE",
		"GLM_FORCE_DEPTH_ZERO_TO_ONE"
	}

	includedirs {
		"src/",
		"vendor/",
		"%{IncludeDir.GLFW}",
		"%{IncludeDir.GLAD}",
		"%{IncludeDir.Box2D}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.filewatch}",
		"%{IncludeDir.stb_image}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.msdfgen}",
		"%{IncludeDir.msdf_atlas_gen}",
		"%{IncludeDir.yaml_cpp}",
		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.VulkanSDK}",
		"%{IncludeDir.Coral}",
		"%{IncludeDir.MagicEnum}",
		"%{IncludeDir.Tracy}",
		"%{IncludeDir.miniaudio}"
	}

	links {
		"GLFW",
		"GLAD",
		"imgui",
		"opengl32",
		"spdlog",
		"%{Library.Coral}",
		"yaml-cpp",
		"msdf-atlas-gen",
		"Box2D",
		"DbgHelp",
		"dwmapi.lib",
		"%{Library.Tracy}",
	}

	filter "files:vendor/imguizmo/**.cpp"
	flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"
		defines { "SE_PLATFORM_WINDOWS", }

		links {
			"%{Library.WinSock}",
			"%{Library.WinMM}",
			"%{Library.WinVersion}",
			"%{Library.BCrypt}",
		}

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "SE_DEBUG", "_DEBUG", "ACL_ON_ASSERT_ABORT", }
		links {
			"%{Library.ShaderC_Debug}",
			"%{Library.SPIRV_Cross_Debug}",
			"%{Library.SPIRV_Cross_GLSL_Debug}"
		}

	filter { "system:windows", "configurations:Debug-AS" }	
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "SE_RELEASE", "NDEBUG", }

		links {
			"%{Library.ShaderC_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_GLSL_Release}"
		}

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

		links {
			"%{Library.ShaderC_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_GLSL_Release}"
		}

	filter "action:vs2022"
		buildoptions { "/utf-8" }

	filter { "configurations:Debug-AS" }
        postbuildcommands {
			'{MKDIR} "%{wks.location}/StarEditor/DotNet"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Coral.Managed.runtimeconfig.json" "%{wks.location}/StarEditor/DotNet/Coral.Managed.runtimeconfig.json"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Build/Debug-AS-%{cfg.system}/Coral.Managed.dll" "%{wks.location}/StarEditor/DotNet/Coral.Managed.dll"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Build/Debug-AS-%{cfg.system}/Coral.Managed.pdb" "%{wks.location}/StarEditor/DotNet/Coral.Managed.pdb"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Build/Debug-AS-%{cfg.system}/Coral.Managed.deps.json" "%{wks.location}/StarEditor/DotNet/Coral.Managed.deps.json"',
	    }

    filter { "configurations:Debug" }
        postbuildcommands {
			'{MKDIR} "%{wks.location}/StarEditor/DotNet"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Coral.Managed.runtimeconfig.json" "%{wks.location}/StarEditor/DotNet/Coral.Managed.runtimeconfig.json"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Debug/Coral.Managed.dll" "%{wks.location}/StarEditor/DotNet/Coral.Managed.dll"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Debug/Coral.Managed.pdb" "%{wks.location}/StarEditor/DotNet/Coral.Managed.pdb"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Debug/Coral.Managed.deps.json" "%{wks.location}/HStarEditorazelnut/DotNet/Coral.Managed.deps.json"',
	    }

    filter { "configurations:Release" }
        postbuildcommands {
			'{MKDIR} "%{wks.location}/StarEditor/DotNet"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Coral.Managed/Coral.Managed.runtimeconfig.json" "%{wks.location}/StarEditor/DotNet/Coral.Managed.runtimeconfig.json"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Release/Coral.Managed.dll" "%{wks.location}/StarEditor/DotNet/Coral.Managed.dll"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Release/Coral.Managed.pdb" "%{wks.location}/StarEditor/DotNet/Coral.Managed.pdb"',
		    '{COPYFILE} "%{wks.location}/StarEngine/vendor/Coral/Build/Release/Coral.Managed.deps.json" "%{wks.location}/StarEditor/DotNet/Coral.Managed.deps.json"',
	    }
