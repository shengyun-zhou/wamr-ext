#pragma once
#include "BaseDef.h"

namespace WAMR_EXT_NS {
    class Utility {
    public:
        static uint32_t GetProcessID();
        static uint32_t GetCurrentThreadID();
        static void SetCurrentThreadName(const char* name);
        static std::string GetCurrentThreadName();
        static uvwasi_errno_t ConvertErrnoToWasiErrno(int err);
    };
}