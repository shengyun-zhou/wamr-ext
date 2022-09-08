#include "WamrExtInternalDef.h"
#include "../base/FSUtility.h"
#include "uv.h"
extern "C" {
#include <fd_table.h>
}

WamrExtInstanceConfig::WamrExtInstanceConfig() {
    auto tempDirPath = WAMR_EXT_NS::FSUtility::GetTempDir();
#ifndef _WIN32
    preOpenDirs["/etc"] = "/etc";
    preOpenDirs["/dev"] = "/dev";
#else
#error "Pre-mount directories must be provided for Windows"
#endif
    if (!tempDirPath.empty()) {
        preOpenDirs["/tmp"] = tempDirPath;
        envVars["TMPDIR"] = "/tmp";
    }
}

WamrExtInstance::~WamrExtInstance() {
    if (wasmMainInstance) {
        // Close all FDs opened by app
        std::vector<uint32_t> appFDs;
        uvwasi_t *pUVWasi = wasm_runtime_get_wasi_ctx(wasmMainInstance);
        uvwasi_fd_table_lock(pUVWasi->fds);
        appFDs.reserve(pUVWasi->fds->size);
        for (uint32_t i = 0; i < pUVWasi->fds->size; i++) {
            auto pEntry = pUVWasi->fds->fds[i];
            if (pEntry)
                appFDs.push_back(pEntry->id);
        }
        uvwasi_fd_table_unlock(pUVWasi->fds);
        for (auto appFD : appFDs)
            uvwasi_fd_close(pUVWasi, appFD);
    }
    for (const auto& p : wasmExecEnvMap)
        wasm_exec_env_destroy(p.second);
    wasmExecEnvMap.clear();
    if (wasmMainInstance)
        wasm_runtime_deinstantiate(wasmMainInstance);
    wasmMainInstance = nullptr;
}

namespace WAMR_EXT_NS {
    thread_local char gLastErrorStr[200] = {0};

    int32_t ExtSyscallBase::Invoke(wasm_exec_env_t pExecEnv, uint32_t argc, wasi::wamr_ext_syscall_arg *appArgv) {
        if (argc < m_argSig.length()) {
            assert(false);
            return UVWASI_ENOSYS;
        }
        assert(argc == m_argSig.length());
        auto wasmInstance = get_module_inst(pExecEnv);
        for (size_t i = 0; i < m_argSig.length(); i++) {
            char c = m_argSig[i];
            if (c == '*') {
                void* p = wasm_runtime_addr_app_to_native(wasmInstance, appArgv[i].app_pointer);
                if (!p) {
                    assert(false);
                    return UVWASI_EFAULT;
                }
                appArgv[i].native_pointer = p;
            }
        }
        return DoSyscall(pExecEnv, appArgv);
    }

    int32_t ExtSyscall_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*)>(m_pFunc)(pExecEnv, appArgv[0].native_pointer);
    }

    int32_t ExtSyscall_P_U32::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*, uint32_t)>(m_pFunc)(pExecEnv, appArgv[0].native_pointer, appArgv[1].u32);
    }

    int32_t ExtSyscall_P_U64::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*, uint64_t)>(m_pFunc)(pExecEnv, appArgv[0].native_pointer, appArgv[1].u64);
    }

    int32_t ExtSyscall_P_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].native_pointer, appArgv[1].native_pointer
        );
    }

    int32_t ExtSyscall_U32_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].native_pointer
        );
    }

    int32_t ExtSyscall_U32_U32::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, uint32_t)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].u32
        );
    }

    int32_t ExtSyscall_P_P_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*, void*, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].native_pointer, appArgv[1].native_pointer, appArgv[2].native_pointer
        );
    }

    int32_t ExtSyscall_P_P_U64::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, void*, void*, uint64_t)>(m_pFunc)(
                pExecEnv, appArgv[0].native_pointer, appArgv[1].native_pointer, appArgv[2].u64
        );
    }

    int32_t ExtSyscall_U32_P_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, void*, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].native_pointer, appArgv[2].native_pointer
        );
    }

    int32_t ExtSyscall_U32_U32_U32_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, uint32_t, uint32_t, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].u32, appArgv[2].u32, appArgv[3].native_pointer
        );
    }

    int32_t ExtSyscall_U32_P_P_U32::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, void*, void*, uint32_t)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].native_pointer, appArgv[2].native_pointer, appArgv[3].u32
        );
    }

    int32_t ExtSyscall_U32_U32_U32_P_P::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, uint32_t, uint32_t, void*, void*)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].u32, appArgv[2].u32, appArgv[3].native_pointer, appArgv[4].native_pointer
        );
    }

    int32_t ExtSyscall_U32_U32_U32_P_U32::DoSyscall(wasm_exec_env_t pExecEnv, wasi::wamr_ext_syscall_arg *appArgv) {
        return reinterpret_cast<int32_t(*)(wasm_exec_env_t, uint32_t, uint32_t, uint32_t, void*, uint32_t)>(m_pFunc)(
                pExecEnv, appArgv[0].u32, appArgv[1].u32, appArgv[2].u32, appArgv[3].native_pointer, appArgv[4].u32
        );
    }
};
