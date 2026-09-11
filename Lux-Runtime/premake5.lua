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

		do -- Vercidium Audio is required.
			postbuildcommands {
				'{COPY} "../Core/vendor/VA_RAY/3d/native/production/windows/vaudionative.dll" "%{cfg.targetdir}"',
			}
		end

		-- Placeholder path - the Windows FMOD package hasn't been added yet (see Dependencies.lua).
		do -- FMOD is required.
			postbuildcommands {
				'{COPY} "../Core/vendor/FMOD/FMOD Studio API Windows/api/core/lib/x64/fmod.dll" "%{cfg.targetdir}"',
				'{COPY} "../Core/vendor/FMOD/FMOD Studio API Windows/api/studio/lib/x64/fmodstudio.dll" "%{cfg.targetdir}"',
			}
		end

	filter { "system:windows", "configurations:Debug or configurations:Debug-AS" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.dll" "%{cfg.targetdir}"',
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/DotNet" "%{cfg.targetdir}/DotNet"',
		}

	filter { "system:windows", "configurations:Release or configurations:Dist" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Release/assimp-vc143-mt.dll" "%{cfg.targetdir}"',
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/DotNet" "%{cfg.targetdir}/DotNet"',
		}

	filter "system:linux"
		defines { "LUX_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread", "X11" }
		-- --no-as-needed forces -latomic to stay linked even though, at LTO's initial
		-- as-needed scan, nothing yet references it; the __atomic_* calls for non-lock-free
		-- atomics (e.g. RenderCommandBuffer's std::atomic<PipelineStatistics>) are only
		-- inserted during LTO's deferred codegen, after Ubuntu ld's default --as-needed
		-- would otherwise have already dropped it.
		linkoptions { "-Wl,--start-group", "-Wl,-rpath,'$$ORIGIN/lib'", "-Wl,--no-as-needed,-latomic,--as-needed" }

		-- vaudionative.so is resolved via the $ORIGIN/lib rpath above, not the default loader path.
		do -- Vercidium Audio is required.
			postbuildcommands {
				'{MKDIR} "%{cfg.targetdir}/lib"',
				'{COPY} "../Core/vendor/VA_RAY/3d/native/production/linux/libvaudionative.so" "%{cfg.targetdir}/lib"',
			}
		end

		-- See the Editor project for why {COPYFILE} + the exact SONAME, not a {COPY} glob.
		do -- FMOD is required.
			postbuildcommands {
				'{MKDIR} "%{cfg.targetdir}/lib"',
				'{COPYFILE} "../Core/vendor/FMOD/fmodstudioapi20314linux/api/core/lib/x86_64/libfmod.so.14" "%{cfg.targetdir}/lib/libfmod.so.14"',
				'{COPYFILE} "../Core/vendor/FMOD/fmodstudioapi20314linux/api/studio/lib/x86_64/libfmodstudio.so.14" "%{cfg.targetdir}/lib/libfmodstudio.so.14"',
			}
		end

		if gtkLinkOptions then
			linkoptions { gtkLinkOptions }
		end
		if os.host() == "linux" then
			LinkNethost()
		end

	filter { "system:linux", "configurations:Debug or configurations:Debug-AS" }
		postbuildcommands {
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/DotNet" "%{cfg.targetdir}/DotNet"',
		}

	filter { "system:linux", "configurations:Release or configurations:Dist" }
		postbuildcommands {
			'{COPYDIR} "../Editor/Resources" "%{cfg.targetdir}/Resources"',
			'{COPYDIR} "../Editor/DotNet" "%{cfg.targetdir}/DotNet"',
		}

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
