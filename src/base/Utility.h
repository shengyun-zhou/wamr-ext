#pragma once
#include "BaseDef.h"

namespace WAMR_EXT_NS {
    class Utility {
    public:
        static uint32_t GetProcessID();
        static uint32_t GetCurrentThreadID();
        static uvwasi_errno_t ConvertErrnoToWasiErrno(int err);
    };
}