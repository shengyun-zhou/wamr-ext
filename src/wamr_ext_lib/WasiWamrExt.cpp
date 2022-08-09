#include "WasiWamrExt.h"
#include <wasm_runtime.h>
#include <wasm_runtime_common.h>
#include <aot/aot_runtime.h>
#include <thread>

namespace WAMR_EXT_NS {
    void WasiWamrExt::Init() {
        static NativeSymbol nativeSymbols[] = {
            {"wamr_ext_sysctl", (void*)WamrExtSysctl, "(***)i", nullptr},
        };
        wasm_runtime_register_natives("wamr_ext", nativeSymbols, sizeof(nativeSymbols) / sizeof(NativeSymbol));
    }

    int32_t WasiWamrExt::WamrExtSysctl(wasm_exec_env_t pExecEnv, const char *name, void *buf, uint32_t* bufLen) {
        if (*bufLen <= 0)
            return __WASI_ERANGE;
        auto pWasmModule = wasm_runtime_get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, buf, *bufLen))
            return __WASI_EFAULT;
        if (strcmp(name, "sysinfo.cpu_count") == 0) {
            if (*bufLen < sizeof(uint32_t))
                return __WASI_ERANGE;
            uint32_t cpuCount = std::thread::hardware_concurrency();
            *bufLen = sizeof(uint32_t);
            memcpy(buf, &cpuCount, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.vm_mem_total") == 0) {
            if (*bufLen < sizeof(uint64_t))
                return __WASI_ERANGE;
            uint64_t totalMemSize = 0;
            if (pWasmModule->module_type == Wasm_Module_Bytecode) {
                WASMMemoryInstance* memInst = ((WASMModuleInstance*)pWasmModule)->default_memory;
                totalMemSize = uint64_t(memInst->num_bytes_per_page) * memInst->max_page_count;
            } else if (pWasmModule->module_type == Wasm_Module_AoT) {
                AOTMemoryInstance* memInst = ((AOTModuleInstance*)pWasmModule)->global_table_data.memory_instances;
                totalMemSize = uint64_t(memInst->num_bytes_per_page) * memInst->max_page_count;
            }
            *bufLen = sizeof(uint64_t);
            memcpy(buf, &totalMemSize, *bufLen);
            return 0;
        }
        return __WASI_EINVAL;
    }
}