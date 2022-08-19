#pragma once
#include "../base/WamrExtInternalDef.h"

namespace WAMR_EXT_NS {
    class WasiFSExt {
    public:
        WasiFSExt() = default;
        static void Init();
    private:
        static int32_t FDStatVFS(wasm_exec_env_t pExecEnv, int32_t fd, wasi::wamr_wasi_struct_base* _pAppRetStatInfo);
        static int32_t FDFcntl(wasm_exec_env_t pExecEnv, int32_t fd, wasi::wamr_wasi_struct_base* _pAppFcntlInfo);
    };
}
