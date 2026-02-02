project "Core"
	kind "StaticLib"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	pchheader "lpch.h"
	pchsource "Source/lpch.cpp"

	files
	{
		"Source/**.h",
		"Source/**.cpp",
		"vendor/stb_image/**.h",
		"vendor/stb_image/**.cpp",
		"vendor/glm/glm/**.hpp",
		"vendor/glm/glm/**.inl",

		"vendor/imguizmo/ImGuizmo.h",
		"vendor/imguizmo/ImGuizmo.cpp"
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE",
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10",
		"YAML_CPP_STATIC_DEFINE",
		"VK_USE_PLATFORM_WIN32_KHR",
		"NOMINMAX"
	}

	includedirs
	{
		"Source",
		"vendor/spdlog/include",
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
		"%{IncludeDir.Tracy}",
		"%{IncludeDir.NVRHI}",
		"%{IncludeDir.SPIRV_Cross}",

		"%{IncludeDir.mono}",
		"%{IncludeDir.miniaudio}"
	}

	links
	{
		"GLFW",
		"GLAD",
		"imgui",
		"opengl32",
		"yaml-cpp",
		"msdf-atlas-gen",
		"Box2D",
		"DbgHelp",
		"dwmapi.lib",
		"NVRHI",

		"%{Library.Tracy}",
		"%{Library.mono}",
		"%{Library.Vulkan}",
	}

	filter "files:vendor/imguizmo/**.cpp"
	flags { "NoPCH" }

	filter "system:windows"
		systemversion "latest"

		defines
		{

		}

		links 
		{
			"%{Library.WinSock}",
			"%{Library.WinMM}",
			"%{Library.WinVersion}",
			"%{Library.BCrypt}",
		}

	filter "configurations:Debug"
		defines "LUX_DEBUG"
		runtime "Debug"
		symbols "on"

		links
		{
			"%{Library.ShaderC_Debug}",
			"SPIRV-Cross",
			"%{Library.DXC_Debug}"
		}

	filter "configurations:Release"
		defines "LUX_RELEASE"
		runtime "Release"
		optimize "on"

		links
		{
			"%{Library.ShaderC_Release}",
			"SPIRV-Cross",
			"%{Library.DXC_Release}"
		}

	filter "configurations:Dist"
		defines "LUX_DIST"
		runtime "Release"
		optimize "on"

		links
		{
			"%{Library.ShaderC_Release}",
			"SPIRV-Cross",
			"%{Library.DXC_Release}"
		}

	filter "action:vs2022"
		buildoptions { "/utf-8" }
