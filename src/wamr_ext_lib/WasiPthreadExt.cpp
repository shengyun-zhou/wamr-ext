#include "WasiPthreadExt.h"
#include "WamrExtInternalDef.h"
#include <wasm_runtime.h>
#include <aot_runtime.h>

namespace WAMR_EXT_NS {
    namespace wasi {
        struct wamr_create_thread_req {
            uint32_t ret_handle_id;
            uint32_t app_start_func;
            uint32_t app_start_arg;
            uint32_t app_stack_addr;
            uint32_t stack_size;
            uint8_t thread_detached;
        };
        static_assert(std::is_trivial<wamr_create_thread_req>::value);
    }

    void WasiPthreadExt::Init() {
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_INIT, std::make_shared<ExtSyscall_P_U32>((void*)PthreadMutexInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_TIMEDLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadMutexTimedLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_UNLOCK, std::make_shared<ExtSyscall_P>((void*)PthreadMutexUnlock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_MUTEX_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadMutexDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_INIT, std::make_shared<ExtSyscall_P>((void*)PthreadCondInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_TIMEDWAIT, std::make_shared<ExtSyscall_P_P_U64>((void*)PthreadCondTimedWait));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_BROADCAST, std::make_shared<ExtSyscall_P>((void*)PthreadCondBroadcast));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_SIGNAL, std::make_shared<ExtSyscall_P>((void*)PthreadCondSignal));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_COND_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadCondDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_INIT, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockInit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDRDLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadRWLockTimedRdLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_TIMEDWRLOCK, std::make_shared<ExtSyscall_P_U64>((void*)PthreadRWLockTimedWrLock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_UNLOCK, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockUnlock));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_RWLOCK_DESTROY, std::make_shared<ExtSyscall_P>((void*)PthreadRWLockDestroy));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_HOST_SETNAME, std::make_shared<ExtSyscall_S>((void*)PthreadSetName));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_CREATE, std::make_shared<ExtSyscall_P>((void*)PthreadCreate));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_SELF, std::make_shared<ExtSyscall_P>((void*)PthreadSelf));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_EXIT, std::make_shared<ExtSyscall_U32>((void*)PthreadExit));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_JOIN, std::make_shared<ExtSyscall_U32_P>((void*)PthreadJoin));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_DETACH, std::make_shared<ExtSyscall_U32>((void*)PthreadDetach));

        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_KEY_CREATE, std::make_shared<ExtSyscall_P_U32>((void*)PthreadKeyCreate));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_KEY_DELETE, std::make_shared<ExtSyscall_U32>((void*)PthreadKeyDelete));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_SETSPECIFIC, std::make_shared<ExtSyscall_U32_U32>((void*)PthreadSetSpecific));
        RegisterExtSyscall(wasi::__EXT_SYSCALL_PTHREAD_GETSPECIFIC, std::make_shared<ExtSyscall_U32_P>((void*)PthreadGetSpecific));
    }

    WasiPthreadExt::InstancePthreadManager::InstancePthreadManager() {
        m_pthreadMutexMap.reserve(30);
        m_pthreadCondMap.reserve(15);
        m_pthreadRWLockMap.reserve(10);
    }

#define PTHREAD_EXT_MAIN_THREAD_ID 0
#define PTHREAD_EXT_MUTEX_UNINIT_ID 0
#define PTHREAD_EXT_COND_UNINIT_ID 0
#define PTHREAD_EXT_RWLOCK_UNINIT_ID 0

    std::shared_ptr<WasiPthreadExt::InstancePthreadManager::ExecEnvThreadInfo> WasiPthreadExt::InstancePthreadManager::GetThreadInfo(uint32_t handleID) {
        std::lock_guard<std::mutex> _al(m_threadMapLock);
        auto it = m_threadMap.find(handleID);
        if (it == m_threadMap.end())
            return nullptr;
        return it->second;
    }

    std::shared_ptr<pthread_mutex_t> WasiPthreadExt::InstancePthreadManager::GetMutex(uint32_t *pHandleID, uint8_t autoCreatedMutexType) {
        std::shared_ptr<pthread_mutex_t> pRetMutex;
        std::lock_guard<std::mutex> _al(m_pthreadMutexMapLock);
        if (*pHandleID == PTHREAD_EXT_MUTEX_UNINIT_ID) {
            if (autoCreatedMutexType == 0)
                return nullptr;
            while (true) {
                *pHandleID = m_curHandleId++;
                if (*pHandleID == PTHREAD_EXT_MUTEX_UNINIT_ID)
                    continue;
                auto &pMutex = m_pthreadMutexMap[*pHandleID];
                if (!pMutex) {
                    pMutex.reset(new pthread_mutex_t, [](pthread_mutex_t* p) {
                        pthread_mutex_destroy(p);
                        delete p;
                    });
                    pRetMutex = pMutex;
                    pthread_mutexattr_t mutexAttr;
                    pthread_mutexattr_init(&mutexAttr);
                    if (autoCreatedMutexType == 2)
                        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);
                    else
                        pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_DEFAULT);
                    pthread_mutex_init(pMutex.get(), &mutexAttr);
                    pthread_mutexattr_destroy(&mutexAttr);
                    break;
                }
            }
        } else {
            auto it = m_pthreadMutexMap.find(*pHandleID);
            if (it != m_pthreadMutexMap.end())
                pRetMutex = it->second;
        }
        return pRetMutex;
    }

    std::shared_ptr<pthread_cond_t> WasiPthreadExt::InstancePthreadManager::GetCond(uint32_t *pHandleID, bool bAutoCreated) {
        std::shared_ptr<pthread_cond_t> pRetCond;
        std::lock_guard<std::mutex> _al(m_pthreadCondLock);
        if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID) {
            if (!bAutoCreated)
                return nullptr;
            while (true) {
                *pHandleID = m_curHandleId++;
                if (*pHandleID == PTHREAD_EXT_COND_UNINIT_ID)
                    continue;
                auto &pCond = m_pthreadCondMap[*pHandleID];
                if (!pCond) {
                    pCond.reset(new pthread_cond_t, [](pthread_cond_t* p) {
                        pthread_cond_destroy(p);
                        delete p;
                    });
                    pRetCond = pCond;
                    pthread_cond_init(pCond.get(), nullptr);
                    break;
                }
            }
        } else {
            auto it = m_pthreadCondMap.find(*pHandleID);
            if (it != m_pthreadCondMap.end())
                pRetCond = it->second;
        }
        return pRetCond;
    }

    std::shared_ptr<pthread_rwlock_t> WasiPthreadExt::InstancePthreadManager::GetRWLock(uint32_t *pHandleID, bool bAutoCreated) {
        std::shared_ptr<pthread_rwlock_t> pRetRWLock;
        std::lock_guard<std::mutex> _al(m_pthreadRWLockMapLock);
        if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID) {
            if (!bAutoCreated)
                return nullptr;
            while (true) {
                *pHandleID = m_curHandleId++;
                if (*pHandleID == PTHREAD_EXT_RWLOCK_UNINIT_ID)
                    continue;
                auto &pRWLock = m_pthreadRWLockMap[*pHandleID];
                if (!pRWLock) {
                    pRWLock.reset(new pthread_rwlock_t, [](pthread_rwlock_t* p) {
                        pthread_rwlock_destroy(p);
                        delete p;
                    });
                    pRetRWLock = pRWLock;
                    pthread_rwlock_init(pRWLock.get(), nullptr);
                    break;
                }
            }
        } else {
            auto it = m_pthreadRWLockMap.find(*pHandleID);
            if (it != m_pthreadRWLockMap.end())
                pRetRWLock = it->second;
        }
        return pRetRWLock;
    }

    void WasiPthreadExt::InitAppMainThreadInfo(wasm_exec_env_t pMainExecEnv) {
        auto* pManager = GetInstPthreadManager(pMainExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
        auto& pThreadInfo = pManager->m_threadMap[PTHREAD_EXT_MAIN_THREAD_ID];
        pThreadInfo.reset(new InstancePthreadManager::ExecEnvThreadInfo(pMainExecEnv));
        pThreadInfo->handleID = PTHREAD_EXT_MAIN_THREAD_ID;
        pThreadInfo->exitCtrl.waitCount = -1;
        pThreadInfo->exitCtrl.bAppDetached = true;
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
                CancelAppThread(pThreadInfo->pExecEnv);
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
                pthread_join(pThreadInfo->hostThreadHandler, nullptr);
                pThreadInfo->exitCtrl.waitCount = -1;
                bDelThreadInfo = true;
            }
        } while (false);
        if (bDelThreadInfo) {
            std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
            auto tempID = pThreadInfo->handleID;
            pManager->m_threadMap.erase(tempID);
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
                    if (it.second->pExecEnv != pExecEnv)
                        wasm_runtime_set_exception(get_module_inst(it.second->pExecEnv), exception);
                }
            }
        } else {
            // pthread key cleanup
            std::unique_lock<std::mutex> al(pManager->m_keyInfoLock);
            for (uint32_t i = 0; i < InstancePthreadManager::MAX_KEY_NUM; i++) {
                if (pManager->m_keyInfoArr[i].bCreated && pManager->m_keyInfoArr[i].appDestructorFunc != 0) {
                    InstancePthreadManager::KeyInfo tempInfo = pManager->m_keyInfoArr[i];
                    al.unlock();
                    uint32_t arg = pThreadInfo->keyData[i];
                    wasm_runtime_call_indirect(pExecEnv, tempInfo.appDestructorFunc, 1, &arg);
                    al.lock();
                }
            }
        }
        if (pThreadInfo->handleID == PTHREAD_EXT_MAIN_THREAD_ID)
            return;
        // Mark this app thread terminated, then the interpreter will also stop after return.
        CancelAppThread(pExecEnv);
    }

    void WasiPthreadExt::GetTimeoutTimespec(struct timespec &ts, uint64_t useconds) {
#ifdef _WIN32
        timeval tv;
        gettimeofday(&tv, nullptr);
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;
#else
        clock_gettime(CLOCK_REALTIME, &ts);
#endif
        ts.tv_sec += (time_t)useconds / (1000 * 1000);
        ts.tv_nsec += long(useconds % (1000 * 1000)) * 1000;
        while (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ++ts.tv_sec;
        }
    }

#define HOST_THREAD_STACK_SIZE (128 * 1024)

    int32_t WasiPthreadExt::PthreadCreate(wasm_exec_env_t pExecEnv, void *_pAppCreateThreadReq) {
        wasm_module_inst_t pWasmModuleInst = get_module_inst(pExecEnv);
        if (!wasm_runtime_validate_native_addr(pWasmModuleInst, _pAppCreateThreadReq, sizeof(wasi::wamr_create_thread_req)))
            return UVWASI_EFAULT;
        auto* pAppCreateThreadReq = (wasi::wamr_create_thread_req*)_pAppCreateThreadReq;
        if (pAppCreateThreadReq->app_start_func == 0 || pAppCreateThreadReq->stack_size < 2048)
            return UVWASI_EINVAL;
        if (pAppCreateThreadReq->app_stack_addr) {
            if (!wasm_runtime_validate_app_addr(pWasmModuleInst, pAppCreateThreadReq->app_stack_addr, pAppCreateThreadReq->stack_size))
                return UVWASI_EFAULT;
        }
        auto* pManager = GetInstPthreadManager(pExecEnv);
        wasm_module_inst_t pNewWasmInst = nullptr;
        wasm_exec_env_t pNewWasmExecEnv = nullptr;
        std::shared_ptr<InstancePthreadManager::ExecEnvThreadInfo> pThreadInfo;
        uvwasi_errno_t err = 0;
        do {
            if (!(pNewWasmInst = wasm_runtime_instantiate_internal(wasm_exec_env_get_module(pExecEnv), true, pExecEnv->wasm_stack_size, 0, nullptr, 0))) {
                err = -1;
                break;
            }
            wasm_runtime_set_custom_data_internal(pNewWasmInst, wasm_runtime_get_custom_data(pWasmModuleInst));
            wasm_runtime_set_wasi_ctx(pNewWasmInst, wasm_runtime_get_wasi_ctx(pWasmModuleInst));

            if (!(pNewWasmExecEnv = wasm_exec_env_create(pNewWasmInst, pExecEnv->wasm_stack_size))) {
                err = -1;
                break;
            }
            pThreadInfo.reset(new InstancePthreadManager::ExecEnvThreadInfo(pNewWasmExecEnv));
            pThreadInfo->appStartArg.appStartFunc = pAppCreateThreadReq->app_start_func;
            pThreadInfo->appStartArg.funcArg = pAppCreateThreadReq->app_start_arg;
            pThreadInfo->stackCtrl.stackSize = pAppCreateThreadReq->stack_size;
            pThreadInfo->stackCtrl.appStackAddr = pAppCreateThreadReq->app_stack_addr;
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
            pThreadInfo->exitCtrl.bAppDetached = pAppCreateThreadReq->thread_detached;
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
                    pAppCreateThreadReq->ret_handle_id = pThreadInfo->handleID = pManager->m_curHandleId++;
                    auto &pTempThreadInfo = pManager->m_threadMap[pAppCreateThreadReq->ret_handle_id];
                    if (!pTempThreadInfo) {
                        pTempThreadInfo = pThreadInfo;
                        break;
                    }
                }
            }
            pthread_attr_t hostThreadAttr;
            pthread_attr_init(&hostThreadAttr);
            pthread_attr_setstacksize(&hostThreadAttr, HOST_THREAD_STACK_SIZE);
            err = Utility::ConvertErrnoToWasiErrno(pthread_create(&pThreadInfo->hostThreadHandler, &hostThreadAttr, [](void* _arg)->void* {
                wasm_exec_env_t pNewExecEnv = (wasm_exec_env_t)_arg;
                auto* pThreadInfo = WasiPthreadExt::GetExecEnvThreadInfo(pNewExecEnv);
                auto* pManager = WasiPthreadExt::GetInstPthreadManager(pNewExecEnv);
                wasm_exec_env_set_thread_info(pNewExecEnv);
                uint32_t argv[1];
                argv[0] = pThreadInfo->appStartArg.funcArg;
                wasm_runtime_call_indirect(pNewExecEnv, pThreadInfo->appStartArg.appStartFunc, 1, argv);
                WasiPthreadExt::PthreadExit(pNewExecEnv, argv[0]);
                // Free app stack
                if (!pThreadInfo->stackCtrl.bStackFromApp)
                    wasm_runtime_module_free(get_module_inst(pNewExecEnv), pThreadInfo->stackCtrl.appStackAddr);
                bool bDelThreadInfo = false;
                {
                    std::unique_lock al(pThreadInfo->exitCtrl.exitLock);
                    bDelThreadInfo = pThreadInfo->exitCtrl.bAppDetached;
                    if (pThreadInfo->exitCtrl.waitCount == 0) {
                        pthread_detach(pThreadInfo->hostThreadHandler);
                        pThreadInfo->exitCtrl.waitCount = -1;
                    } else {
                        pThreadInfo->exitCtrl.exitCV.notify_all();
                        bDelThreadInfo = false;
                    }
                    wasm_runtime_deinstantiate_internal(get_module_inst(pNewExecEnv), true);
                    wasm_exec_env_destroy(pNewExecEnv);
                }
                if (bDelThreadInfo) {
                    std::lock_guard<std::mutex> _al(pManager->m_threadMapLock);
                    auto tempID = pThreadInfo->handleID;
                    pManager->m_threadMap.erase(tempID);
                }
                return nullptr;
            }, pNewWasmExecEnv));
            pthread_attr_destroy(&hostThreadAttr);
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
        return err;
    }

    int32_t WasiPthreadExt::PthreadSelf(wasm_exec_env_t pExecEnv, uint32_t *thread) {
        *thread = GetExecEnvThreadInfo(pExecEnv)->handleID;
        return 0;
    }

    int32_t WasiPthreadExt::PthreadExit(wasm_exec_env_t pExecEnv, uint32_t retval) {
        auto* pThreadInfo = GetExecEnvThreadInfo(pExecEnv);
        if (pThreadInfo->handleID == PTHREAD_EXT_MAIN_THREAD_ID) {
            assert(false);
            wasm_runtime_set_exception(get_module_inst(pExecEnv), "app main thread exited via pthread_exit()");
            return 0;
        }
        pThreadInfo->exitCtrl.appRetValAddr = retval;
        DoAppThreadExit(pExecEnv);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadJoin(wasm_exec_env_t pExecEnv, uint32_t thread, uint32_t *retVal) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pThreadInfo = pManager->GetThreadInfo(thread);
        if (!pThreadInfo) {
            assert(false);
            return UVWASI_ESRCH;
        }
        DoHostThreadJoin(pManager, pThreadInfo);
        *retVal = pThreadInfo->exitCtrl.appRetValAddr;
        return 0;
    }

    int32_t WasiPthreadExt::PthreadDetach(wasm_exec_env_t pExecEnv, uint32_t thread) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pThreadInfo = pManager->GetThreadInfo(thread);
        if (!pThreadInfo) {
            assert(false);
            return UVWASI_ESRCH;
        }
        std::lock_guard<std::mutex> _al(pThreadInfo->exitCtrl.exitLock);
        if (pThreadInfo->exitCtrl.bAppDetached)
            return UVWASI_EINVAL;
        pThreadInfo->exitCtrl.bAppDetached = true;
        return 0;
    }

    int32_t WasiPthreadExt::PthreadKeyCreate(wasm_exec_env_t pExecEnv, uint32_t *key, uint32_t appDestructorFunc) {
        auto *pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_keyInfoLock);
        for (uint32_t i = 0; i < InstancePthreadManager::MAX_KEY_NUM; i++) {
            if (!pManager->m_keyInfoArr[i].bCreated) {
                pManager->m_keyInfoArr[i].bCreated = true;
                pManager->m_keyInfoArr[i].appDestructorFunc = appDestructorFunc;
                *key = i;
                return 0;
            }
        }
        return UVWASI_EAGAIN;
    }

    int32_t WasiPthreadExt::PthreadKeyDelete(wasm_exec_env_t pExecEnv, uint32_t key) {
        if (key >= InstancePthreadManager::MAX_KEY_NUM)
            return UVWASI_EINVAL;
        auto *pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_keyInfoLock);
        pManager->m_keyInfoArr[key].bCreated = false;
        pManager->m_keyInfoArr[key].appDestructorFunc = 0;
        return 0;
    }

    int32_t WasiPthreadExt::PthreadSetSpecific(wasm_exec_env_t pExecEnv, uint32_t key, uint32_t val) {
        if (key >= InstancePthreadManager::MAX_KEY_NUM)
            return UVWASI_EINVAL;
        auto* pThreadInfo = GetExecEnvThreadInfo(pExecEnv);
        pThreadInfo->keyData[key] = val;
        return 0;
    }

    int32_t WasiPthreadExt::PthreadGetSpecific(wasm_exec_env_t pExecEnv, uint32_t key, uint32_t *val) {
        if (key >= InstancePthreadManager::MAX_KEY_NUM)
            return UVWASI_EINVAL;
        auto* pThreadInfo = GetExecEnvThreadInfo(pExecEnv);
        *val = pThreadInfo->keyData[key];
        return 0;
    }

    int32_t WasiPthreadExt::PthreadMutexInit(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint32_t mutexType) {
        auto *pManager = GetInstPthreadManager(pExecEnv);
        bool bRecursive = mutexType == 1;
        *mutex = PTHREAD_EXT_MUTEX_UNINIT_ID;
        pManager->GetMutex(mutex, bRecursive ? 2 : 1);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadMutexUnlock(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 0);
        if (!pMutex)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_mutex_unlock(pMutex.get()));
    }

    int32_t WasiPthreadExt::PthreadMutexTimedLock(wasm_exec_env_t pExecEnv, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pMutex)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_mutex_lock(pMutex.get());
        } else if (useconds == 0) {
            err = pthread_mutex_trylock(pMutex.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_mutex_timedlock(pMutex.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    int32_t WasiPthreadExt::PthreadMutexDestroy(wasm_exec_env_t pExecEnv, uint32_t *mutex) {
        if (*mutex == PTHREAD_EXT_MUTEX_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadMutexMapLock);
        auto it = pManager->m_pthreadMutexMap.find(*mutex);
        if (it == pManager->m_pthreadMutexMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadMutexMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondInit(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        *cond = PTHREAD_EXT_COND_UNINIT_ID;
        pManager->GetCond(cond, true);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondDestroy(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        if (*cond == PTHREAD_EXT_COND_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadCondLock);
        auto it = pManager->m_pthreadCondMap.find(*cond);
        if (it == pManager->m_pthreadCondMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadCondMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadCondTimedWait(wasm_exec_env_t pExecEnv, uint32_t *cond, uint32_t *mutex, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        auto pMutex = pManager->GetMutex(mutex, 1);
        if (!pCond || !pMutex)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_cond_wait(pCond.get(), pMutex.get());
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_cond_timedwait(pCond.get(), pMutex.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    int32_t WasiPthreadExt::PthreadCondSignal(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        if (!pCond)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_signal(pCond.get()));
    }

    int32_t WasiPthreadExt::PthreadCondBroadcast(wasm_exec_env_t pExecEnv, uint32_t *cond) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pCond = pManager->GetCond(cond, true);
        if (!pCond)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_cond_broadcast(pCond.get()));
    }

    int32_t WasiPthreadExt::PthreadRWLockInit(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        *rwlock = PTHREAD_EXT_RWLOCK_UNINIT_ID;
        pManager->GetRWLock(rwlock, true);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadRWLockDestroy(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        if (*rwlock == PTHREAD_EXT_RWLOCK_UNINIT_ID)
            return 0;
        auto* pManager = GetInstPthreadManager(pExecEnv);
        std::lock_guard<std::mutex> _al(pManager->m_pthreadRWLockMapLock);
        auto it = pManager->m_pthreadRWLockMap.find(*rwlock);
        if (it == pManager->m_pthreadRWLockMap.end()) {
            assert(false);
            return UVWASI_EINVAL;
        }
        pManager->m_pthreadRWLockMap.erase(it);
        return 0;
    }

    int32_t WasiPthreadExt::PthreadRWLockTimedRdLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_rwlock_rdlock(pRWLock.get());
        } else if (useconds == 0) {
            err = pthread_rwlock_tryrdlock(pRWLock.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_rwlock_timedrdlock(pRWLock.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    int32_t WasiPthreadExt::PthreadRWLockTimedWrLock(wasm_exec_env_t pExecEnv, uint32_t *rwlock, uint64_t useconds) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, true);
        if (!pRWLock)
            return UVWASI_EINVAL;
        int err = 0;
        if (useconds == UINT64_MAX) {
            err = pthread_rwlock_wrlock(pRWLock.get());
        } else if (useconds == 0) {
            err = pthread_rwlock_trywrlock(pRWLock.get());
            if (err == EBUSY)
                err = ETIMEDOUT;
        } else {
            struct timespec ts;
            GetTimeoutTimespec(ts, useconds);
            err = pthread_rwlock_timedwrlock(pRWLock.get(), &ts);
        }
        return Utility::ConvertErrnoToWasiErrno(err);
    }

    int32_t WasiPthreadExt::PthreadRWLockUnlock(wasm_exec_env_t pExecEnv, uint32_t *rwlock) {
        auto* pManager = GetInstPthreadManager(pExecEnv);
        auto pRWLock = pManager->GetRWLock(rwlock, false);
        if (!pRWLock)
            return UVWASI_EINVAL;
        return Utility::ConvertErrnoToWasiErrno(pthread_rwlock_unlock(pRWLock.get()));
    }

    int32_t WasiPthreadExt::PthreadSetName(wasm_exec_env_t pExecEnv, char *name) {
        Utility::SetCurrentThreadName(name);
        return 0;
    }
}
