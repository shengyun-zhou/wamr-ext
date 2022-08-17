#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cerrno>
#include <cassert>

#include <memory>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>

#include <wasi_types.h>
#include <wasm_export.h>

#define WAMR_EXT_NS wamr_ext

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define INVALID_FD INVALID_HANDLE_VALUE
#else
#include <unistd.h>
#define INVALID_FD (-1)
#endif
