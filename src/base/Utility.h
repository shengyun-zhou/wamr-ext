#pragma once
#include "BaseDef.h"
extern "C" {
#include <fd_table.h>
}

namespace WAMR_EXT_NS {
    class Utility {
    public:
        static uint32_t GetProcessID();
        static uint32_t GetCurrentThreadID();
        static void SetCurrentThreadName(const char* name);
        static const char* GetCurrentThreadName();
        static uvwasi_errno_t GetHostFDByAppFD(wasm_module_inst_t pWasmModuleInst, int32_t appFD, uv_os_fd_t& outHostFD,
                                               const std::function<void(const uvwasi_fd_wrap_t*)>& cb = nullptr);
        static uvwasi_errno_t ConvertErrnoToWasiErrno(int err);
    private:
        static thread_local char g_currentThreadName[64];
    };
}