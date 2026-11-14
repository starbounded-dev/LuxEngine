-- Lux-Watchdog: a small, dependency-free process that watches the editor from outside and writes a
-- crash report (dump, logs, report.txt) when it crashes, hangs or ends unexpectedly. Windows only.
-- Built into the editor's output folder, which is where CrashWatchdog::Start looks for it.
project "Lux-Watchdog"
	kind "WindowedApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"

	targetdir ("../bin/" .. outputdir .. "/Editor")
	objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

	files { "Source/**.cpp", "../Core/Source/Lux/Debug/CrashWatchdogProtocol.h" }
	includedirs { "../Core/Source" }

	links { "dbghelp", "psapi", "shell32", "user32" }
	defines { "UNICODE", "_UNICODE" }
	entrypoint "wWinMainCRTStartup"

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug or configurations:Debug-AS"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"
		symbols "on"

	filter "configurations:Dist"
		runtime "Release"
		optimize "on"
		symbols "off"
