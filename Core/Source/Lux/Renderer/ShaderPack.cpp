// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ShaderPack.h"

#include "Lux/Core/Hash.h"

#include "Lux/Serialization/FileStream.h"

#include "Lux/Renderer/RHI/RHIShader.h"

#include "Lux/Core/Hash.h"

#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Lux
{

	namespace
	{
		// The sidecar text, written by WriteStampedPack and read by ReadStamp through one pair of
		// spellings, so the description of a pack can never drift from how it is parsed back.
		std::string RenderStampText(const ShaderPackStamp& stamp)
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			out << Yaml::Key << "ShaderInputs" << Yaml::Value << stamp.m_ShaderInputs;
			out << Yaml::Key << "GlobalMacros" << Yaml::Value << stamp.m_Macros;
			out << Yaml::Key << "RendererName" << Yaml::Value << stamp.m_RendererName;
			out << Yaml::Key << "VendorID" << Yaml::Value << stamp.m_VendorID;
			out << Yaml::Key << "DriverVersion" << Yaml::Value << stamp.m_DriverVersion;
			out << Yaml::Key << "Programs" << Yaml::Value << stamp.m_Programs;
			out << Yaml::EndMap;
			return out.str();
		}
	}



	ShaderPack::ShaderPack(const std::filesystem::path& path)
		: m_Path(path)
	{
		// Read index
		FileStreamReader serializer(path);
		if (!serializer)
		{
			LUX_CORE_ERROR("Shader pack file could not be opened: {}", path.string());
			return;
		}

		serializer.ReadRaw(m_File.Header);
		const bool isLuxShaderPack = memcmp(m_File.Header.HEADER, "LXSP", 4) == 0;
		const bool isLegacyHazelShaderPack = memcmp(m_File.Header.HEADER, "HZSP", 4) == 0;
		if (!isLuxShaderPack && !isLegacyHazelShaderPack)
		{
			LUX_CORE_ERROR("Shader pack '{}' has an invalid header. Expected LXSP.", path.string());
			return;
		}

		m_Loaded = true;
		for (uint32_t i = 0; i < m_File.Header.ShaderProgramCount; i++)
		{
			uint32_t key;
			serializer.ReadRaw(key);
			auto& shaderProgramInfo = m_File.Index.ShaderPrograms[key];
			serializer.ReadRaw(shaderProgramInfo.ReflectionDataOffset);
			serializer.ReadArray(shaderProgramInfo.ModuleIndices);
		}

		auto sp = serializer.GetStreamPosition();
		serializer.ReadArray(m_File.Index.ShaderModules, m_File.Header.ShaderModuleCount);
	}

	bool ShaderPack::Contains(std::string_view name) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_File.Index.ShaderPrograms.find(Hash::GenerateFNVHash(name)) != m_File.Index.ShaderPrograms.end();
	}

	Ref<Shader> ShaderPack::LoadShader(std::string_view name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t nameHash = Hash::GenerateFNVHash(name);
		LUX_CORE_VERIFY(Contains(name));

		const auto& shaderProgramInfo = m_File.Index.ShaderPrograms.at(nameHash);

		FileStreamReader serializer(m_Path);

		serializer.SetStreamPosition(shaderProgramInfo.ReflectionDataOffset);

		// Debug only
		std::string shaderName;
		{
			std::string path(name);
			size_t found = path.find_last_of("/\\");
			shaderName = found != std::string::npos ? path.substr(found + 1) : path;
			found = shaderName.find_last_of('.');
			shaderName = found != std::string::npos ? shaderName.substr(0, found) : name;
		}

		Ref<RHIShader> rhiShader = Ref<RHIShader>::Create();
		rhiShader->m_Name = shaderName;
		rhiShader->m_AssetPath = name;
		//rhiShader->TryReadReflectionData(&serializer);
		// rhiShader->m_DisableOptimization =

		std::map<ShaderStage, std::vector<uint32_t>> shaderModules;
		for (uint32_t index : shaderProgramInfo.ModuleIndices)
		{
			const auto& info = m_File.Index.ShaderModules[index];
			auto& moduleData = shaderModules[info.Stage];

			serializer.SetStreamPosition(info.PackedOffset);
			serializer.ReadArray(moduleData, (uint32_t)info.PackedSize);
		}

		serializer.SetStreamPosition(shaderProgramInfo.ReflectionDataOffset);
		rhiShader->TryReadReflectionData(&serializer);

		rhiShader->SetShaderData(shaderModules);
		rhiShader->CreateDescriptors();

		//Renderer::AcknowledgeParsedGlobalMacros(compiler->GetAcknowledgedMacros(), rhiShader);
		//Renderer::OnShaderReloaded(rhiShader->GetHash());
		return rhiShader;
	}

	Ref<ShaderPack> ShaderPack::CreateFromLibrary(Ref<ShaderLibrary> shaderLibrary, const std::filesystem::path& path)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<ShaderPack> shaderPack = Ref<ShaderPack>::Create();

		const auto& shaderMap = shaderLibrary->GetShaders();
		auto& shaderPackFile = shaderPack->m_File;

		shaderPackFile.Header.Version = 1;
		shaderPackFile.Header.ShaderProgramCount = (uint32_t)shaderMap.size();
		shaderPackFile.Header.ShaderModuleCount = 0;

		// Determine number of modules (per shader)
		// NOTE(Yan): this currently doesn't care about duplicated modules, but it should (eventually, not that important atm)
		uint32_t shaderModuleIndex = 0;
		uint32_t shaderModuleIndexArraySize = 0;
		for (const auto& [name, shader] : shaderMap)
		{
			Ref<RHIShader> rhiShader = shader.As<RHIShader>();
			const auto& shaderData = rhiShader->m_ShaderData;

			shaderPackFile.Header.ShaderModuleCount += (uint32_t)shaderData.size();
			auto& shaderProgramInfo = shaderPackFile.Index.ShaderPrograms[(uint32_t)rhiShader->GetHash()];

			for (int i = 0; i < (int)shaderData.size(); i++)
				shaderProgramInfo.ModuleIndices.emplace_back(shaderModuleIndex++);

			shaderModuleIndexArraySize += sizeof(uint32_t); // size
			shaderModuleIndexArraySize += (uint32_t)shaderData.size() * sizeof(uint32_t); // indices
		}

		uint32_t shaderProgramIndexSize = shaderPackFile.Header.ShaderProgramCount *
			(sizeof(std::map<uint32_t, ShaderPackFile::ShaderProgramInfo>::key_type) + sizeof(ShaderPackFile::ShaderProgramInfo::ReflectionDataOffset))
			+ shaderModuleIndexArraySize;

		FileStreamWriter serializer(path);

		// Write header
		serializer.WriteRaw<ShaderPackFile::FileHeader>(shaderPackFile.Header);

		// ===============
		// Write index
		// ===============
		// Write dummy data for shader programs
		uint64_t shaderProgramIndexPos = serializer.GetStreamPosition();
		serializer.WriteZero(shaderProgramIndexSize);

		// Write dummy data for shader modules
		uint64_t shaderModuleIndexPos = serializer.GetStreamPosition();
		serializer.WriteZero(shaderPackFile.Header.ShaderModuleCount * sizeof(ShaderPackFile::ShaderModuleInfo));
		for (const auto& [name, shader] : shaderMap)
		{
			Ref<RHIShader> rhiShader = shader.As<RHIShader>();

			// Serialize reflection data
			shaderPackFile.Index.ShaderPrograms[(uint32_t)rhiShader->GetHash()].ReflectionDataOffset = serializer.GetStreamPosition();
			rhiShader->SerializeReflectionData(&serializer);

			// Serialize SPIR-V data
			const auto& shaderData = rhiShader->m_ShaderData;
			for (const auto& [stage, data] : shaderData)
			{
				auto& indexShaderModule = shaderPackFile.Index.ShaderModules.emplace_back();
				indexShaderModule.PackedOffset = serializer.GetStreamPosition();
				indexShaderModule.PackedSize = data.size();
				indexShaderModule.Stage = stage;

				serializer.WriteArray(data, false);
			}
		}

		// Write program index
		serializer.SetStreamPosition(shaderProgramIndexPos);
		uint64_t begin = shaderProgramIndexPos;
		for (const auto& [name, programInfo] : shaderPackFile.Index.ShaderPrograms)
		{
			serializer.WriteRaw(name);
			serializer.WriteRaw(programInfo.ReflectionDataOffset);
			serializer.WriteArray(programInfo.ModuleIndices);
		}
		uint64_t end = serializer.GetStreamPosition();
		uint64_t s = end - begin;

		// Write module index
		serializer.SetStreamPosition(shaderModuleIndexPos);
		serializer.WriteArray(shaderPackFile.Index.ShaderModules, false);

		return shaderPack;
	}

	std::filesystem::path ShaderPack::StampPathFor(const std::filesystem::path& packPath)
	{
		// One spelling shared by ReadStamp and WriteStampedPack, so the two files that describe one pack
		// can never disagree about which sidecar belongs to which bytes.
		return std::filesystem::path{packPath.string() + ".stamp"};
	}
	
	// Walked sorted and hashed by content because the stamp is compared by equality: a
	// nondeterministic walk would make every launch look stale. The generated material-graph include
	// is an input rather than a by-product - MaterialGraphRegistry.h:23-24 documents
	// Resources/Cache/Shader/Generated/MaterialGraphs.glslh as an include the shaders consume, so a
	// project-authored graph changes the SPIR-V a pack carries and must invalidate it.
	// A 32-bit FNV chain is staleness detection, not integrity (ShaderPack.h § m_ShaderInputs): a miss
	// asks for a regeneration and nothing here may treat a match as proof a pack is correct.
	std::string ShaderPack::ComputeShaderInputIdentity()
	{
		std::error_code ec;
		std::vector<std::string> inputs;
		for (std::filesystem::recursive_directory_iterator it{"Resources/Shaders", ec}, end; it != end;
			it.increment(ec))
		{
			if (ec or !it->is_regular_file(ec))
				continue;
			const std::string extension = Utils::String::ToLowerCopy(it->path().extension().string());
			if (extension != ".glsl" and extension != ".glslh" and extension != ".hlsl"
				and extension != ".hlslh" and extension != ".slh")
				continue;
			inputs.push_back(it->path().lexically_normal().string());
		}
		if (ec)
			// Without the walk the identity would silently describe nothing, and a compilerless build would
			// consume a pack that matches nothing: report, and let the caller refuse on the difference.
			LUX_CORE_WARN_TAG("Renderer", "Could not enumerate shader sources under Resources/Shaders: {}", ec.message());
		const std::filesystem::path generatedInclude = MaterialGraphRegistry::GetGeneratedIncludePath();
		if (FileSystem::Exists(generatedInclude))
			inputs.push_back(generatedInclude.lexically_normal().string());
		std::ranges::sort(inputs);
		std::string chain;
		for (const std::string& path : inputs)
		{
			const Buffer content = FileSystem::ReadBytes(path);
			if (!content.Data)
			{
				// An unreadable source must not hash like an empty file, or a broken tree would look current.
				LUX_CORE_WARN_TAG("Renderer", "Shader source {} could not be read for the pack identity", path);
				chain += path + ":unreadable;";
				continue;
			}
			const std::string_view bytes{static_cast<const char*>(content.Data), static_cast<size_t>(content.Size)};
			chain += path + ":" + std::to_string(Hash::GenerateFNVHash(bytes)) + ";";
		}
		return std::to_string(Hash::GenerateFNVHash(chain));
	}
	
	// Absent is not an error: the runtime-export path (EditorLayer.cpp:398-407) has always written a
	// bare pack, so every exported package to date has no stamp and must keep working. A stamp that
	// exists but cannot be read or parsed is a real fault and is logged, because the caller maps
	// false to "no stamp" and must not keep consuming a pack whose description is garbage.
	bool ShaderPack::ReadStamp(const std::filesystem::path& packPath, ShaderPackStamp& stampOut)
	{
		const std::filesystem::path stampPath = StampPathFor(packPath);
		if (!FileSystem::Exists(stampPath))
			return false;
		try
		{
			const Yaml::Node document = Yaml::LoadFile(stampPath);
			stampOut.m_ShaderInputs = document["ShaderInputs"].as<std::string>("");
			stampOut.m_Macros = document["GlobalMacros"].as<std::string>("");
			stampOut.m_RendererName = document["RendererName"].as<std::string>("");
			stampOut.m_VendorID = document["VendorID"].as<uint32_t>(0);
			stampOut.m_DriverVersion = document["DriverVersion"].as<uint32_t>(0);
			stampOut.m_Programs = document["Programs"].as<uint32_t>(0);
		}
		catch (const Yaml::Exception& error)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Shader pack stamp {} is unreadable: {}", stampPath.string(), error.what());
			return false;
		}
		return true;
	}
	
	// Main thread only: file I/O (Threading.md § Main reserves long blocking work - file I/O - for
	// main; the callers are the Application Settings action and the shader-pack self-test). Nothing
	// here creates a GPU resource - the SPIR-V is owned by the RHIShaders the library holds, so no
	// Renderer::SubmitResourceFree release is needed (Rendering.md § Ownership and lifetime).
	// Ordering is the safety property: pack bytes are promoted first, then the stamp. A failure
	// between the two leaves a NEW pack with an OLD stamp, which Evaluate reports as InputsChanged
	// (a refusal). The reverse order could leave a stamp vouching for bytes that never arrived.
	// m_Programs is taken from the pack actually written, never from the caller's copy, so the count
	// in the sidecar cannot claim more programs than the file contains.
	bool ShaderPack::WriteStampedPack(Ref<ShaderLibrary> shaderLibrary, const std::filesystem::path& packPath, ShaderPackStamp stamp)
	{
		const std::filesystem::path tempPath{packPath.string() + ".tmp"};
		if (FileSystem::Exists(tempPath) and !FileSystem::DeleteFile(tempPath))
		{
			// A leftover temp from an earlier attempt would be reused by the writer; say so rather than
			// silently truncating it, since the previous pack stays untouched either way.
			LUX_CORE_ERROR_TAG("Renderer", "Could not remove a leftover shader-pack temp {}", tempPath.string());
			return false;
		}
		const Ref<ShaderPack> pack = ShaderPack::CreateFromLibrary(shaderLibrary, tempPath);
		if (!pack)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Could not serialize the shader library into {}", tempPath.string());
			return false;
		}
		// Re-read through the consumer's own reader, so "authored here" and "consumed later" are judged by
		// identical code: ShaderPack.cpp:26-35 decides what counts as loaded, and a pack that reader
		// rejects must not be promoted just because the writer reported success.
		ShaderPack written{tempPath};
		const uint32_t programs = written.m_File.Header.ShaderProgramCount;
		if (!written.IsLoaded() or programs == 0)
		{
			// An empty pack reads as loaded on the magic match alone, so promoting one would serve a build
			// with no shaders at all. Keep the previous pack and report instead.
			LUX_CORE_ERROR_TAG("Renderer", "Shader pack {} holds no shader programs; the library gave {} entries", tempPath.string(), shaderLibrary->GetShaders().size());
			return false;
		}
		stamp.m_Programs = programs;
		const std::string stampText = RenderStampText(stamp);
		const std::filesystem::path stampPath = StampPathFor(packPath);
		const bool promoted = FileSystem::Exists(packPath)
			? FileSystem::ReplaceFileAtomically(tempPath, packPath)
			: FileSystem::Rename(tempPath, packPath);
		if (!promoted)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Could not put the refreshed shader pack {} in place; the previous pack is unchanged", packPath.string());
			return false;
		}
		if (!FileSystem::WriteFileAtomically(stampPath, stampText))
		{
			// The pack is in place but its description is now the previous one, so Evaluate reports
			// InputsChanged - a refusal, never a silent acceptance. The temp stays for inspection.
			LUX_CORE_ERROR_TAG("Renderer", "Could not write the shader-pack stamp beside {}", packPath.string());
			return false;
		}
		// Prove the round trip rather than trusting the write: a stamp that cannot be parsed back would
		// make the next launch validate against garbage.
		ShaderPackStamp readBack;
		if (!ReadStamp(packPath, readBack))
		{
			LUX_CORE_ERROR_TAG("Renderer", "Shader pack stamp beside {} could not be read back after writing", packPath.string());
			return false;
		}
		LUX_CORE_INFO_TAG("Renderer", "Shader pack refreshed: {} programs, source set {}, on {} (vendor {}, driver {})", programs, stamp.m_ShaderInputs, stamp.m_RendererName, stamp.m_VendorID, stamp.m_DriverVersion);
		return true;
	}
	

	// Main thread: it opens the pack, the sidecar and the shader sources, which Threading.md § Main
	// keeps on main (file I/O). The pack is opened through the same reader a consumer uses
	// (ShaderPack.cpp:26-35) so a "loaded" verdict here means exactly what it means at load time;
	// a zero program count is structural because that reader decides loaded on the magic match alone.
	// Inputs and macros are one severity class: both are expanded into the SPIR-V inside the pack,
	// so a difference means this build would render programs that no longer match its sources.
	// Device drift is a report only: SPIR-V is portable across drivers by the Vulkan spec, so another
	// vendor is a difference to state, not automatically a bug (Rendering.md § Validation errors are
	// bugs forbids treating the inputs case as noise, not this one).
	// An unstamped pack is legacy: the bare-pack export path (EditorLayer.cpp:398) predates the sidecar,
	// so it reports rather than refuses - exported packages keep working exactly as before.
	ShaderPackState ShaderPack::Evaluate(const std::filesystem::path& packPath, const ShaderPackStamp& environment)
	{
		ShaderPack pack{packPath};
		if (!pack.IsLoaded() or pack.m_File.Header.ShaderProgramCount == 0)
			// Absent, truncated or empty: the constructor already logged which, and either way there is
			// nothing consumable - a compilerless build cannot produce it either.
			return ShaderPackState::Unreadable;
		ShaderPackStamp recorded;
		if (!ReadStamp(packPath, recorded))
			return ShaderPackState::NoStamp;
		if (recorded.m_Programs != pack.m_File.Header.ShaderProgramCount)
			// The sidecar describes different bytes than the pack it sits beside: the pair was written
			// apart, or a promotion failed between the two halves. Refuse rather than trust either.
			return ShaderPackState::Unreadable;
		if (recorded.m_ShaderInputs != environment.m_ShaderInputs
			or recorded.m_Macros != environment.m_Macros)
			return ShaderPackState::InputsChanged;
		if (recorded.m_RendererName != environment.m_RendererName
			or recorded.m_VendorID != environment.m_VendorID
			or recorded.m_DriverVersion != environment.m_DriverVersion)
			return ShaderPackState::DeviceChanged;
		return ShaderPackState::Current;
	}
	

#if 0
	void ShaderPack::CompileAndStoreShader(std::string_view shaderSourcePath)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t hash = Hash::GenerateFNVHash(shaderSourcePath);
		m_File.Index.ShaderPrograms[hash];

		BinarySerializer serializer(m_File);
		serializer.SerializeToFile(m_Path);
	}

	void ShaderPack::AddShader(Ref<Shader> shader)
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}
#endif

}
