#pragma once

#ifdef SE_PLATFORM_WINDOWS
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

#include <StarEngine/Core/Version.h>
#include <StarEngine/Core/Assert.h>
#include <StarEngine/Core/Base.h>
#include <StarEngine/Core/Events/Event.h>
#include <StarEngine/Core/Log.h>
#include <StarEngine/Core/Math/Mat4.h>
#include <StarEngine/Core/Memory.h>
#include <StarEngine/Core/Delegate.h>

#include <StarEngine/Debug/Profiler.h>

/*
// Jolt (Safety because this file has to be included before all other Jolt headers, at all times)
#ifdef SE_DEBUG // NOTE(Emily): This is a bit of a hacky fix for some dark magic that happens in Jolt
				// 				We'll need to address this in future.
#define JPH_ENABLE_ASSERTS
#endif
*/
