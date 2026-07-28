-- ScriptCore is a premake-generated C# project built inside Lux.sln, mirroring Hazel-ScriptCore.
-- It links Coral.Managed (the C# host assembly) and lands in Editor/Resources/Scripts, where
-- ScriptEngine loads the core assembly from.
-- On Linux, skip this project for the gmake actions only: premake's C# gmake generator shells
-- out to `csc`, which the .NET SDK does not put on PATH. The vs2022 action is still wanted
-- there - it emits an SDK-style .csproj that `dotnet build` consumes on any platform, which is
-- how scripts/Linux-Build.sh builds ScriptCore.
if os.host() == "linux" and _ACTION ~= nil and string.match(_ACTION, "^gmake") then
	return
end

project "ScriptCore"
	kind "SharedLib"
	language "C#"
	dotnetframework "net9.0"
	clr "Unsafe"

	targetdir ("../Editor/Resources/Scripts")
	objdir ("../Editor/Resources/Scripts/Intermediates")

	links { "Coral.Managed" }

	-- EnableDynamicLoading emits ScriptCore.deps.json so the assembly is loadable into a Coral
	-- AssemblyLoadContext. AppendTargetFrameworkToOutputPath keeps ScriptCore.dll directly in
	-- the target dir (no net9.0/ subfolder).
	vsprops {
		AppendTargetFrameworkToOutputPath = "false",
		Nullable = "enable",
		EnableDynamicLoading = "true",
	}

	files {
		"Source/**.cs"
	}
