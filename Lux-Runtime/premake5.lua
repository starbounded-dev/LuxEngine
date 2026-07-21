local gtkLinkOptions = nil
if os.host() == "linux" then
	local result, err = os.outputof("pkg-config --libs gtk+-3.0")
	if result == nil or result:gsub("%s+", "") == "" then
		error("pkg-config --libs gtk+-3.0 failed for Lux-Runtime: " .. tostring(err))
	end
	gtkLinkOptions = result
end

project "Lux-Runtime"
	kind "ConsoleApp"
	targetname "Lux-Runtime"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")
	debugdir "%{cfg.targetdir}"

	links { "Core" }

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE" }

	if _OPTIONS["discord"] then
		defines { "LUX_ENABLE_DISCORD" }
	end

	files {
		"src/**.h",
		"src/**.c",
		"src/**.hpp",
		"src/**.cpp"
	}

	includedirs {
		"src/",
		"../Core/Source",
		"../Core/vendor"
	}

	filter "system:windows"
		systemversion "latest"
		defines { "LUX_PLATFORM_WINDOWS" }
		files {
			"Lux-Runtime.rc",
			"resource.h"
		}

		-- We link the release SDK in every configuration, so copy that DLL everywhere too.
		-- discord_krisp.dll is deliberately not copied: it's only needed for voice chat.
		if _OPTIONS["discord"] then
			postbuildcommands {
				'{COPY} "../Core/vendor/discord_social_sdk/bin/release/discord_partner_sdk.dll" "%{cfg.targetdir}"',
			}
		end

	filter { "system:windows", "configurations:Debug or configurations:Debug-AS" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.dll" "%{cfg.targetdir}"',
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/mono" "%{cfg.targetdir}/mono"',
		}

	filter { "system:windows", "configurations:Release or configurations:Dist" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Release/assimp-vc143-mt.dll" "%{cfg.targetdir}"',
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/mono" "%{cfg.targetdir}/mono"',
		}

	filter "system:linux"
		defines { "LUX_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread" }
		if gtkLinkOptions then
			linkoptions { gtkLinkOptions }
		end

	filter "configurations:Debug or configurations:Debug-AS"
		symbols "On"
		defines { "LUX_DEBUG" }

		ProcessDependencies("Debug")

	filter { "system:windows", "configurations:Debug-AS" }
		sanitize { "Address" }
		flags { "NoRuntimeChecks", "NoIncrementalLink" }

	filter "configurations:Release"
		optimize "On"
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_RELEASE" }

		ProcessDependencies("Release")

	filter "configurations:Debug or configurations:Debug-AS or configurations:Release"
		defines {
			"LUX_TRACK_MEMORY",

			"JPH_DEBUG_RENDERER",
			"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
			"JPH_EXTERNAL_PROFILE"
		}

	filter "configurations:Dist"
		kind "WindowedApp"
		optimize "Full"
		symbols "Off"
		defines { "LUX_DIST" }

		ProcessDependencies("Dist")
