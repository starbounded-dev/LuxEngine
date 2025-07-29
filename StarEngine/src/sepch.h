#pragma once

#ifdef SE_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
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
#include <optional>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <StarEngine/Core/Assert.h>
#include <StarEngine/Core/Base.h>
#include <StarEngine/Core/Log.h>
#include <StarEngine/Core/Memory.h>
#include <StarEngine/Core/Delegate.h>
#include <StarEngine/Core/Ref.h>
#include "StarEngine/Debug/Profiler.h"
