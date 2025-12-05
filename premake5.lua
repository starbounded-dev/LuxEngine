include "./vendor/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "StarEngine"
	configurations { "Debug", "Debug-AS", "Release", "Dist" }
	startproject "StarEditor"
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
		"TRACY_ENABLE",
		"TRACY_ON_DEMAND",
		"TRACY_CALLSTACK=10",
		
		"SE_HAS_VULKAN",
		"VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1",
		"IMGUI_DEFINE_MATH_OPERATORS",
		"YAML_CPP_STATIC_DEFINE",
	}

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

	filter "system:windows"
		buildoptions { "/EHsc", "/Zc:preprocessor", "/Zc:__cplusplus" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "StarEngine/vendor/Box2D"
	include "StarEngine/vendor/GLFW"
	include "StarEngine/vendor/imgui"
	include "StarEngine/vendor/NFD-Extended"
	include "StarEngine/vendor/yaml-cpp"
	include "StarEngine/vendor/Tracy"
group ""

group "Dependencies/Text"
	include "StarEngine/vendor/msdf-atlas-gen"
group ""
group "Dependencies/Coral"
	include "StarEngine/vendor/Coral/Coral.Native"
	include "StarEngine/vendor/Coral/Coral.Managed"
group ""

group "Dependencies/Renderer"
	include "StarEngine/vendor/NVRHI"
	include "StarEngine/vendor/GLAD"
group ""

group "Dependencies/Physics"
	include "StarEngine/vendor/JoltPhysics/JoltPhysicsPremake"
group ""

group "Core"
	include "StarEngine"
	include "StarEngine-ScriptCore"
group ""

group "Tools"
	include "StarEditor"
group ""

group "Runtime"
	include "Sandbox"
group ""
