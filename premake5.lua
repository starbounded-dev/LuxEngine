include "./vendor/premake/premake_customization/solution_items.lua"
include "Dependencies.lua"

workspace "Lux"
	architecture "x86_64"
	startproject "Editor"

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

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "vendor/premake"
	include "Core/vendor/Box2D"
	include "Core/vendor/GLFW"
	include "Core/vendor/GLAD"
	include "Core/vendor/imgui"
	include "Core/vendor/yaml-cpp"
	include "Core/vendor/tracy"
	include "Core/vendor/nvrhi"
	include "Core/vendor/SPIRV-Cross"
group ""

group "Dependencies - Text"
		include "Core/vendor/msdf-atlas-gen"
group ""

group "Core"
	include "Core"
	include "Lux-ScriptCore"
group ""

group "Tools"
	include "Editor"
group ""

group "Misc"
	include "Sandbox"
group ""
