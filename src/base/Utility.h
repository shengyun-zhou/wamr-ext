#pragma once
#include "BaseDef.h"

namespace WAMR_EXT_NS {
    class Utility {
    public:
        static uint32_t GetProcessID();
        static uint32_t GetCurrentThreadID();
        static wasi_errno_t ConvertErrnoToWasiErrno(int err);
    };
}