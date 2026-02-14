project "Sandbox"
	kind "ConsoleApp"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	links { "Core" }

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE", }

	files  { 
		"Source/**.h",
		"Source/**.c",
		"Source/**.hpp",
		"Source/**.cpp",
	}

	includedirs  {
		"Source/",

		"../Core/Source/",
		"../Core/vendor/"
	}

	defines
	{
		"LUX_PLATFORM_WINDOWS",
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10"
	}

	filter "system:windows" 
		systemversion "latest"

	filter "configurations:Debug"
		defines "LUX_DEBUG"
		runtime "Debug"
		symbols "on"
		ProcessDependencies("Debug")

	filter "configurations:Release"
		defines "LUX_RELEASE"
		runtime "Release"
		optimize "on"
		ProcessDependencies("Release")

	filter "configurations:Dist"
		defines "LUX_DIST"
		runtime "Release"
		optimize "on"
		ProcessDependencies("Dist")

	filter "action:vs2022"
      buildoptions { "/utf-8" }