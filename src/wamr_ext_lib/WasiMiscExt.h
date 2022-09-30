#pragma once
#include "../base/Utility.h"

namespace WAMR_EXT_NS {
    class WasiMiscExt {
    public:
        static void Init();
    private:
        static uint32_t AlignedAlloc(wasm_exec_env_t pExecEnv, uint32_t align, uint32_t size);
    };
}
