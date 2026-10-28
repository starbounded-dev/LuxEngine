include "./vendor/premake_customization/ordered_pairs.lua"

-- One SDK root drives headers, link inputs, validation and deployed libraries.
FMODSDKRoot = os.getenv("LUX_FMOD_SDK")
if not FMODSDKRoot then
	local candidates = os.matchdirs("Core/vendor/FMOD/*")
	table.sort(candidates)
	local matches = {}
	for _, candidate in ipairs(candidates) do
		local library = os.target() == "windows" and "/api/core/lib/x64/fmod_vc.lib" or "/api/core/lib/x86_64/libfmod.so"
		if os.isfile(candidate .. "/api/core/inc/fmod.hpp") and os.isfile(candidate .. library) then
			table.insert(matches, candidate)
		end
	end
	if #matches > 1 then
		error("Multiple FMOD SDKs found; set LUX_FMOD_SDK to the package root containing api/.")
	end
	FMODSDKRoot = matches[1] or (os.target() == "windows" and "Core/vendor/FMOD/FMOD Studio API Windows" or "Core/vendor/FMOD/fmodstudioapi20314linux")
end
FMODSDKRoot = path.getabsolute(FMODSDKRoot)
AsterwaveRoot = path.getabsolute(os.getenv("LUX_ASTERWAVE_ROOT") or "Core/vendor/Asterwave")
local manifestFile = assert(io.open(path.join(_SCRIPT_DIR, "scripts/dependencies/Asterwave.json"), "r"))
AsterwaveManifest = json.decode(manifestFile:read("*a"))
manifestFile:close()

local function QuoteGitPath(value)
	if os.host() == "windows" then
		-- cmd expands percent variables even inside quotes; reject them instead of validating
		-- a different directory. Windows filenames cannot contain a double quote.
		if value:find('["%%\r\n]') then
			error("Unsupported characters in Asterwave checkout path: " .. value)
		end
		return '"' .. value .. '"'
	end
	return "'" .. value:gsub("'", "'\\''") .. "'"
end

local function ReadAsterwaveGit(arguments)
	local output, exitCode = os.outputof("git -C " .. QuoteGitPath(AsterwaveRoot) .. " " .. arguments)
	if exitCode ~= 0 then
		error("Cannot validate Asterwave checkout with Git: " .. AsterwaveRoot .. " (" .. arguments .. ")")
	end
	return (output or ""):gsub("%s+$", "")
end

function ValidateAsterwaveSource()
	for _, collection in ipairs({ AsterwaveManifest.sources, AsterwaveManifest.required_files }) do
		for _, file in ipairs(collection) do
			if not os.isfile(AsterwaveRoot .. "/" .. file) then
				error("Required Asterwave source file missing: " .. AsterwaveRoot .. "/" .. file .. ". Run python scripts/FetchAsterwave.py or set LUX_ASTERWAVE_ROOT.")
			end
		end
	end
	local root = path.getabsolute(ReadAsterwaveGit("rev-parse --show-toplevel"))
	local expectedRoot = AsterwaveRoot
	if os.host() == "windows" then
		root, expectedRoot = root:lower(), expectedRoot:lower()
	end
	if root ~= expectedRoot then
		error("Asterwave must be its own Git checkout: " .. AsterwaveRoot)
	end
	local revision = ReadAsterwaveGit("rev-parse HEAD")
	if revision ~= AsterwaveManifest.revision then
		error("Asterwave revision mismatch: expected " .. AsterwaveManifest.revision .. ", found " .. revision .. ". Run python scripts/FetchAsterwave.py.")
	end
	if ReadAsterwaveGit("status --porcelain --untracked-files=all"):find("%S") then
		error("Asterwave checkout has local changes: " .. AsterwaveRoot .. ". Preserve them separately before using the pinned dependency.")
	end
end

function ValidateAudioSDK()
	local files = {
		FMODSDKRoot .. "/api/core/inc/fmod.hpp",
		FMODSDKRoot .. "/api/studio/inc/fmod_studio.hpp"
	}
	local libraries = os.target() == "windows" and {
		"core/lib/x64/fmod_vc.lib", "core/lib/x64/fmod.dll",
		"studio/lib/x64/fmodstudio_vc.lib", "studio/lib/x64/fmodstudio.dll"
	} or {
		"core/lib/x86_64/libfmod.so", "core/lib/x86_64/libfmod.so.14",
		"studio/lib/x86_64/libfmodstudio.so", "studio/lib/x86_64/libfmodstudio.so.14"
	}
	for _, library in ipairs(libraries) do
		table.insert(files, FMODSDKRoot .. "/api/" .. library)
	end
	for _, file in ipairs(files) do
		if not os.isfile(file) then
			error("Required audio SDK file missing: " .. file .. ". Set LUX_FMOD_SDK to a complete SDK package root.")
		end
	end
	if _OPTIONS["raytraced-audio"] then
		ValidateAsterwaveSource()
	end
end

-- Utility function for converting the first character to uppercase
function firstToUpper(str)
	return (str:gsub("^%l", string.upper))
end

-- Grab Vulkan SDK path (fall back to the repo-bundled SDK so premake generation
-- works without the environment variable that Linux-Run.sh sets at runtime).
VULKAN_SDK = os.getenv("VULKAN_SDK") or path.getabsolute("Core/vendor/VulkanSDK/x86_64")

--[[
	If you're adding a new dependency all you have to do to get it linking
	and included is define it properly in the table below, here's some example usage:

	MyDepName = {
		LibName = "my_dep_name",
		LibDir = "some_path_to_dependency_lib_dir",
		IncludeDir = "my_include_dir",
		Windows = { DebugLibName = "my_dep_name_debug" },
		Configurations = "Debug,Release"
	}

	MyDepName - This is just for organizational purposes, it doesn't actually matter for the build process
	LibName - This is the name of the .lib file that you want e.g Hazelnut to link against (you shouldn't include the .lib extension since Linux uses .a)
	LibDir - Indicates which directory the lib file is located in, this can include "%{cfg.buildcfg}" if you have a dedicated Debug / Release directory
	IncludeDir - Pretty self explanatory, the filepath that will be included in externalincludedirs
	Windows - This defines a platform-specific scope for this dependency, anything defined in that scope will only apply for Windows, you can also add one for Linux
	DebugLibName - Use this if the .lib file has a different name in Debug builds (e.g "shaderc_sharedd" vs "shaderc_shared")
	Configurations - Defines a list of configurations that this dependency should be used in, if no Configurations is specified all configs will link and include this dependency (which is what we want in most cases)

	Most of the properties I've listed can be used in either the "global" dependency scope OR in a specific platform scope,
	the only property that doesn't support that is "Configurations" which HAS to be defined in the global scope.

	Of course you can put only SOME properties in a platform scope, and others in the global scope if you want to.

	Naturally I suggest taking a look at existing dependency definitions when adding a new dependency.

	Remember that in most cases you only need to update this list, no need to manually add dependencies to the "links" list or "includedirs" list

	HEADER-ONLY LIBRARIES: If your dependency is header-only you shouldn't specify e.g LibName, just add IncludeDir and it'll be treated like a header-only library

]]--

Dependencies = {
	Vulkan = {
		Windows = {
			LibName = "vulkan-1",
			IncludeDir = "%{VULKAN_SDK}/Include/",
			LibDir = "%{VULKAN_SDK}/Lib/",
		},
		Linux =  {
			LibName = "vulkan",
			IncludeDir = "%{VULKAN_SDK}/include/",
			LibDir = "%{VULKAN_SDK}/lib/",
		},
	},
	DirectXCompiler = {
		-- Linux compiles HLSL by shelling out to the `dxc` binary (see VulkanShaderCompiler.cpp),
		-- so libdxcompiler is only linked on Windows.
		Windows = { LibName = "dxcompiler" },
	},
	TBB = {
		Linux = { LibName = "tbb" },
	},
	WinSock = {
		Windows = { LibName = "Ws2_32" },
	},
	WinMM = {
		Windows = { LibName = "Winmm" },
	},
	WinVersion = {
		Windows = { LibName = "Version" },
	},
	Bcrypt = {
		Windows = { LibName = "Bcrypt" },
	},
	-- Headers only: Aftermath.cpp loads the library at runtime, and Editor/Lux-Runtime copy it
	-- beside the executable. Dropped with "--no-aftermath"; the code is gated on LUX_DISABLE_AFTERMATH.
	NvidiaAftermath = (not _OPTIONS["no-aftermath"]) and {
		IncludeDir = "%{wks.location}/Core/vendor/NvidiaAftermath/include",
	} or nil,
	ShaderC = {
		LibName = "shaderc_shared",
		Windows = { DebugLibName = "shaderc_sharedd", },
		IncludeDir = "%{wks.location}/Core/vendor/shaderc/include",
		Configurations = "Debug,Release"
	},
	ShaderCUtil = {
		LibName = "shaderc_util",
		Windows = { DebugLibName = "shaderc_utild", },
		IncludeDir = "%{wks.location}/Core/vendor/shaderc/libshaderc_util/include",
		Configurations = "Debug,Release"
	},
	SPIRVCrossCore = {
		LibName = "spirv-cross-core",
		Windows = { DebugLibName = "spirv-cross-cored", },
		Configurations = "Debug,Release"
	},
	SPIRVCrossGLSL = {
		LibName = "spirv-cross-glsl",
		Windows = { DebugLibName = "spirv-cross-glsld", },
		Configurations = "Debug,Release"
	},
	SPIRVTools = {
		LibName = "SPIRV-Tools",
		Windows = { DebugLibName = "SPIRV-Toolsd", },
		Configurations = "Debug,Release"
	},
	GLFW = {
		-- No need to specify LibDir for GLFW since it's automatically handled by premake
		LibName = "GLFW",
		IncludeDir = "%{wks.location}/Core/vendor/GLFW/include",
	},
	Coral = {
		LibName = "Coral.Native",
		IncludeDir = "%{wks.location}/Core/vendor/Coral/Coral.Native/Include",
	},
	FastNoise = {
		IncludeDir = "%{wks.location}/Core/vendor/FastNoise",
	},
	Assimp = {
		IncludeDir = "%{wks.location}/Core/vendor/assimp/include",
		Windows = { LibName = "assimp-vc143-mt", DebugLibName = "assimp-vc143-mtd", LibDir = "%{wks.location}/Core/vendor/assimp/bin/windows/%{cfg.buildcfg}/" },
		Linux = { LibName = "assimp", LibDir = "%{wks.location}/Core/vendor/assimp/bin/linux/" },
		Configurations = "Debug,Release"
	},
	-- Opt-in, enabled with the "--discord" premake option. The SDK ships both debug and release
	-- binaries, but we link release in every configuration: it's a C ABI boundary behind a DLL,
	-- so there's no CRT mismatch, and the debug build is only useful for debugging the SDK itself.
	-- See Core/vendor/discord_social_sdk/DISCORD_SDK_SETUP.md.
	DiscordSocial = _OPTIONS["discord"] and {
		IncludeDir = "%{wks.location}/Core/vendor/discord_social_sdk/include",
		Windows = { LibName = "discord_partner_sdk", LibDir = "%{wks.location}/Core/vendor/discord_social_sdk/lib/release/" },
	} or nil,
	-- Opt-in with "--raytraced-audio". Built from the pinned private C++ source by the Asterwave
	-- Premake project and linked statically, so every exported game of that build contains it.
	Asterwave = _OPTIONS["raytraced-audio"] and {
		LibName = "Asterwave",
		IncludeDir = AsterwaveRoot .. "/include",
	} or nil,
	-- Required audio backend. Two entries because FMOD ships the Studio
	-- API as a separate library layered on the Core one: Studio owns the events and banks the game
	-- actually plays, and creates a Core system internally for the low-level work (3D listener,
	-- reverb, CPU stats). Both must be linked - Studio alone does not resolve.
	--
	-- We link the release build in every configuration: DebugLibName below is only honoured on
	-- Windows (see ProcessDependencies). FMOD allocator statistics work with release libraries;
	-- detailed Studio allocation attribution requires the optional logging build.
	--
	-- Package roots are discovered or explicitly configured above. Generation verifies every
	-- header, import library and deployed DLL/so for the target platform.
	FMOD = {
		Windows = { LibName = "fmod_vc", IncludeDir = FMODSDKRoot .. "/api/core/inc", LibDir = FMODSDKRoot .. "/api/core/lib/x64/" },
		Linux = { LibName = "fmod", IncludeDir = FMODSDKRoot .. "/api/core/inc", LibDir = FMODSDKRoot .. "/api/core/lib/x86_64/" },
	},
	FMODStudio = {
		Windows = { LibName = "fmodstudio_vc", IncludeDir = FMODSDKRoot .. "/api/studio/inc", LibDir = FMODSDKRoot .. "/api/studio/lib/x64/" },
		Linux = { LibName = "fmodstudio", IncludeDir = FMODSDKRoot .. "/api/studio/inc", LibDir = FMODSDKRoot .. "/api/studio/lib/x86_64/" },
	},
	ACL = {
		IncludeDir = "%{wks.location}/Core/vendor/acl/include"
	},
	RTM = {
		IncludeDir = "%{wks.location}/Core/vendor/rtm/include"
	},
	Choc = {
		IncludeDir = "%{wks.location}/Core/vendor/choc",
	},
	MagicEnum = {
		IncludeDir = "%{wks.location}/Core/vendor/magic_enum/include",
	},
	NFDExtended = {
		LibName = "NFD-Extended",
		IncludeDir = "%{wks.location}/Core/vendor/NFD-Extended/NFD-Extended/src/include"
	},
	GLM = {
		IncludeDir = "%{wks.location}/Core/vendor/glm",
	},
	EnTT = {
		IncludeDir = "%{wks.location}/Core/vendor/entt/include",
	},
	ImGuizmo = {
		IncludeDir = "%{wks.location}/Core/vendor/imguizmo",
	},
	STB = {
		IncludeDir = "%{wks.location}/Core/vendor/stb/include",
	},
	ImGui = {
		LibName = "ImGui",
		IncludeDir = "%{wks.location}/Core/vendor/imgui",
	},
	NRI = {
		-- Dependents before dependencies: GNU ld resolves static archives left to right.
		LibName = { "NRI", "NRI-VK", "NRI-Validation", "NRI-NONE", "NRI-Shared" },
		IncludeDir = "%{wks.location}/Core/vendor/NRI/Include"
	},
	Box2D = {
		LibName = "Box2D",
		IncludeDir = "%{wks.location}/Core/vendor/Box2D/include",
	},
	JoltPhysics = {
		LibName = "JoltPhysics",
		IncludeDir = "%{wks.location}/Core/vendor/JoltPhysics/JoltPhysics",
	},
	Tracy = {
		LibName = "Tracy",
		IncludeDir = "%{wks.location}/Core/vendor/tracy/tracy/public",
	},
	MSDFAtlasGen = {
		LibName = "msdf-atlas-gen",
		IncludeDir = "%{wks.location}/Core/vendor/msdf-atlas-gen/msdf-atlas-gen",
	},
	MSDFGen = {
		LibName = "msdfgen",
		IncludeDir = "%{wks.location}/Core/vendor/msdf-atlas-gen/msdfgen",
	},
	Freetype = {
		LibName = "freetype"
	},
	RapidYAML = {
		IncludeDir = "%{wks.location}/Core/vendor/rapidyaml",
	},
	SPDLog = {
		IncludeDir = "%{wks.location}/Core/vendor/spdlog/include",
	},
	WS2 = {
		Windows = { LibName = "ws2_32", },
	},
	Dbghelp = {
		Windows = { LibName = "Dbghelp" },
	},
}


-- NOTE(Peter): Probably don't touch these functions unless you know what you're doing (or just ask me if you need help extending them)

function LinkDependency(table, is_debug, target)

	-- Setup library directory
	if table.LibDir ~= nil then
		libdirs { table.LibDir }
	end

	-- Try linking
	local libraryName = nil
	if table.LibName ~= nil then
		libraryName = table.LibName
	end

	if table.DebugLibName ~= nil and is_debug and target == "Windows" then
		libraryName = table.DebugLibName
	end

	if libraryName ~= nil then
		links { libraryName }
		return true
	end

	return false
end

function AddDependencyIncludes(table)
	if table.IncludeDir ~= nil then
		externalincludedirs { table.IncludeDir }
	end
end

function ProcessDependencies(config_name)
	local target = firstToUpper(os.target())

	for key, libraryData in orderedPairs(Dependencies) do

		-- Always match config_name if no Configurations list is specified
		local matchesConfiguration = true

		if config_name ~= nil and libraryData.Configurations ~= nil then
			matchesConfiguration = string.find(libraryData.Configurations, config_name)
		end

		local isDebug = config_name == "Debug"

		if matchesConfiguration then
			local continueLink = true

			-- Process Platform Scope
			if libraryData[target] ~= nil then
				continueLink = not LinkDependency(libraryData[target], isDebug, target)
				AddDependencyIncludes(libraryData[target])
			end

			-- Process Global Scope
			if continueLink then
				LinkDependency(libraryData, isDebug, target)
			end

			AddDependencyIncludes(libraryData)
		end

	end
end

function IncludeDependencies(config_name)
	local target = firstToUpper(os.target())

	for key, libraryData in orderedPairs(Dependencies) do

		-- Always match config_name if no Configurations list is specified
		local matchesConfiguration = true

		if config_name ~= nil and libraryData.Configurations ~= nil then
			matchesConfiguration = string.find(libraryData.Configurations, config_name)
		end

		if matchesConfiguration then
			-- Process Global Scope
			AddDependencyIncludes(libraryData)

			-- Process Platform Scope
			if libraryData[target] ~= nil then
				AddDependencyIncludes(libraryData[target])
			end
		end

	end
end
