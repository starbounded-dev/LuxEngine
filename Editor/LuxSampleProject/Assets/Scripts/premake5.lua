local LuxEngineRootDir = '../../../..'
include (LuxEngineRootDir .. "/vendor/premake_customization/solution_items.lua")

workspace "LuxSample"
	startproject "LuxSample"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "LuxSample"
	kind "SharedLib"
	language "C#"
	dotnetframework "4.7.2"

	targetdir ("Binaries")
	objdir ("Intermediates")

	files 
	{
		"Source/**.cs",
		"Properties/**.cs"
	}

	libdirs
	{
		LuxEngineRootDir .. "/Editor/Resources/Scripts"
	}

	links
	{
		"ScriptCore"
	}
	
	filter "configurations:Debug"
		optimize "Off"
		symbols "Default"

	filter "configurations:Release"
		optimize "On"
		symbols "Default"

	filter "configurations:Dist"
		optimize "Full"
		symbols "Off"

group "Core"
	include (LuxEngineRootDir .. "/ScriptCore")
group ""
