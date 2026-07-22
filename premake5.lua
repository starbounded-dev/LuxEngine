include "./vendor/premake_customization/solution_items.lua"

-- Opt-in integrations. These stay out of the build entirely unless requested, so
-- the default build never depends on an SDK that isn't checked out.
newoption {
	trigger = "discord",
	description = "Enable the Discord Social SDK integration (requires Core/vendor/discord_social_sdk)"
}

newoption {
	trigger = "no-tracy",
	description = "Build without Tracy profiler instrumentation"
}

newoption {
	trigger = "no-aftermath",
	description = "Build without the Nvidia Aftermath GPU crash tracker"
}

include "Dependencies.lua"

workspace "Lux"
	configurations { "Debug", "Debug-AS", "Release", "Dist" }
	startproject "Core"
    conformancemode "On"

	language "C++"
	cppdialect "C++20"
	staticruntime "Off"

	solution_items { ".editorconfig" }

	flags { "MultiProcessorCompile" }

	-- NOTE(Peter): Don't remove this. Please never use Annex K functions ("secure", e.g _s) functions.
	defines {
		"_CRT_SECURE_NO_WARNINGS",
		"NOMINMAX",
		"SPDLOG_USE_STD_FORMAT",
		"_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING",

		"LUX_HAS_VULKAN",
		"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1",
		"IMGUI_DEFINE_MATH_OPERATORS",
		"YAML_CPP_STATIC_DEFINE",
	}

	-- Tracy stays on by default. With "--no-tracy" the defines are omitted, which turns
	-- the vendored Tracy library into a stub and makes Profiler.h compile every
	-- LUX_PROFILE_* macro away - the same path Dist builds already take.
	if not _OPTIONS["no-tracy"] then
		defines {
			"TRACY_ENABLE",
			"TRACY_ON_DEMAND",
			"TRACY_CALLSTACK=10",
		}
	end

	-- Aftermath is already compiled out of Dist builds; this lets the other configs opt out too.
	if _OPTIONS["no-aftermath"] then
		defines { "LUX_DISABLE_AFTERMATH" }
	end

    filter "action:vs*"
        linkoptions { "/ignore:4099" } -- NOTE(Peter): Disable no PDB found warning
        disablewarnings { "4068" } -- Disable "Unknown #pragma mark warning"

	filter "language:C++ or language:C"
		architecture "x86_64"

	filter "configurations:Debug or configurations:Debug-AS"
		optimize "Off"
		symbols "On"

	filter { "system:windows", "configurations:Debug-AS" }	
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
		symbols "Default"
		defines { "NDEBUG" }

	filter "configurations:Dist"
		optimize "Full"
		symbols "Off"
		defines { "NDEBUG" }
		linktimeoptimization "On"

	filter "system:windows"
		buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "Core/vendor/Box2D"
	include "Core/vendor/JoltPhysics/JoltPhysicsPremake.lua"
	include "Core/vendor/GLFW"
	include "Core/vendor/imgui"
	include "Core/vendor/tracy"
	include "Core/vendor/NFD-Extended"
	include "Core/vendor/Coral/Coral.Native"
	include "Core/vendor/Coral/Coral.Managed"

	-- Coral's upstream premake only defines Debug/Release, and Coral.Managed dependson a
	-- Coral.Generator project that doesn't exist at our pinned commit. Rather than patch the
	-- vendored submodule (those edits wouldn't travel with the repo / would break fresh
	-- submodule checkouts), we re-open the projects here to add Lux's Debug-AS/Dist configs and
	-- a stub Coral.Generator so the dependson resolves.
	project "Coral.Native"
		filter { "configurations:Debug-AS" }
			runtime "Debug"
			symbols "On"
		filter { "system:windows", "configurations:Debug-AS" }
			sanitize { "Address" }
			flags { "NoRuntimeChecks", "NoIncrementalLink" }
			editandcontinue "Off" -- /ZI is incompatible with /fsanitize=address
		filter { "configurations:Dist" }
			runtime "Release"
			symbols "Off"
			optimize "On"
		filter {}

	-- Stub so Coral.Managed's `dependson { "Coral.Generator" }` resolves (the real generator
	-- isn't present at our pinned Coral commit; Coral.Managed builds fine without it).
	project "Coral.Generator"
		kind "Utility"
		targetdir "Core/vendor/Coral/Build/%{cfg.buildcfg}"
		objdir "Core/vendor/Coral/Intermediates/%{cfg.buildcfg}"
group ""

group "Dependencies/Text"
	include "Core/vendor/msdf-atlas-gen"
group ""

group "Dependencies/Renderer"
	include "Core/vendor/NVRHI"
group ""

group "Core"
	include "Core"
	include "ScriptCore"
group ""

group "Tools"
	include "Editor"
group ""

group "Runtime"
	include "Lux-Runtime"
group ""
