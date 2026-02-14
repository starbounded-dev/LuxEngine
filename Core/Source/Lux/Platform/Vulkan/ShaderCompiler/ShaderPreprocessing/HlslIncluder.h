#pragma once

#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <unknwn.h>
#endif

#include <dxc/dxcapi.h>

#include "ShaderPreprocessor.h"

namespace Lux
{
	struct IncludeData;

	class HlslIncluder : public IDxcIncludeHandler
    {
    public:
        HRESULT LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override;
        HRESULT QueryInterface(const IID& riid, void** ppvObject) override
        {
            return s_DefaultIncludeHandler->QueryInterface(riid, ppvObject);
        }
		std::unordered_set<IncludeData>&& GetIncludeData() { return std::move(m_includeData); }
		std::unordered_set<std::string>&& GetParsedSpecialMacros() { return std::move(m_ParsedSpecialMacros); }

        ULONG AddRef() override { return 0; }

        ULONG Release() override { return 0; }

    private:

        inline static IDxcIncludeHandler* s_DefaultIncludeHandler = nullptr;
		std::unordered_set<IncludeData> m_includeData;
		std::unordered_set<std::string> m_ParsedSpecialMacros;
		std::unordered_map<std::string, HeaderCache> m_HeaderCache;
	};

}
