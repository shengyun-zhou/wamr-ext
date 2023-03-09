#include "WasiPthreadExt.h"
#include "WamrExtInternalDef.h"
#include <wasm_runtime.h>
#include <aot_runtime.h>

namespace WAMR_EXT_NS {
    void WasiPthreadExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_HOST_SETNAME, std::make_shared<ExtSyscall_S>((void*)PthreadSetName));

        static NativeSymbol wasiNativeSymbols[] = {
            {"thread-spawn", (void*)WasiThreadSpawn, "(i)i", nullptr},
        };
        wasm_runtime_register_natives("wasi", wasiNativeSymbols, sizeof(wasiNativeSymbols) / sizeof(NativeSymbol));
    }

    WasiPthreadExt::InstancePthreadManager::InstancePthreadManager() {}

#define PTHREAD_EXT_MAIN_THREAD_ID 0

    std::shared_ptr<WasiPthreadExt::InstancePthreadManager::ExecEnvThreadInfo> WasiPthreadExt::InstancePthreadManager::GetThreadInfo(uint32_t handleID) {
        std::lock_guard<std::mutex> _al(m_threadMapLock);
        auto it = m_threadMap.find(handleID);
        if (it == m_threadMap.end())
            return nullptr;
        return it->second;
    }

    void WasiPthreadExt::InitAppMainThreadInfo(wasm_exec_env_t pMainExecEnv) {
        auto* pManager = GetInstPthreadManager(pMainExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
        auto& pThreadInfo = pManager->m_threadMap[PTHREAD_EXT_MAIN_THREAD_ID];
        pThreadInfo.reset(new InstancePthreadManager::ExecEnvThreadInfo({pMainExecEnv, [](wasm_exec_env_t){}}));
        pThreadInfo->handleID = PTHREAD_EXT_MAIN_THREAD_ID;
        pThreadInfo->exitCtrl.waitCount = -1;
    }

    void WasiPthreadExt::CleanupAppThreadInfo(wasm_exec_env_t pMainExecEnv) {
        auto* pManager = GetInstPthreadManager(pMainExecEnv);
        while (true) {
            std::vector<std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo>> allThreadInfo;
            {
                std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
                for (const auto& it : pManager->m_threadMap) {
                    if (it.second->handleID != PTHREAD_EXT_MAIN_THREAD_ID)
                        allThreadInfo.push_back(it.second);
                }
            }
            if (allThreadInfo.empty())
                break;
            for (const auto& pThreadInfo : allThreadInfo)
                CancelAppThread(pThreadInfo->pExecEnv.get());
            for (const auto& pThreadInfo : allThreadInfo)
                DoHostThreadJoin(pManager, pThreadInfo);
        }
        DoAppThreadExit(pMainExecEnv);
        assert(pManager->m_threadMap.size() == 1);
    }

    WasiPthreadExt::InstancePthreadManager::ExecEnvThreadInfo *WasiPthreadExt::GetExecEnvThreadInfo(wasm_exec_env_t pExecEnv) {
        return (WasiPthreadExt::InstancePthreadManager::ExecEnvThreadInfo*)wasm_runtime_get_user_data(pExecEnv);
    }

    WasiPthreadExt::InstancePthreadManager* WasiPthreadExt::GetInstPthreadManager(wasm_exec_env_t pExecEnv) {
        auto* pWamrExtInst = (WamrExtInstance*)wasm_runtime_get_custom_data(get_module_inst(pExecEnv));
        return &pWamrExtInst->wasiPthreadManager;
    }

    void WasiPthreadExt::DoHostThreadJoin(InstancePthreadManager* pManager, const std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo> &pThreadInfo) {
        bool bDelThreadInfo = false;
        do {
            std::unique_lock al(pThreadInfo->exitCtrl.exitLock);
            if (pThreadInfo->exitCtrl.waitCount < 0) {
                // Thread exited or detached
                bDelThreadInfo = true;
                break;
            }
            pThreadInfo->exitCtrl.waitCount++;
            pThreadInfo->exitCtrl.exitCV.wait(al);
            pThreadInfo->exitCtrl.waitCount--;
            if (pThreadInfo->exitCtrl.waitCount == 0) {
                pThreadInfo->pHostThread->join();
                pThreadInfo->exitCtrl.waitCount = -1;
                bDelThreadInfo = true;
            }
        } while (false);
        if (bDelThreadInfo) {
            std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
            pManager->m_threadMap.erase(pThreadInfo->handleID);
        }
    }

    void WasiPthreadExt::DoAppThreadExit(wasm_exec_env_t pExecEnv) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto* pThreadInfo = GetExecEnvThreadInfo(pExecEnv);
        const char* exception = wasm_runtime_get_exception(get_module_inst(pExecEnv));
        if (exception) {
            /* skip "Exception: " */
            exception += 11;
            if (pThreadInfo->handleID != PTHREAD_EXT_MAIN_THREAD_ID) {
                // Spread exception info
                std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
                for (const auto &it: pManager->m_threadMap) {
                    if (it.second->pExecEnv.get() != pExecEnv)
                        wasm_runtime_set_exception(get_module_inst(it.second->pExecEnv.get()), exception);
                }
            }
        }
        if (pThreadInfo->handleID == PTHREAD_EXT_MAIN_THREAD_ID)
            return;
        // Mark this app thread terminated, then the interpreter will also stop after return.
        CancelAppThread(pExecEnv);
    }

#define APP_THREAD_DEFAULT_STACK_SIZE (128 * 1024)

    int32_t WasiPthreadExt::WasiThreadSpawn(wasm_exec_env_t pExecEnv, uint32_t appArg) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        auto* pManager = GetInstPthreadManager(pExecEnv);
        wasm_module_inst_t pNewWasmInst = nullptr;
        wasm_exec_env_t pNewWasmExecEnv = nullptr;
        std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo> pThreadInfo;
        uvwasi_errno_t err = 0;
        int32_t retTid = 0;
        do {
            if (!(pNewWasmInst = wasm_runtime_instantiate_internal(wasm_exec_env_get_module(pExecEnv), true, pExecEnv->wasm_stack_size, 0, nullptr, 0))) {
                err = -1;
                break;
            }
            wasm_function_inst_t wasmThreadEntryFuncInst = wasm_runtime_lookup_function(pNewWasmInst, "wasi_thread_start", "(ii)");
            if (!wasmThreadEntryFuncInst) {
                err = -1;
                break;
            }
            wasm_runtime_set_custom_data_internal(pNewWasmInst, wasm_runtime_get_custom_data(pWasmModuleInst));
            wasm_runtime_set_wasi_ctx(pNewWasmInst, wasm_runtime_get_wasi_ctx(pWasmModuleInst));

            if (!(pNewWasmExecEnv = wasm_exec_env_create(pNewWasmInst, pExecEnv->wasm_stack_size))) {
                err = -1;
                break;
            }
            pThreadInfo.reset(new InstancePthreadManager::ExecEnvThreadInfo({pNewWasmExecEnv, [](wasm_exec_env_t pExecEnv) {
                wasm_runtime_deinstantiate_internal(get_module_inst(pExecEnv), true);
                wasm_exec_env_destroy(pExecEnv);
            }}));
            pThreadInfo->appStartArg.funcArg = appArg;
            {
                uint32_t _offset = 0;
                if (!wasm_exec_env_get_aux_stack(pExecEnv, &_offset, &pThreadInfo->stackCtrl.stackSize))
                    pThreadInfo->stackCtrl.stackSize = APP_THREAD_DEFAULT_STACK_SIZE;
            }
            pThreadInfo->stackCtrl.appStackAddr = 0;
            pThreadInfo->stackCtrl.bStackFromApp = pThreadInfo->stackCtrl.appStackAddr;
            if (!pThreadInfo->stackCtrl.bStackFromApp) {
                // Allocate 16 bytes more space here for alignment later
                pThreadInfo->stackCtrl.stackSize += 16;
                void* _pAddr;
                pThreadInfo->stackCtrl.appStackAddr = wasm_runtime_module_malloc(pWasmModuleInst, pThreadInfo->stackCtrl.stackSize, &_pAddr);
                if (pThreadInfo->stackCtrl.appStackAddr == 0) {
                    assert(false);
                    err = UVWASI_ENOMEM;
                    break;
                }
            }
            {
                // Make stack address aligned at 16 bytes
                uint32_t appStackBottom = (pThreadInfo->stackCtrl.appStackAddr + pThreadInfo->stackCtrl.stackSize) / 16 * 16;
                pThreadInfo->stackCtrl.stackSize = appStackBottom - pThreadInfo->stackCtrl.appStackAddr;
                if (!wasm_exec_env_set_aux_stack(pNewWasmExecEnv, appStackBottom, pThreadInfo->stackCtrl.stackSize)) {
                    assert(false);
                    err = -1;
                    break;
                }
            }
            {
                std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
                while (true) {
                    pThreadInfo->handleID = pManager->m_curHandleId++;
                    auto &pTempThreadInfo = pManager->m_threadMap[pThreadInfo->handleID];
                    if (!pTempThreadInfo) {
                        pTempThreadInfo = pThreadInfo;
                        retTid = std::min<uint32_t>(INT32_MAX, pThreadInfo->handleID);
                        break;
                    }
                }
            }
            pThreadInfo->pHostThread = new std::thread([pCurThreadInfo = pThreadInfo.get(), wasmThreadEntryFuncInst, retTid]{
                wasm_exec_env_set_thread_info(pCurThreadInfo->pExecEnv.get());
                wasm_val_t argv[2];
                argv[0].kind = WASM_I32; argv[0].of.i32 = retTid;
                argv[1].kind = WASM_I32; argv[1].of.i32 = pCurThreadInfo->appStartArg.funcArg;

                wasm_runtime_call_wasm_a(pCurThreadInfo->pExecEnv.get(), wasmThreadEntryFuncInst, 0, nullptr, 2, argv);
                WasiPthreadExt::DoAppThreadExit(pCurThreadInfo->pExecEnv.get());
                // Free app stack
                if (!pCurThreadInfo->stackCtrl.bStackFromApp)
                    wasm_runtime_module_free(get_module_inst(pCurThreadInfo->pExecEnv.get()), pCurThreadInfo->stackCtrl.appStackAddr);
                {
                    std::unique_lock al(pCurThreadInfo->exitCtrl.exitLock);
                    if (pCurThreadInfo->exitCtrl.waitCount == 0) {
                        pCurThreadInfo->pHostThread->detach();
                        pCurThreadInfo->exitCtrl.waitCount = -1;
                    } else {
                        pCurThreadInfo->exitCtrl.exitCV.notify_all();
                    }
                }
            });
        } while (false);
        if (err != 0) {
            if (pThreadInfo) {
                if (pNewWasmInst && !pThreadInfo->stackCtrl.bStackFromApp && pThreadInfo->stackCtrl.appStackAddr)
                    wasm_runtime_module_free(pNewWasmInst, pThreadInfo->stackCtrl.appStackAddr);
                std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
                pManager->m_threadMap.erase(pThreadInfo->handleID);
            }
            if (pNewWasmInst)
                wasm_runtime_deinstantiate_internal(pNewWasmInst, true);
            if (pNewWasmExecEnv)
                wasm_exec_env_destroy(pNewWasmExecEnv);
        }
        if (err != 0) {
            int32_t tempErr = err;
            return tempErr > 0 ? -tempErr : tempErr;
        }
        return retTid;
    }

    int32_t WasiPthreadExt::PthreadSetName(wasm_exec_env_t pExecEnv, char *name) {
        Utility::SetCurrentThreadName(name);
        return 0;
    }
}
