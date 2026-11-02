-- Developer-only command-line host for the production bank validator. No renderer is started.
project "Lux-AudioBankValidation"
	kind "ConsoleApp"
	targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")
	files { "AudioBankValidation.cpp" }
	includedirs { "../../Core/Source", "../../Core/vendor" }
	links { "Core" }
	defines { "LUX_PLATFORM_WINDOWS", "GLM_FORCE_DEPTH_ZERO_TO_ONE" }

	filter "configurations:Debug or configurations:Debug-AS"
		runtime "Debug"
		symbols "On"
		defines { "LUX_DEBUG" }
		ProcessDependencies("Debug")

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_RELEASE" }
		ProcessDependencies("Release")

	filter "configurations:Dist"
		runtime "Release"
		optimize "Full"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_DIST" }
		ProcessDependencies("Dist")
	filter {}
