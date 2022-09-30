#include "WasiMiscExt.h"

namespace WAMR_EXT_NS {
    void WasiMiscExt::Init() {
        static NativeSymbol syms[] = {
            {"aligned_alloc", (void*)AlignedAlloc, "(ii)i", nullptr},
        };
        wasm_runtime_register_natives("env", syms, sizeof(syms) / sizeof(NativeSymbol));
    }

    uint32_t WasiMiscExt::AlignedAlloc(wasm_exec_env_t pExecEnv, uint32_t align, uint32_t size) {
        // Aligned alloc is not supported now
        void* _p;
        return wasm_runtime_module_malloc(get_module_inst(pExecEnv), size, &_p);
    }
}
