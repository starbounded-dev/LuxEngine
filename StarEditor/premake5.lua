project "StarEditor"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

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
		"TRACY_CALLSTACK=10"
	}

	filter "system:windows" 
		systemversion "latest"
		postbuildcommands {}

	filter "configurations:Debug"
		defines "SE_DEBUG"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines "SE_RELEASE"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		defines "SE_DIST"
		runtime "Release"
		optimize "on"

	filter "action:vs2022"
    	buildoptions { "/utf-8" }


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
