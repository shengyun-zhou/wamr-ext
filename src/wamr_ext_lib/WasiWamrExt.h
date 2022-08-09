#pragma once
#include "../base/Utility.h"

namespace WAMR_EXT_NS {
    class WasiWamrExt {
    public:
        WasiWamrExt() = default;
        static void Init();
    private:
        static int32_t WamrExtSysctl(wasm_exec_env_t pExecEnv, const char* name, void* buf, uint32_t* bufLen);
    };
}
