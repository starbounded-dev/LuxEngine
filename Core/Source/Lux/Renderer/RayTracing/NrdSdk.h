// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Include in RTGI.cpp only. The optional SDK is loaded from the application package
// on first request so missing SDK binaries never prevent Editor or Player startup.
#include "Lux/Core/SharedLibrary.h"
#include "Lux/Utilities/FileSystem.h"

#include <NRD.h>
#include <LuxNRDIntegration.hpp>

namespace Lux::NrdSdk {

	inline std::filesystem::path FindLibrary()
	{
		const auto folder = FileSystem::GetExecutablePath().parent_path();
		std::error_code ec;
#if defined(LUX_PLATFORM_WINDOWS)
		const auto library = folder / "NRD.dll";
#else
		auto library = folder / "libNRD.so";
		if (!std::filesystem::is_regular_file(library, ec))
			library = folder / "lib/libNRD.so";
#endif
		return std::filesystem::is_regular_file(library, ec) ? library : std::filesystem::path{};
	}

	struct Library
	{
		decltype(&nrd::CreateInstance) CreateInstance = nullptr;
		decltype(&nrd::DestroyInstance) DestroyInstance = nullptr;
		decltype(&nrd::GetLibraryDesc) GetLibraryDesc = nullptr;
		decltype(&nrd::GetInstanceDesc) GetInstanceDesc = nullptr;
		decltype(&nrd::GetComputeDispatches) GetComputeDispatches = nullptr;
		decltype(&nrd::SetCommonSettings) SetCommonSettings = nullptr;
		decltype(&nrd::SetDenoiserSettings) SetDenoiserSettings = nullptr;
		decltype(&nrd::GetResourceTypeString) GetResourceTypeString = nullptr;
		bool Ready = false;

		Library()
		{
			const auto path = FindLibrary();
			if (path.empty())
				return;
			m_Module = SharedLibrary::LoadFile(path);
			Ready = m_Module && Bind(CreateInstance, "CreateInstance") && Bind(DestroyInstance, "DestroyInstance") &&
				Bind(GetLibraryDesc, "GetLibraryDesc") && Bind(GetInstanceDesc, "GetInstanceDesc") &&
				Bind(GetComputeDispatches, "GetComputeDispatches") && Bind(SetCommonSettings, "SetCommonSettings") &&
				Bind(SetDenoiserSettings, "SetDenoiserSettings") && Bind(GetResourceTypeString, "GetResourceTypeString");
		}

		~Library()
		{
			SharedLibrary::Unload(m_Module);
		}

	private:
		template<typename Function>
		bool Bind(Function& function, const char* name)
		{
			function = reinterpret_cast<Function>(SharedLibrary::GetSymbol(m_Module, name));
			return function != nullptr;
		}

		void* m_Module = nullptr;
	};

	inline const Library& Get()
	{
		static const Library library;
		return library;
	}

}

// The vendor NRI integration has direct C API calls. Resolve them against the packaged
// library so these symbols do not add an application loader dependency.
namespace nrd {

	Result NRD_CALL CreateInstance(const InstanceCreationDesc& desc, Instance*& instance)
	{
		return Lux::NrdSdk::Get().CreateInstance(desc, instance);
	}

	void NRD_CALL DestroyInstance(Instance& instance)
	{
		Lux::NrdSdk::Get().DestroyInstance(instance);
	}

	const LibraryDesc* NRD_CALL GetLibraryDesc()
	{
		return Lux::NrdSdk::Get().GetLibraryDesc();
	}

	const InstanceDesc* NRD_CALL GetInstanceDesc(const Instance& instance)
	{
		return Lux::NrdSdk::Get().GetInstanceDesc(instance);
	}

	Result NRD_CALL GetComputeDispatches(Instance& instance, const Identifier* identifiers, uint32_t count, const DispatchDesc*& desc, uint32_t& dispatchCount)
	{
		return Lux::NrdSdk::Get().GetComputeDispatches(instance, identifiers, count, desc, dispatchCount);
	}

	Result NRD_CALL SetCommonSettings(Instance& instance, const CommonSettings& settings)
	{
		return Lux::NrdSdk::Get().SetCommonSettings(instance, settings);
	}

	Result NRD_CALL SetDenoiserSettings(Instance& instance, Identifier identifier, const void* settings)
	{
		return Lux::NrdSdk::Get().SetDenoiserSettings(instance, identifier, settings);
	}

	const char* NRD_CALL GetResourceTypeString(ResourceType type)
	{
		return Lux::NrdSdk::Get().GetResourceTypeString(type);
	}

}
