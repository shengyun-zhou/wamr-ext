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
        if (strcmp(name, "sysinfo.tid") == 0) {
            uint32_t tempTid = 0;
            if (*bufLen < sizeof(tempTid))
                return __WASI_ERANGE;
            tempTid = Utility::GetCurrentThreadID();
            *bufLen = sizeof(tempTid);
            memcpy(buf, &tempTid, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.pid") == 0) {
            uint32_t tempPid = 0;
            if (*bufLen < sizeof(tempPid))
                return __WASI_ERANGE;
            tempPid = Utility::GetProcessID();
            *bufLen = sizeof(tempPid);
            memcpy(buf, &tempPid, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.cpu_count") == 0) {
            uint32_t cpuCount = 1;
            if (*bufLen < sizeof(cpuCount))
                return __WASI_ERANGE;
            cpuCount = std::thread::hardware_concurrency();
            *bufLen = sizeof(cpuCount);
            memcpy(buf, &cpuCount, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.vm_mem_total") == 0) {
            uint64_t totalMemSize = 0;
            if (*bufLen < sizeof(totalMemSize))
                return __WASI_ERANGE;
            if (pWasmModule->module_type == Wasm_Module_Bytecode) {
                WASMMemoryInstance* memInst = ((WASMModuleInstance*)pWasmModule)->default_memory;
                totalMemSize = uint64_t(memInst->num_bytes_per_page) * memInst->max_page_count;
            } else if (pWasmModule->module_type == Wasm_Module_AoT) {
                AOTMemoryInstance* memInst = ((AOTModuleInstance*)pWasmModule)->global_table_data.memory_instances;
                totalMemSize = uint64_t(memInst->num_bytes_per_page) * memInst->max_page_count;
            }
            *bufLen = sizeof(totalMemSize);
            memcpy(buf, &totalMemSize, *bufLen);
            return 0;
        }
        return __WASI_EINVAL;
    }
}