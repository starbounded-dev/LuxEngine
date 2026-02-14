#pragma once

#ifdef LUX_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdarg>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <filesystem>
#include <thread>

#include <Lux/Core/Version.h>
#include <Lux/Core/Assert.h>
#include <Lux/Core/Base.h>
#include <Lux/Core/Events/Event.h>
#include <Lux/Core/Log.h>
//#include <Lux/Core/Math/Mat4.h>
#include <Lux/Core/Memory.h>
//#include <Lux/Core/Delegate.h>

#include <Lux/Debug/Profiler.h>

// Jolt (Safety because this file has to be included before all other Jolt headers, at all times)
#ifdef LUX_DEBUG // NOTE(Emily): This is a bit of a hacky fix for some dark magic that happens in Jolt
				// 				We'll need to address this in future.
#define JPH_ENABLE_ASSERTS
#endif
//#include <Jolt/Jolt.h>
