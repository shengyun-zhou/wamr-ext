#include "WasiWamrExt.h"
#include "WamrExtInternalDef.h"
#include <wasm_runtime.h>
#include <wasm_runtime_common.h>
#include <aot/aot_runtime.h>
#include <mem_alloc.h>
#include <thread>

namespace WAMR_EXT_NS {
    void WasiWamrExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_WAMR_EXT_SYSCTL, std::make_shared<ExtSyscall_S_P_P>((void*)WamrExtSysctl));
    }

    int32_t WasiWamrExt::WamrExtSysctl(wasm_exec_env_t pExecEnv, const char *name, void *buf, uint32_t* bufLen) {
        if (*bufLen <= 0)
            return UVWASI_ERANGE;
        auto pWasmModule = wasm_runtime_get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModule, buf, *bufLen))
            return UVWASI_EFAULT;
        if (strcmp(name, "sysinfo.tid") == 0) {
            uint32_t tempTid = 0;
            if (*bufLen < sizeof(tempTid))
                return UVWASI_ERANGE;
            tempTid = Utility::GetCurrentThreadID();
            *bufLen = sizeof(tempTid);
            memcpy(buf, &tempTid, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.pid") == 0) {
            uint32_t tempPid = 0;
            if (*bufLen < sizeof(tempPid))
                return UVWASI_ERANGE;
            tempPid = Utility::GetProcessID();
            *bufLen = sizeof(tempPid);
            memcpy(buf, &tempPid, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.cpu_count") == 0) {
            uint32_t cpuCount = 1;
            if (*bufLen < sizeof(cpuCount))
                return UVWASI_ERANGE;
            cpuCount = std::thread::hardware_concurrency();
            *bufLen = sizeof(cpuCount);
            memcpy(buf, &cpuCount, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.vm_mem_total") == 0) {
            uint64_t totalMemSize = 0;
            if (*bufLen < sizeof(totalMemSize))
                return UVWASI_ERANGE;
            WASMMemoryInstance* memInst = wasm_get_default_memory((WASMModuleInstance*)pWasmModule);
            totalMemSize = uint64_t(memInst->num_bytes_per_page) * memInst->max_page_count;
            *bufLen = sizeof(totalMemSize);
            memcpy(buf, &totalMemSize, *bufLen);
            return 0;
        } else if (strcmp(name, "sysinfo.vm_mem_avail") == 0) {
            uint64_t availMem = 0;
            if (*bufLen < sizeof(availMem))
                return UVWASI_ERANGE;
            WASMMemoryInstance* memInst = wasm_get_default_memory((WASMModuleInstance*)pWasmModule);
            void* pAppHeap = memInst->heap_handle;
            if (pAppHeap) {
                mem_alloc_info_t allocStatInfo = {0};
                if (mem_allocator_get_alloc_info(pAppHeap, &allocStatInfo))
                    availMem = allocStatInfo.total_free_size;
            }
            *bufLen = sizeof(availMem);
            memcpy(buf, &availMem, *bufLen);
            return 0;
        }
        return UVWASI_EINVAL;
    }
}