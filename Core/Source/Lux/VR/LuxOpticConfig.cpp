// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "LuxOpticConfig.h"

#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"

#include <fstream>

namespace Lux {

	namespace {

		constexpr const char* kFileName = "LuxOptic.yaml";
		constexpr const char* kRootKey = "LuxOptic";

		constexpr const char* LayoutToString(LuxOpticLayout layout)
		{
			return layout == LuxOpticLayout::TopBottom ? "TopBottom" : "SideBySide";
		}

		// Keys whose value was the wrong type (kept as before) or out of range (clamped).
		using ProblemList = std::vector<std::string_view>;

		template<typename T>
		void ReadValue(const Yaml::Node& parent, const char* key, T& value, ProblemList& problems)
		{
			const Yaml::Node node = parent[key];
			if (!node)
				return;

			T read{};
			if (Yaml::Convert<T>::Decode(node, read))
				value = read;
			else
				problems.push_back(key);
		}

		void ReadSize(const Yaml::Node& parent, const char* key, glm::uvec2& value, ProblemList& problems)
		{
			const Yaml::Node node = parent[key];
			if (!node)
				return;

			std::vector<uint32_t> read;
			if (Yaml::Convert<std::vector<uint32_t>>::Decode(node, read) && read.size() == 2)
				value = { read[0], read[1] };
			else
				problems.push_back(key);
		}

		template<typename T>
		void ClampValue(T& value, T minimum, T maximum, const char* key, ProblemList& problems)
		{
			const T clamped = glm::clamp(value, minimum, maximum);
			if (clamped == value)
				return;

			value = clamped;
			problems.push_back(key);
		}

	}

	std::filesystem::path LuxOpticConfig::GetDefaultPath()
	{
		return FileSystem::GetPersistentStoragePath() / kFileName;
	}

	bool LuxOpticConfig::Load(const std::filesystem::path& path)
	{
		if (!FileSystem::Exists(path))
			return false;

		Yaml::Node data;
		try
		{
			data = Yaml::LoadFile(path);
		}
		catch (const Yaml::Exception& e)
		{
			LUX_CORE_ERROR_TAG("VR", "Could not read '{}', keeping the current headset settings: {}", path.string(), e.what());
			return false;
		}

		const Yaml::Node root = data[kRootKey];
		if (!root)
		{
			LUX_CORE_ERROR_TAG("VR", "'{}' has no '{}' section, keeping the current headset settings.", path.string(), kRootKey);
			return false;
		}

		ProblemList problems;
		ReadValue(root, "IPD", IPD, problems);
		ReadValue(root, "FOV", FOV, problems);
		ReadSize(root, "EyeResolution", EyeResolution, problems);
		ReadValue(root, "RenderScale", RenderScale, problems);

		if (const Yaml::Node display = root["Display"])
		{
			ReadSize(display, "MatchResolution", DisplayMatchResolution, problems);
			ReadValue(display, "Index", DisplayIndex, problems);
			ReadValue(display, "RefreshRate", DisplayRefreshRate, problems);
		}

		if (const Yaml::Node layout = root["Layout"])
		{
			const std::string_view text = layout.Scalar();
			if (text == "SideBySide")
				Layout = LuxOpticLayout::SideBySide;
			else if (text == "TopBottom")
				Layout = LuxOpticLayout::TopBottom;
			else
				problems.push_back("Layout");
		}

		// Any angle reads, wrapped into [0, 360) (-90 is 270); one that is not a quarter turn snaps.
		int32_t eyeRotation = static_cast<int32_t>(EyeRotation);
		ReadValue(root, "EyeRotation", eyeRotation, problems);
		const int32_t wrappedRotation = ((eyeRotation % 360) + 360) % 360;
		const int32_t snappedRotation = (wrappedRotation + 45) / 90 % 4 * 90;
		if (snappedRotation != wrappedRotation)
			problems.push_back("EyeRotation");
		EyeRotation = static_cast<uint32_t>(snappedRotation);

		ReadValue(root, "EyeGapOffset", EyeGapOffset, problems);

		if (const Yaml::Node distortion = root["Distortion"])
		{
			ReadValue(distortion, "K1", DistortionK1, problems);
			ReadValue(distortion, "K2", DistortionK2, problems);
			ReadValue(distortion, "Zoom", DistortionZoom, problems);
		}

		if (const Yaml::Node chromatic = root["Chromatic"])
		{
			ReadValue(chromatic, "Enabled", ChromaticEnabled, problems);
			ReadValue(chromatic, "RedScale", ChromaticRedScale, problems);
			ReadValue(chromatic, "BlueScale", ChromaticBlueScale, problems);
		}

		ReadValue(root, "Near", Near, problems);
		ReadValue(root, "Far", Far, problems);

		// Wide enough for any plausible headset, narrow enough that a typo cannot make a view the
		// renderer cannot draw (zero-sized targets, an inverted depth range).
		ClampValue(IPD, 0.04f, 0.09f, "IPD", problems);
		ClampValue(FOV, 30.0f, 170.0f, "FOV", problems);
		ClampValue(EyeResolution.x, 64u, 8192u, "EyeResolution", problems);
		ClampValue(EyeResolution.y, 64u, 8192u, "EyeResolution", problems);
		ClampValue(RenderScale, 0.25f, 2.0f, "RenderScale", problems);
		ClampValue(DisplayIndex, -1, 63, "Display.Index", problems);
		ClampValue(DisplayRefreshRate, 24u, 500u, "Display.RefreshRate", problems);
		ClampValue(EyeGapOffset, -2048, 2048, "EyeGapOffset", problems);
		ClampValue(DistortionK1, -2.0f, 2.0f, "Distortion.K1", problems);
		ClampValue(DistortionK2, -2.0f, 2.0f, "Distortion.K2", problems);
		ClampValue(DistortionZoom, 0.1f, 4.0f, "Distortion.Zoom", problems);
		ClampValue(ChromaticRedScale, 0.9f, 1.1f, "Chromatic.RedScale", problems);
		ClampValue(ChromaticBlueScale, 0.9f, 1.1f, "Chromatic.BlueScale", problems);
		ClampValue(Near, 0.001f, 1.0f, "Near", problems);
		ClampValue(Far, Near * 10.0f, 1.0e6f, "Far", problems);

		if (!problems.empty())
		{
			std::string keys;
			for (size_t i = 0; i < problems.size(); i++)
			{
				// EyeResolution can be reported for each axis.
				if (std::find(problems.begin(), problems.begin() + i, problems[i]) != problems.begin() + i)
					continue;
				if (!keys.empty())
					keys += ", ";
				keys += problems[i];
			}
			LUX_CORE_WARN_TAG("VR", "'{}': ignored or clamped {} (unreadable or out of range).", path.string(), keys);
		}

		return true;
	}

	bool LuxOpticConfig::Save(const std::filesystem::path& path) const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap;
		out << Yaml::Key << kRootKey << Yaml::Value << Yaml::BeginMap;
		{
			out << Yaml::Key << "IPD" << Yaml::Value << IPD;
			out << Yaml::Key << "FOV" << Yaml::Value << FOV;
			out << Yaml::Key << "EyeResolution" << Yaml::Value << Yaml::Flow << Yaml::BeginSeq << EyeResolution.x << EyeResolution.y << Yaml::EndSeq;
			out << Yaml::Key << "RenderScale" << Yaml::Value << RenderScale;

			out << Yaml::Key << "Display" << Yaml::Value << Yaml::BeginMap;
			out << Yaml::Key << "MatchResolution" << Yaml::Value << Yaml::Flow << Yaml::BeginSeq << DisplayMatchResolution.x << DisplayMatchResolution.y << Yaml::EndSeq;
			out << Yaml::Key << "Index" << Yaml::Value << DisplayIndex;
			out << Yaml::Key << "RefreshRate" << Yaml::Value << DisplayRefreshRate;
			out << Yaml::EndMap;

			out << Yaml::Key << "Layout" << Yaml::Value << LayoutToString(Layout);
			out << Yaml::Key << "EyeRotation" << Yaml::Value << EyeRotation;
			out << Yaml::Key << "EyeGapOffset" << Yaml::Value << EyeGapOffset;

			out << Yaml::Key << "Distortion" << Yaml::Value << Yaml::BeginMap;
			out << Yaml::Key << "K1" << Yaml::Value << DistortionK1;
			out << Yaml::Key << "K2" << Yaml::Value << DistortionK2;
			out << Yaml::Key << "Zoom" << Yaml::Value << DistortionZoom;
			out << Yaml::EndMap;

			out << Yaml::Key << "Chromatic" << Yaml::Value << Yaml::BeginMap;
			out << Yaml::Key << "Enabled" << Yaml::Value << ChromaticEnabled;
			out << Yaml::Key << "RedScale" << Yaml::Value << ChromaticRedScale;
			out << Yaml::Key << "BlueScale" << Yaml::Value << ChromaticBlueScale;
			out << Yaml::EndMap;

			out << Yaml::Key << "Near" << Yaml::Value << Near;
			out << Yaml::Key << "Far" << Yaml::Value << Far;
		}
		out << Yaml::EndMap;
		out << Yaml::EndMap;

		if (!FileSystem::Exists(path.parent_path()))
			FileSystem::CreateDirectory(path.parent_path());

		std::ofstream file(path);
		if (file.is_open())
			file << out.c_str();

		if (!file.good())
		{
			LUX_CORE_ERROR_TAG("VR", "Could not write the headset settings to '{}'.", path.string());
			return false;
		}

		return true;
	}

}
