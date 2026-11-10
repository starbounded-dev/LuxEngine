project "Editor"
    kind "ConsoleApp"

    debuggertype "NativeWithManagedCore"

	targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	links { "Core" }

	-- The crash watchdog is built next to Editor.exe, where CrashWatchdog::Start looks for it.
	if os.target() == "windows" then
		dependson { "Lux-Watchdog" }
	end

	defines { "GLM_FORCE_DEPTH_ZERO_TO_ONE", }

	if _OPTIONS["discord"] then
		defines { "LUX_ENABLE_DISCORD" }
	end

	files  { 
		"Source/**.h",
		"Source/**.c",
		"Source/**.hpp",
		"Source/**.cpp",
		
		-- Shaders
		"Resources/Shaders/**.glsl",
		"Resources/Shaders/**.glslh",
		"Resources/Shaders/**.hlsl",
		"Resources/Shaders/**.hlslh",
		"Resources/Shaders/**.slh",
	}

	includedirs  {
		"Source/",

		"../Core/Source/",
		"../Core/vendor/"
	}

	filter "system:windows"
		-- Incremental build directories can retain the previous dynamically loaded backend.
		postbuildcommands { '{DELETE} "%{cfg.targetdir}/vaudionative.dll"' }
		systemversion "latest"
		defines { "LUX_PLATFORM_WINDOWS" }
		files {
			"Editor.rc",
			"resource.h"
		}

		-- We link the release SDK in every configuration, so copy that DLL everywhere too.
		-- discord_krisp.dll is deliberately not copied: it's only needed for voice chat.
		if _OPTIONS["discord"] then
			postbuildcommands {
				'{COPY} "../Core/vendor/discord_social_sdk/bin/release/discord_partner_sdk.dll" "%{cfg.targetdir}"',
			}
		end


		-- Coral links nethost.lib on Windows (never statically), so the exe cannot start without
		-- nethost.dll beside it. Take it from the .NET SDK pack Coral.Native found.
		postbuildcommands {
			'{COPY} "' .. _G["CORAL_NETHOST_BASE_PATH"] .. 'nethost.dll" "%{cfg.targetdir}"',
		}

		-- The shader compiler links shaderc_shared and dxcompiler from the Vulkan SDK, so copy them
		-- beside the exe as well: a machine without the SDK's Bin on PATH (a user's PC, the
		-- self-hosted CI runner) otherwise fails to start with "DLL not found" (0xC0000135).
		-- Runtime export ships every DLL in this folder.
		postbuildcommands {
			'{COPY} "' .. os.getenv("VULKAN_SDK") .. '/Bin/dxcompiler.dll" "%{cfg.targetdir}"',
		}

		-- vulkan-1.dll comes with the GPU driver, not the SDK. Delay-load it so the exe still starts
		-- for --version on a machine without one (the CI runner VM); EntryPoint.h reports a missing
		-- loader before the engine needs it. xinput1_4.dll (gamepad rumble) is absent on Windows Server;
		-- it is only called once an Xbox pad is connected.
		linkoptions { "/DELAYLOAD:vulkan-1.dll", "/DELAYLOAD:xinput1_4.dll" }
		links { "delayimp" }

	filter { "system:windows", "configurations:Debug or configurations:Debug-AS" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Debug/assimp-vc143-mtd.dll" "%{cfg.targetdir}"',
			'{COPY} "' .. os.getenv("VULKAN_SDK") .. '/Bin/shaderc_sharedd.dll" "%{cfg.targetdir}"',
		}

	filter { "system:windows", "configurations:Release or configurations:Dist" }
		postbuildcommands {
			'{COPY} "../Core/vendor/assimp/bin/windows/Release/assimp-vc143-mt.dll" "%{cfg.targetdir}"',
			'{COPY} "' .. os.getenv("VULKAN_SDK") .. '/Bin/shaderc_shared.dll" "%{cfg.targetdir}"',
		}

	-- Nsight Aftermath is loaded at runtime when present (Platform/Vulkan/Debug/Aftermath.cpp), in
	-- every config but Dist, so ship it beside the executable (Linux: via the $ORIGIN/lib rpath).
	if not _OPTIONS["no-aftermath"] then
		filter { "system:windows", "configurations:not Dist" }
			postbuildcommands {
				'{COPY} "../Core/vendor/NvidiaAftermath/lib/x64/windows/GFSDK_Aftermath_Lib.x64.dll" "%{cfg.targetdir}"',
				'{COPY} "../Core/vendor/NvidiaAftermath/lib/x64/windows/llvm_7_0_1.dll" "%{cfg.targetdir}"',
			}

		filter { "system:linux", "configurations:not Dist" }
			postbuildcommands {
				'{MKDIR} "%{cfg.targetdir}/lib"',
				'{COPYFILE} "../Core/vendor/NvidiaAftermath/lib/x64/linux/libGFSDK_Aftermath_Lib.x64.so" "%{cfg.targetdir}/lib/libGFSDK_Aftermath_Lib.x64.so"',
			}
	end

	filter "system:linux"
		postbuildcommands { '{DELETE} "%{cfg.targetdir}/lib/libvaudionative.so"' }
		defines { "LUX_PLATFORM_LINUX", "__EMULATE_UUID", "BACKWARD_HAS_DW", "BACKWARD_HAS_LIBUNWIND" }
		links { "dw", "dl", "unwind", "pthread", "X11" }
		-- --no-as-needed forces -latomic to stay linked even though, at LTO's initial
		-- as-needed scan, nothing yet references it; the __atomic_* calls for non-lock-free
		-- atomics (e.g. RenderCommandBuffer's std::atomic<PipelineStatistics>) are only
		-- inserted during LTO's deferred codegen, after Ubuntu ld's default --as-needed
		-- would otherwise have already dropped it.
		linkoptions { "-Wl,--start-group", "-Wl,-rpath,'$$ORIGIN/lib'", "-Wl,--no-as-needed,-latomic,--as-needed" }


		-- Link nethost for Coral .NET hosting
		if os.host() == "linux" then
			LinkNethost()
		end

		-- os.outputof runs at parse time on every host; pkg-config only exists on Linux.
		if os.host() == "linux" then
			local gtklibs, _ = os.outputof("pkg-config --libs gtk+-3.0")
			linkoptions { gtklibs }
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
		defines { "LUX_RELEASE", }

		ProcessDependencies("Release")

	filter "configurations:Debug or configurations:Debug-AS or configurations:Release"
		defines {
			"LUX_TRACK_MEMORY",
			
            "JPH_DEBUG_RENDERER",
            "JPH_FLOATING_POINT_EXCEPTIONS_ENABLED",
            "JPH_EXTERNAL_PROFILE"
		}

	filter "files:**.hlsl"
		flags {"ExcludeFromBuild"}

	filter "configurations:Dist"
		kind "WindowedApp"
		optimize "Full"
		symbols "Off"
		-- Core's Dist ISA: one LTO program must be built for one target, or headers shared with
		-- Core compile differently on each side (-Wodr at the Dist link).
		vectorextensions "AVX2"
		isaextensions { "BMI", "POPCNT", "LZCNT", "F16C" }
		defines { "LUX_DIST" }

		ProcessDependencies("Dist")

		flags { "ExcludeFromBuild" }
