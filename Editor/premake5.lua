project "Editor"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"Source/**.h",
		"Source/**.cpp"
	}

	includedirs
	{
		"%{wks.location}/Core/vendor/spdlog/include",
		"%{wks.location}/Core/Source",
		"%{wks.location}/Core/vendor",
		"%{IncludeDir.filewatch}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.entt}",
		"%{IncludeDir.ImGuizmo}",
		"%{IncludeDir.Tracy}",
		"%{IncludeDir.GLAD}",
		"%{IncludeDir.miniaudio}"
	}

	links
	{
		"Core",
		"GLAD",
		"%{Library.Tracy}"
	}

	defines
	{
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10"
	}

	filter "system:windows" 
		systemversion "latest"
		postbuildcommands {
			"{COPYDIR} %{wks.location}/Editor/assets %{wks.location}/bin/" .. outputdir .. "/Editor/assets",
			"{COPYDIR} %{wks.location}/Editor/Resources %{wks.location}/bin/" .. outputdir .. "/Editor/Resources",
			"{COPYFILE} %{wks.location}/Editor/imgui.ini %{wks.location}/bin/" .. outputdir .. "/Editor/imgui.ini",
		}

	filter "configurations:Debug"
		defines "LUX_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "LUX_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "LUX_DIST"
		runtime "Release"
		optimize "on"

	filter "action:vs2022"
    	buildoptions { "/utf-8" }
