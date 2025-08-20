include "./vendor/premake/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "StarEngine"
	configurations { "Debug", "Debug-AS", "Release", "Dist" }
	startproject "StarEditor"
	conformancemode "On"

	defines {
		"IMGUI_DEFINE_MATH_OPERATORS"
	}

	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

	filter "action:vs*"
        linkoptions { "/ignore:4099" } -- NOTE(Peter): Disable no PDB found warning
        disablewarnings { "4068" } -- Disable "Unknown #pragma mark warning"

	filter "language:C++ or language:C"
		architecture "x86_64"

	filter "configurations:Debug or configurations:Debug-AS"
		optimize "Off"
		symbols "On"

	filter "files:**.c"
		flags {"NoPCH"}

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
	include "vendor/premake"
	include "StarEngine/vendor/Box2D"
	include "StarEngine/vendor/GLFW"
	include "StarEngine/vendor/GLAD"
	include "StarEngine/vendor/imgui"
	include "StarEngine/vendor/yaml-cpp"
	include "StarEngine/vendor/tracy"
	include "StarEngine/vendor/NFD-Extended"
group ""

group "Dependencies - Mono"
	include "StarEngine/vendor/Coral/Coral.Managed"
	include "StarEngine/vendor/Coral/Coral.Native"
group ""

group "Dependencies - Text"
		include "StarEngine/vendor/msdf-atlas-gen"
group ""

group "Core"
	include "StarEngine"
	include "StarEngine-ScriptCore"
group ""

group "Tools"
	include "StarEditor"
group ""

group "Misc"
	include "Sandbox"
group ""
