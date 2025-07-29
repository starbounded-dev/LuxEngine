include "./vendor/premake/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "StarEngine"
	startproject "StarEditor"
	architecture "x86_64"
	conformancemode "On"
	cppdialect "C++20"
	staticruntime "off"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

	filter "language:C++ or language:C"
		architecture "x86_64"

	filter "files:**.c"
		flags {"NoPCH"}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "vendor/premake"
	include "StarEngine/vendor/Box2D"
	include "StarEngine/vendor/GLFW"
	include "StarEngine/vendor/GLAD"
	include "StarEngine/vendor/imgui"
	include "StarEngine/vendor/yaml-cpp"
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
