-- ScriptCore is a premake-generated C# project built inside Lux.sln, mirroring Hazel-ScriptCore.
-- It links Coral.Managed (the C# host assembly) and lands in Editor/Resources/Scripts, where
-- ScriptEngine loads the core assembly from.
-- On Linux, ScriptCore is built via `dotnet build` in scripts/Linux-Build.sh instead of gmake.
if os.host() == "linux" then return end

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
