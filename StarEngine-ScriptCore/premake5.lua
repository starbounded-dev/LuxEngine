StarRootDirectory = os.getenv("STAR_DIR")

project "StarEngine-ScriptCore"
	kind "SharedLib"
	language "C#"
	dotnetframework "net4.7.2"
	clr "Unsafe"

	targetdir "%{StarRootDirectory}/StarEditor/Resources/Scripts"
	objdir "%{StarRootDirectory}/StarEditor/Resources/Scripts/Intermediates"

	files {
		"Source/**.cs",
		"Properties/**.cs"
	}