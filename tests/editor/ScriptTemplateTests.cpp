// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "ScriptTemplate.h"
#include <iostream>
#include <stdexcept>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main(int argc, char** argv)
{
	try
	{
		Check(argc == 3, "Expected template and private test output paths");
		const std::filesystem::path directory = argv[2];
		std::filesystem::create_directories(directory);
		std::string text, source, error;
		Check(Lux::ScriptTemplate::Load(argv[1], text, error), "Could not read production template");
		for (const char* name : { "PlayerController", "GameController", "Entity", "Vector3", "_Actor2" })
		{
			Check(Lux::ScriptTemplate::Render(text, name, "TemplateTests.Game", source, error), "Valid name rejected");
			Check(source.find("$CLASS_NAME$") == std::string::npos && source.find("$NAMESPACE_NAME$") == std::string::npos, "Unreplaced tokens");
			Check(source.find(std::string("class ") + name + " : global::Lux.Entity") != std::string::npos, "Class substitution or base type incorrect");
			Check(source.find("namespace TemplateTests.Game") != std::string::npos, "Namespace substitution incorrect");
			std::ofstream file(directory / (std::string(name) + ".cs"));
			file << source;
			file.close();
			Check(static_cast<bool>(file), "Could not write generated fixture");
		}
		for (const char* name : { "", "class", "return", "1Player", "Bad Name", "../Player", "Player.cs", "CON", "nul", "COM1", "Lpt9", "OnCreate", "OnUpdate", "MoveSpeed" })
		{
			Check(!Lux::ScriptTemplate::Render(text, name, "MyGame", source, error), "Invalid class accepted");
			Check(source.empty() && !error.empty(), "Failure kept stale generated source or no diagnostic");
		}
		for (const char* nameSpace : { "", "Game.", ".Game", "Game..Logic", "Game.class", "Bad Namespace" })
			Check(!Lux::ScriptTemplate::Render(text, "Player", nameSpace, source, error), "Invalid namespace accepted");
		Check(!Lux::ScriptTemplate::Render("class Example {}", "Player", "Game", source, error), "Missing placeholders accepted");
		Check(Lux::ScriptTemplate::Render("$CLASS_NAME$ $CLASS_NAME$ $NAMESPACE_NAME$", "Player", "Game", source, error), "Repeated placeholder rendering failed");
		Check(source == "Player Player Game", "Not all placeholders replaced");
		Check(!Lux::ScriptTemplate::Load(directory / "missing.template", text, error) && text.empty() && !error.empty(), "Missing template accepted");
		std::cout << "PASS: real starter generation, dotted namespaces, repeated tokens, invalid names and missing templates\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL: " << exception.what() << '\n';
		return 1;
	}
}
